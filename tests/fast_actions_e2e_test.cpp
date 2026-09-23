#include "app/download_job.hpp"
#include "decision/candidate_selector.hpp"
#include "executor/action_executor.hpp"
#include "executor/fast_action_executor.hpp"
#include "storage/clipboard_store.hpp"
#include "storage/path_history.hpp"
#include "ui/desktop_flow.hpp"

#include <cassert>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (const unsigned char ch : text) {
        out.push_back(static_cast<std::byte>(ch));
    }
    return out;
}

pastit::ClipboardItem item(std::string ref,
                           pastit::ContentKind kind,
                           std::string preview,
                           std::int64_t captured_at_ms = 100) {
    pastit::ClipboardItem out;
    out.ref = std::move(ref);
    out.kind = kind;
    out.preview = std::move(preview);
    out.size_bytes = out.preview.size();
    out.mime_types = kind == pastit::ContentKind::Image
        ? std::vector<std::string>{"image/png"}
        : std::vector<std::string>{"text/plain"};
    out.captured_at_ms = captured_at_ms;
    return out;
}

pastit::PathLocation directory(std::string ref, const std::filesystem::path& path, std::int64_t seen = 100) {
    return pastit::PathLocation{
        .ref = std::move(ref),
        .path = path,
        .kind = pastit::PathKind::Directory,
        .last_seen_ms = seen,
        .source = "test",
        .exists = true,
    };
}

pastit::DesktopDecisionInput base_input(const std::filesystem::path& root) {
    auto settings = pastit::default_settings();
    pastit::DesktopDecisionInput input;
    input.request_id = "req-e2e";
    input.captured_at_ms = 1000;
    input.recent_paths = {directory("recent", root / "recent")};
    input.focused_target_hash = "focused";
    input.focused_app = "test";
    input.focused_window_title = "test window";
    input.prompt_templates = settings.prompt_templates;
    input.general_llm = {.endpoint = "http://127.0.0.1:9999/v1/chat/completions", .model_id = "local-model"};
    input.default_text_directory = root / "text";
    input.default_image_directory = root / "images";
    input.downloads.resume_directory = root / "downloads";
    input.downloads.keep_part_files = true;
    input.hash.default_algorithms = {"sha256", "sha512"};
    input.date_time.source_zone = "UTC";
    input.date_time.target_zone = "Asia/Singapore";
    return input;
}

std::optional<pastit::ActionInstance> find_kind(const pastit::ActionCatalog& catalog, pastit::ActionKind kind) {
    for (const auto& action : catalog.actions) {
        if (action.kind == kind) {
            return action;
        }
    }
    return std::nullopt;
}

std::optional<pastit::ActionInstance> find_kind_with_target(const pastit::ActionCatalog& catalog,
                                                            pastit::ActionKind kind,
                                                            std::string_view target_ref) {
    for (const auto& action : catalog.actions) {
        if (action.kind == kind && action.target_ref == target_ref) {
            return action;
        }
    }
    return std::nullopt;
}

bool has_kind(const pastit::ActionCatalog& catalog, pastit::ActionKind kind) {
    return find_kind(catalog, kind).has_value();
}

