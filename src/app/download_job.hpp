#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace pastit {

enum class DownloadStatus { Queued, Running, Paused, Completed, Cancelled, Failed };

struct DownloadJob {
    std::string id;
    std::string url;
    std::filesystem::path part_path;
    std::filesystem::path final_path;
    std::uint64_t downloaded = 0;
    std::uint64_t total = 0;
    bool range_supported = false;
    DownloadStatus status = DownloadStatus::Queued;
    std::string error;
};

struct DownloadRequest {
    std::string url;
    std::uint64_t range_start = 0;
};

struct DownloadResponse {
    int status_code = 0;
    std::uint64_t total_size = 0;
    bool range_supported = false;
    std::string body;
    std::string error;
    std::optional<std::uint64_t> range_start;
};

class DownloadTransport {
public:
    virtual ~DownloadTransport() = default;
    virtual DownloadResponse fetch(const DownloadRequest& request, const std::atomic_bool& cancelled) = 0;
    virtual DownloadResponse stream(
        const DownloadRequest& request, const std::atomic_bool& cancelled,
        const std::function<bool(const DownloadResponse&)>& on_headers,
        const std::function<bool(std::string_view)>& on_chunk);
};

class DownloadManager {
public:
    struct Options {
        bool keep_part_files_on_cancel = true;
    };

    explicit DownloadManager(std::shared_ptr<DownloadTransport> transport = {});
    DownloadManager(std::shared_ptr<DownloadTransport> transport, Options options);
    ~DownloadManager();

    DownloadManager(const DownloadManager&) = delete;
    DownloadManager& operator=(const DownloadManager&) = delete;

    std::string start(std::string url, std::filesystem::path destination);
    void pause(std::string_view id);
    void resume(std::string_view id);
    void cancel(std::string_view id);
    std::optional<DownloadJob> get(std::string_view id) const;
    void wait(std::string_view id);
    void set_options(Options options);

private:
    struct JobControl;

    void spawn_worker(const std::shared_ptr<JobControl>& control);
    bool join_finished_worker(const std::shared_ptr<JobControl>& control);
    std::shared_ptr<JobControl> find_control(std::string_view id) const;

    std::shared_ptr<DownloadTransport> transport_;
    Options options_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<JobControl>> jobs_;
    std::uint64_t next_id_ = 0;
};

}  // namespace pastit
