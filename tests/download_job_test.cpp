#include "app/download_job.hpp"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

class ScopedTempRoot {
public:
    ScopedTempRoot() {
        std::random_device random;
        for (int attempt = 0; attempt < 16; ++attempt) {
            path_ = std::filesystem::temp_directory_path() /
                ("pastit-download-job-" + std::to_string(random()) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(path_)) {
                return;
            }
        }
        throw std::runtime_error("failed to create unique download test directory");
    }

    ~ScopedTempRoot() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

class BlockingTransport final : public pastit::DownloadTransport {
public:
    explicit BlockingTransport(pastit::DownloadResponse response) : response_(std::move(response)) {}

    pastit::DownloadResponse fetch(const pastit::DownloadRequest& request,
                                   const std::atomic_bool& cancelled) override {
        {
            std::lock_guard lock(mutex_);
            requests_.push_back(request);
            requested_ = true;
        }
        changed_.notify_all();

        std::unique_lock lock(mutex_);
        while (!release_ && !cancelled.load()) {
            changed_.wait_for(lock, std::chrono::milliseconds(10));
        }
        if (cancelled.load()) {
            return {.status_code = 0, .error = "cancelled"};
        }
        return response_;
    }

    void wait_for_request_count(std::size_t count) {
        std::unique_lock lock(mutex_);
        const bool ready = changed_.wait_for(lock, std::chrono::seconds(2), [&] {
            return requests_.size() >= count;
        });
        assert(ready);
    }

    void set_response(pastit::DownloadResponse response) {
        std::lock_guard lock(mutex_);
        response_ = std::move(response);
        release_ = false;
        requested_ = false;
    }

    void release() {
        {
            std::lock_guard lock(mutex_);
            release_ = true;
        }
        changed_.notify_all();
    }

    std::vector<pastit::DownloadRequest> requests() const {
        std::lock_guard lock(mutex_);
        return requests_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    pastit::DownloadResponse response_;
    std::vector<pastit::DownloadRequest> requests_;
    bool requested_ = false;
    bool release_ = false;
};

class ImmediateTransport final : public pastit::DownloadTransport {
public:
    explicit ImmediateTransport(pastit::DownloadResponse response) : response_(std::move(response)) {}

    pastit::DownloadResponse fetch(const pastit::DownloadRequest& request,
                                   const std::atomic_bool&) override {
        requests.push_back(request);
        return response_;
    }

    pastit::DownloadResponse response_;
    std::vector<pastit::DownloadRequest> requests;
};

class ThrowingTransport final : public pastit::DownloadTransport {
public:
    pastit::DownloadResponse fetch(const pastit::DownloadRequest&, const std::atomic_bool&) override {
        throw std::runtime_error("transport exploded");
    }
};

class StagedStreamingTransport final : public pastit::DownloadTransport {
public:
    explicit StagedStreamingTransport(bool hold_after_stop = false) : hold_after_stop_(hold_after_stop) {}

    pastit::DownloadResponse fetch(const pastit::DownloadRequest&, const std::atomic_bool&) override {
        return {.status_code = 0, .error = "stream was not used"};
    }

    pastit::DownloadResponse stream(
        const pastit::DownloadRequest& request, const std::atomic_bool& cancelled,
        const std::function<bool(const pastit::DownloadResponse&)>& on_headers,
        const std::function<bool(std::string_view)>& on_chunk) override {
        {
            std::lock_guard lock(mutex_);
            requests_.push_back(request);
        }
        changed_.notify_all();
        const bool first = request.range_start == 0;
        pastit::DownloadResponse response{
            .status_code = first ? 200 : 206,
            .total_size = 11,
            .range_supported = !first,
            .range_start = first ? std::optional<std::uint64_t>{} : request.range_start,
        };
        if (!on_headers(response) || !on_chunk(first ? "hello " : "world")) {
            return {.status_code = 0, .error = "sink rejected transfer"};
        }
        {
            std::lock_guard lock(mutex_);
            chunk_delivered_ = true;
        }
        changed_.notify_all();
        if (first) {
            std::unique_lock lock(mutex_);
            changed_.wait_for(lock, std::chrono::seconds(2), [&] {
                return released_ || (!hold_after_stop_ && cancelled.load());
            });
            if (cancelled.load()) {
                return {.status_code = 0, .error = "cancelled"};
            }
        }
        return response;
    }