class ScopedTempDir {
public:
    explicit ScopedTempDir(std::string_view prefix) {
        const auto base = std::filesystem::temp_directory_path();
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        std::random_device random;
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto name = std::string{prefix} + "-" + std::to_string(tick) + "-"
                + std::to_string(random()) + "-" + std::to_string(attempt);
            std::error_code error;
            auto candidate = base / name;
            if (std::filesystem::create_directory(candidate, error)) {
                path_ = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("failed to create unique fast-actions e2e temp directory");
    }

    ScopedTempDir(const ScopedTempDir&) = delete;
    ScopedTempDir& operator=(const ScopedTempDir&) = delete;

    ~ScopedTempDir() {
        if (path_.empty()) {
            return;
        }
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

class FakeServices final : public pastit::FastActionServices {
public:
    bool open_terminal(const std::filesystem::path& directory) override {
        terminal_directory = directory;
        return true;
    }

    pastit::ProcessOutput run_network_probe(pastit::NetworkProbe, std::string_view) override {
        return {.exit_code = 0, .stdout_text = "ok"};
    }

    pastit::ProcessOutput clone_repository(std::string_view url,
                                           const std::filesystem::path& destination) override {
        clone_url = std::string{url};
        clone_destination = destination;
        return {.exit_code = 0, .stdout_text = "cloned"};
    }

    std::optional<std::int64_t> parse_datetime(std::string_view, std::string_view) override { return 123; }
    std::string format_datetime(std::int64_t, std::string_view zone) override {
        return std::string{"formatted:"} + std::string{zone};
    }
    std::string hash_file(const std::filesystem::path&, pastit::HashAlgorithm algorithm) override {
        return algorithm == pastit::HashAlgorithm::Sha512 ? "sha512" : "sha256";
    }

    pastit::ProcessOutput run_argv(const std::vector<std::string>& argv) override {
        argv_calls.push_back(argv);
        return {.exit_code = 127, .stderr_text = "execvp failed"};
    }

    std::filesystem::path terminal_directory;
    std::string clone_url;
    std::filesystem::path clone_destination;
    std::vector<std::vector<std::string>> argv_calls;
};

class BlockingDownloadTransport final : public pastit::DownloadTransport {
public:
    pastit::DownloadResponse fetch(const pastit::DownloadRequest& request,
                                   const std::atomic_bool& cancelled) override {
        {
            std::lock_guard lock(mutex_);
            requests.push_back(request);
        }
        changed_.notify_all();
        std::unique_lock lock(mutex_);
        while (!released_ && !cancelled.load()) {
            changed_.wait_for(lock, std::chrono::milliseconds(10));
        }
        if (cancelled.load()) {
            return {.status_code = 0, .error = "cancelled"};
        }
        return {.status_code = 200, .total_size = 12, .range_supported = false, .body = "owned bytes"};
    }

    void wait_for_request() {
        std::unique_lock lock(mutex_);
        const bool ready = changed_.wait_for(lock, std::chrono::seconds(2), [&] {
            return !requests.empty();
        });
        assert(ready);
    }

    void release() {
        {
            std::lock_guard lock(mutex_);
            released_ = true;
        }
        changed_.notify_all();
    }

    std::vector<pastit::DownloadRequest> requests;

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool released_ = false;
};

pastit::ClipboardItem put_text(pastit::ClipboardStore& store,
                               std::string text,
                               pastit::ContentKind kind = pastit::ContentKind::Text) {
    return store.put(pastit::ClipboardData{
        .mime_types = {"text/plain"},
        .bytes = bytes(text),
        .kind = kind,
        .source_app = "fast-actions-e2e-test",
        .captured_at_ms = 100,
    });
}

std::vector<pastit::ExecutionResult> poll_until_done(pastit::FastActionExecutor& executor,
                                                     pastit::ExecutionContext& context) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto results = executor.poll(context);
        if (!results.empty()) {
            return results;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(false && "async job did not complete");
    return {};
}

}  // namespace

int main() {
    using namespace pastit;

    ScopedTempDir temp("pastit-fast-actions-e2e-test");
    const auto& root = temp.path();
    std::filesystem::create_directories(root / "recent");
    std::filesystem::create_directories(root / "text");
    std::filesystem::create_directories(root / "images");
    std::filesystem::create_directories(root / "downloads");

    {
        auto input = base_input(root);
        input.clipboard_items = {
            item("stale_image", ContentKind::Image, "image bytes", 900),
            item("current_url", ContentKind::Url, "https://github.com/acme/widget.git#readme", 1000),
            item("stale_contact", ContentKind::Text, "Ada Lovelace\nada@example.com", 800),
        };

        const auto batch = build_desktop_decision(input);
        assert(batch.request.snapshot.clipboard_items.size() == 1);
        assert(batch.request.snapshot.clipboard_items.front().ref == "current_url");
        assert(has_kind(batch.catalog, ActionKind::CopyGithubSshUrl));
        assert(!has_kind(batch.catalog, ActionKind::AnnotateImage));
        assert(!has_kind(batch.catalog, ActionKind::ExtractContactInfo));

        const auto top_eight = select_djev_candidates(batch.catalog, batch.request.snapshot, 8);
        assert(top_eight.actions.size() <= 8);
        for (const auto& action : top_eight.actions) {
            assert(is_current_source_action(action, batch.request.snapshot));
            assert(action.source_ref == "current_url");
        }
        assert(find_kind_with_target(batch.catalog, ActionKind::DownloadUrl, "default_download").has_value());
    }

    {
        const auto file_path = root / "hash input.txt";
        {
            std::ofstream output(file_path);
            output << "hash me";
        }
        auto input = base_input(root);
        input.clipboard_items = {item("current_path", ContentKind::Path, file_path.string(), 1000)};
        input.hash.default_algorithms = {"sha512"};
        const auto batch = build_desktop_decision(input);
        assert(has_kind(batch.catalog, ActionKind::HashSha512));
        assert(!has_kind(batch.catalog, ActionKind::HashSha256));

        FakeServices services;
        ClipboardStore store(root / "store-terminal");
        PathHistory history;
        ExecutionContext context{store, history, "req-terminal"};
        context.fast_action_services = &services;
        const auto terminal = find_kind(batch.catalog, ActionKind::OpenTerminalAtPath);
        assert(terminal.has_value());
        const auto terminal_result = execute_action(*terminal, context);
        assert(terminal_result.status == ExecutionStatus::Completed);
        assert(services.terminal_directory == file_path.parent_path());
    }

    {
        auto input = base_input(root);
        input.clipboard_items = {item("current_date", ContentKind::Text, "2026-09-21T08:30:00Z", 1000)};
        input.date_time.target_zone = "Asia/Singapore";
        const auto batch = build_desktop_decision(input);
        const auto timezone = find_kind(batch.catalog, ActionKind::ConvertTimezone);
        assert(timezone.has_value());
        assert(timezone->parameters.at("target_zone") == "Asia/Singapore");
    }

    {
        FakeServices services;
        FastActionExecutor executor(services, RendererSettings{});
        ClipboardStore store(root / "store-renderer");
        PathHistory history;
        ExecutionContext context{store, history, "req-renderer"};
        auto qr = ActionInstance{};
        qr.id = "qr";
        qr.kind = ActionKind::GenerateQr;
        qr.source_ref = put_text(store, "https://example.test", ContentKind::Url).ref;
        qr.parameters["payload"] = "https://example.test";
        const auto started = executor.execute(qr, context);
        assert(started.status == ExecutionStatus::Executing);
        const auto completed = poll_until_done(executor, context);
        assert(completed.front().status == ExecutionStatus::Completed);
        assert(executor.renderer_result().has_value());
        assert(executor.renderer_result()->active().status == RendererResultStatus::Ready);
        assert(executor.renderer_result()->active().output_path.has_value());
        assert(executor.renderer_result()->copy_text() == "https://example.test");
    }

    {
        auto transport = std::make_shared<BlockingDownloadTransport>();
        DownloadManager downloads(transport);
        ClipboardStore store(root / "store-download");
        PathHistory history;
        const auto target = history.observe_path(root / "downloads", PathKind::Directory, "test", 1000);
        ExecutionContext context{store, history, "req-download"};
        context.download_manager = &downloads;
        const auto source = put_text(store, "https://example.test/owned.bin", ContentKind::Url);
        ActionInstance download;
        download.id = "download";
        download.kind = ActionKind::DownloadUrl;
        download.source_ref = source.ref;
        download.target_ref = target.ref;
        download.filename = "owned.bin";
        download.enabled = true;

        const auto started = execute_action(download, context);
        assert(started.status == ExecutionStatus::Executing);
        assert(started.download_job_id.has_value());
        transport->wait_for_request();
        const auto active = downloads.get(*started.download_job_id);
        assert(active.has_value());
        assert(active->status == DownloadStatus::Running);
        transport->release();
        downloads.wait(*started.download_job_id);
        const auto completed = downloads.get(*started.download_job_id);
        assert(completed.has_value());
        assert(completed->status == DownloadStatus::Completed);
    }
}