    void wait_for_chunk() {
        std::unique_lock lock(mutex_);
        assert(changed_.wait_for(lock, std::chrono::seconds(2), [&] { return chunk_delivered_; }));
    }

    void release() {
        {
            std::lock_guard lock(mutex_);
            released_ = true;
        }
        changed_.notify_all();
    }

    std::vector<pastit::DownloadRequest> requests() const {
        std::lock_guard lock(mutex_);
        return requests_;
    }

private:
    bool hold_after_stop_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<pastit::DownloadRequest> requests_;
    bool chunk_delivered_ = false;
    bool released_ = false;
};

class StreamingReplyTransport final : public pastit::DownloadTransport {
public:
    explicit StreamingReplyTransport(pastit::DownloadResponse response) : response_(std::move(response)) {}

    pastit::DownloadResponse fetch(const pastit::DownloadRequest&, const std::atomic_bool&) override {
        return {.status_code = 0, .error = "stream was not used"};
    }

    pastit::DownloadResponse stream(
        const pastit::DownloadRequest&, const std::atomic_bool&,
        const std::function<bool(const pastit::DownloadResponse&)>& on_headers,
        const std::function<bool(std::string_view)>& on_chunk) override {
        if (!on_headers(response_) || !on_chunk("fresh body")) {
            return {.status_code = 0, .error = "sink rejected transfer"};
        }
        return response_;
    }

private:
    pastit::DownloadResponse response_;
};

}  // namespace

int main() {
    using namespace pastit;

    const ScopedTempRoot temp_root;
    const auto& root = temp_root.path();

    {
        const auto source_path = root / "source-file.bin";
        const auto final_path = root / "file-download.bin";
        write_text(source_path, "file transport bytes");
        DownloadManager manager;
        const auto id = manager.start("file://" + source_path.string(), final_path);
        manager.wait(id);
        assert(manager.get(id)->status == DownloadStatus::Completed);
        assert(read_text(final_path) == "file transport bytes");
    }

    {
        const auto final_path = root / "resume.txt";
        const auto part_path = final_path.string() + ".part";
        write_text(part_path, "hello ");

        auto transport = std::make_shared<BlockingTransport>(DownloadResponse{
            .status_code = 206,
            .total_size = 11,
            .range_supported = true,
            .body = "world",
            .range_start = 6,
        });
        DownloadManager manager(transport);

        const auto id = manager.start("https://example.test/resume.txt", final_path);
        transport->wait_for_request_count(1);
        assert(manager.get(id)->status == DownloadStatus::Running);
        manager.cancel(id);
        manager.wait(id);

        auto cancelled = manager.get(id);
        assert(cancelled.has_value());
        assert(cancelled->status == DownloadStatus::Cancelled);
        assert(read_text(part_path) == "hello ");
        assert(!std::filesystem::exists(final_path));
        assert(transport->requests().front().range_start == 6);

        transport->set_response(DownloadResponse{
            .status_code = 206,
            .total_size = 11,
            .range_supported = true,
            .body = "world",
            .range_start = 6,
        });
        manager.resume(id);
        transport->wait_for_request_count(2);
        transport->release();
        manager.wait(id);

        const auto completed = manager.get(id);
        assert(completed.has_value());
        assert(completed->status == DownloadStatus::Completed);
        assert(completed->downloaded == 11);
        assert(completed->total == 11);
        assert(completed->range_supported);
        assert(read_text(final_path) == "hello world");
        assert(!std::filesystem::exists(part_path));
        assert(transport->requests().back().range_start == 6);

        manager.resume(id);
        assert(transport->requests().size() == 2);
    }

    {
        const auto final_path = root / "fallback.txt";
        const auto part_path = final_path.string() + ".part";
        write_text(part_path, "stale partial");

        auto transport = std::make_shared<ImmediateTransport>(DownloadResponse{
            .status_code = 200,
            .total_size = 10,
            .range_supported = false,
            .body = "fresh body",
        });
        DownloadManager manager(transport);

        const auto id = manager.start("https://example.test/fallback.txt", final_path);
        manager.wait(id);

        const auto job = manager.get(id);
        assert(job.has_value());
        assert(job->status == DownloadStatus::Completed);
        assert(!job->range_supported);
        assert(job->downloaded == 10);
        assert(read_text(final_path) == "fresh body");
        assert(!std::filesystem::exists(part_path));
        assert(transport->requests.size() == 1);
        assert(transport->requests.front().range_start == 13);
    }

    {
        const auto final_path = root / "server-error.txt";
        const auto part_path = final_path.string() + ".part";
        write_text(part_path, "keep me");

        auto transport = std::make_shared<ImmediateTransport>(DownloadResponse{
            .status_code = 404,
            .total_size = 9,
            .range_supported = false,
            .body = "not found",
            .error = "not found",
        });
        DownloadManager manager(transport);

        const auto id = manager.start("https://example.test/missing.txt", final_path);
        manager.wait(id);

        const auto job = manager.get(id);
        assert(job.has_value());
        assert(job->status == DownloadStatus::Failed);
        assert(read_text(part_path) == "keep me");
        assert(!std::filesystem::exists(final_path));
    }

    {
        const auto final_path = root / "stream-pause.txt";
        const auto part_path = final_path.string() + ".part";
        auto transport = std::make_shared<StagedStreamingTransport>();
        DownloadManager manager(transport, DownloadManager::Options{.keep_part_files_on_cancel = false});
        const auto id = manager.start("https://example.test/stream-pause.txt", final_path);
        transport->wait_for_chunk();
        assert(read_text(part_path) == "hello ");
        manager.pause(id);
        manager.wait(id);
        assert(manager.get(id)->status == DownloadStatus::Paused);
        assert(read_text(part_path) == "hello ");

        manager.resume(id);
        manager.wait(id);
        assert(manager.get(id)->status == DownloadStatus::Completed);
        assert(read_text(final_path) == "hello world");
        assert(transport->requests().size() == 2);
        assert(transport->requests()[1].range_start == 6);
    }

    {
        const auto final_path = root / "stream-cancel.txt";
        const auto part_path = final_path.string() + ".part";
        auto transport = std::make_shared<StagedStreamingTransport>(true);
        DownloadManager manager(transport, DownloadManager::Options{.keep_part_files_on_cancel = false});
        const auto id = manager.start("https://example.test/stream-cancel.txt", final_path);
        transport->wait_for_chunk();
        manager.cancel(id);
        assert(read_text(part_path) == "hello ");
        transport->release();
        manager.wait(id);
        assert(!std::filesystem::exists(part_path));
        assert(manager.get(id)->status == DownloadStatus::Cancelled);
    }

    {
        const auto final_path = root / "mismatched-range.txt";
        const auto part_path = final_path.string() + ".part";
        write_text(part_path, "prior");
        auto transport = std::make_shared<StreamingReplyTransport>(DownloadResponse{
            .status_code = 206, .total_size = 15, .range_supported = true, .range_start = 0,
        });
        DownloadManager manager(transport);
        const auto id = manager.start("https://example.test/mismatched-range.txt", final_path);
        manager.wait(id);
        assert(manager.get(id)->status == DownloadStatus::Failed);
        assert(read_text(part_path) == "prior");
        assert(!std::filesystem::exists(final_path));
    }

    {
        const auto final_path = root / "stream-restart.txt";
        const auto part_path = final_path.string() + ".part";
        write_text(part_path, "stale");
        auto transport = std::make_shared<StreamingReplyTransport>(DownloadResponse{
            .status_code = 200, .total_size = 10,
        });
        DownloadManager manager(transport);
        const auto id = manager.start("https://example.test/stream-restart.txt", final_path);
        manager.wait(id);
        assert(manager.get(id)->status == DownloadStatus::Completed);
        assert(read_text(final_path) == "fresh body");
    }

    {
        const auto final_path = root / "pause-preserves-part.txt";
        const auto part_path = final_path.string() + ".part";
        write_text(part_path, "partial bytes");

        auto transport = std::make_shared<BlockingTransport>(DownloadResponse{
            .status_code = 206,
            .total_size = 18,
            .range_supported = true,
            .body = "ignored",
        });
        DownloadManager manager(transport, DownloadManager::Options{.keep_part_files_on_cancel = false});
        const auto id = manager.start("https://example.test/pause-preserves-part.txt", final_path);
        transport->wait_for_request_count(1);
        manager.pause(id);
        manager.wait(id);

        assert(manager.get(id)->status == DownloadStatus::Paused);
        assert(read_text(part_path) == "partial bytes");
        assert(!std::filesystem::exists(final_path));
    }

    {
        const auto final_path = root / "discard-on-cancel.txt";
        const auto part_path = final_path.string() + ".part";
        write_text(part_path, "partial bytes");

        auto transport = std::make_shared<BlockingTransport>(DownloadResponse{
            .status_code = 206,
            .total_size = 18,
            .range_supported = true,
            .body = "ignored",
        });
        DownloadManager manager(transport, DownloadManager::Options{.keep_part_files_on_cancel = false});

        const auto id = manager.start("https://example.test/discard-on-cancel.txt", final_path);
        transport->wait_for_request_count(1);
        manager.cancel(id);
        manager.wait(id);

        const auto job = manager.get(id);
        assert(job.has_value());
        assert(job->status == DownloadStatus::Cancelled);
        assert(!std::filesystem::exists(part_path));
        assert(!std::filesystem::exists(final_path));
    }

    {
        const auto final_path = root / "fresh-server-error.txt";
        auto transport = std::make_shared<ImmediateTransport>(DownloadResponse{
            .status_code = 404,
            .total_size = 9,
            .range_supported = false,
            .body = "not found",
            .error = "not found",
        });
        DownloadManager manager(transport);

        const auto id = manager.start("https://example.test/fresh-missing.txt", final_path);
        manager.wait(id);

        const auto job = manager.get(id);
        assert(job.has_value());
        assert(job->status == DownloadStatus::Failed);
        assert(!std::filesystem::exists(final_path));
        assert(!std::filesystem::exists(final_path.string() + ".part"));
    }

    {
        const auto final_path = root / "running.txt";
        auto transport = std::make_shared<BlockingTransport>(DownloadResponse{
            .status_code = 200,
            .total_size = 4,
            .range_supported = false,
            .body = "done",
        });
        DownloadManager manager(transport);

        const auto id = manager.start("https://example.test/running.txt", final_path);
        transport->wait_for_request_count(1);
        manager.resume(id);
        assert(transport->requests().size() == 1);
        transport->release();
        manager.wait(id);
        assert(manager.get(id)->status == DownloadStatus::Completed);
        manager.resume(id);
        assert(transport->requests().size() == 1);
    }

    {
        auto transport = std::make_shared<ThrowingTransport>();
        DownloadManager manager(transport);
        const auto id = manager.start("https://example.test/throws.bin", root / "throws.bin");
        manager.wait(id);
        const auto job = manager.get(id);
        assert(job.has_value());
        assert(job->status == DownloadStatus::Failed);
        assert(job->error.find("transport exploded") != std::string::npos);
    }

    {
        const auto non_directory = root / "not-a-directory";
        write_text(non_directory, "file blocks directory creation");
        auto transport = std::make_shared<ImmediateTransport>(DownloadResponse{
            .status_code = 200,
            .total_size = 4,
            .range_supported = false,
            .body = "data",
        });
        DownloadManager manager(transport);
        const auto destination = non_directory / "output.bin";
        const auto id = manager.start("https://example.test/output.bin", destination);
        manager.wait(id);
        const auto job = manager.get(id);
        assert(job.has_value());
        assert(job->status == DownloadStatus::Failed);
        assert(!job->error.empty());
        assert(!std::filesystem::exists(destination));
    }

}
