#include "app/download_job.hpp"

#include <array>
#include <charconv>
#include <cctype>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#if defined(PASTIT_HAS_CURL)
#include <curl/curl.h>
#endif

namespace pastit {
namespace {

std::filesystem::path part_path_for(const std::filesystem::path& final_path) {
    return std::filesystem::path{final_path.string() + ".part"};
}

std::uint64_t file_size_or_zero(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    return error ? 0 : size;
}

#if defined(PASTIT_HAS_CURL)
std::string_view trim_header(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    return value;
}

std::optional<std::uint64_t> parse_number(std::string_view value) {
    value = trim_header(value);
    std::uint64_t number = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    return error == std::errc{} && end == value.data() + value.size()
        ? std::optional<std::uint64_t>{number} : std::nullopt;
}

bool header_name_is(std::string_view line, std::string_view name) {
    if (line.size() <= name.size() || line[name.size()] != ':') return false;
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(line[i])) != name[i]) return false;
    }
    return true;
}

struct CurlHeaders {
    DownloadResponse response;
    std::optional<std::uint64_t> content_length;
    bool valid_range = false;
};

void parse_content_range(std::string_view value, CurlHeaders& headers) {
    value = trim_header(value);
    if (!value.starts_with("bytes ")) return;
    value.remove_prefix(6);
    const auto dash = value.find('-');
    const auto slash = value.find('/');
    if (dash == std::string_view::npos || slash == std::string_view::npos || dash >= slash) return;
    const auto start = parse_number(value.substr(0, dash));
    const auto end = parse_number(value.substr(dash + 1, slash - dash - 1));
    if (!start || !end || *end < *start) return;
    headers.response.range_start = start;
    const auto total = parse_number(value.substr(slash + 1));
    if (total && *end >= *total) return;
    if (total) headers.response.total_size = *total;
    headers.valid_range = true;
}
#endif

class FileDownloadTransport final : public DownloadTransport {
public:
    DownloadResponse fetch(const DownloadRequest& request, const std::atomic_bool& cancelled) override {
        std::string body;
        auto response = stream(request, cancelled,
            [](const DownloadResponse&) { return true; },
            [&body](std::string_view chunk) { body.append(chunk); return true; });
        response.body = std::move(body);
        return response;
    }

    DownloadResponse stream(const DownloadRequest& request, const std::atomic_bool& cancelled,
                            const std::function<bool(const DownloadResponse&)>& on_headers,
                            const std::function<bool(std::string_view)>& on_chunk) override {
        constexpr std::string_view file_prefix = "file://";
        if (request.url.starts_with(file_prefix)) {
            const std::filesystem::path path{request.url.substr(file_prefix.size())};
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                return {.status_code = 404, .body = {}, .error = "source file not found", .range_start = {}};
            }
            input.seekg(0, std::ios::end);
            const auto total = static_cast<std::uint64_t>(input.tellg());
            if (request.range_start > total) {
                return {.status_code = 416, .total_size = total, .body = {}, .error = "range outside file", .range_start = {}};
            }
            DownloadResponse response{
                .status_code = request.range_start == 0 ? 200 : 206,
                .total_size = total,
                .range_supported = request.range_start != 0,
                .body = {},
                .error = {},
                .range_start = request.range_start == 0 ? std::nullopt : std::optional{request.range_start},
            };
            if (!on_headers(response)) return {.status_code = 0, .body = {}, .error = "download sink rejected headers", .range_start = {}};
            input.seekg(static_cast<std::streamoff>(request.range_start), std::ios::beg);
            std::array<char, 4096> buffer{};
            while (!cancelled.load() && input) {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto count = static_cast<std::size_t>(input.gcount());
                if (count > 0 && !on_chunk(std::string_view(buffer.data(), count))) {
                    return {.status_code = 0, .body = {}, .error = "download sink rejected bytes", .range_start = {}};
                }
            }
            if (cancelled.load()) {
                return {.status_code = 0, .total_size = total, .body = {}, .error = "cancelled", .range_start = {}};
            }
            if (input.bad()) return {.status_code = 0, .body = {}, .error = "failed to read source file", .range_start = {}};
            return response;
        }

#if defined(PASTIT_HAS_CURL)
        CURL* curl = curl_easy_init();
        if (curl == nullptr) {
            return {.status_code = 0, .body = {}, .error = "curl initialization failed"};
        }
        struct CurlState {
            const std::atomic_bool* cancelled = nullptr;
            const std::function<bool(const DownloadResponse&)>* on_headers = nullptr;
            const std::function<bool(std::string_view)>* on_chunk = nullptr;
            CurlHeaders headers;
            bool accepted_headers = false;
            bool sink_rejected = false;
            std::uint64_t received = 0;
        } state{.cancelled = &cancelled, .on_headers = &on_headers, .on_chunk = &on_chunk};
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 0L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
            auto* state = static_cast<CurlState*>(userdata);
            const auto total = size * nmemb;
            if (!state->accepted_headers && state->headers.response.status_code >= 300 &&
                state->headers.response.status_code < 400) return total;
            if (!state->accepted_headers || state->cancelled->load() ||
                !(*state->on_chunk)(std::string_view(ptr, total))) {
                state->sink_rejected = true;
                return std::size_t{0};
            }
            state->received += total;
            return total;
        });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, +[](char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
            auto* state = static_cast<CurlState*>(userdata);
            const auto length = size * nmemb;
            const std::string_view line(ptr, length);
            if (line.starts_with("HTTP/")) {
                state->headers = {};
                state->accepted_headers = false;
                const auto first_space = line.find(' ');
                if (first_space != std::string_view::npos && first_space + 4 <= line.size()) {
                    const auto code = parse_number(line.substr(first_space + 1, 3));
                    if (code) state->headers.response.status_code = static_cast<int>(*code);
                }
            } else if (header_name_is(line, "content-range")) {
                parse_content_range(line.substr(14), state->headers);
            } else if (header_name_is(line, "content-length")) {
                state->headers.content_length = parse_number(line.substr(15));
            } else if (trim_header(line).empty()) {
                auto& headers = state->headers;
                const auto status = headers.response.status_code;
                if (status >= 200 && status < 300) {
                    if (status == 206 && !headers.valid_range) {
                        state->sink_rejected = true;
                        return std::size_t{0};
                    }
                    headers.response.range_supported = status == 206;
                    if (headers.response.total_size == 0 && headers.content_length) {
                        headers.response.total_size = *headers.content_length +
                            (status == 206 ? headers.response.range_start.value_or(0) : 0);
                    }
                    if (!(*state->on_headers)(headers.response)) {
                        state->sink_rejected = true;
                        return std::size_t{0};
                    }
                    state->accepted_headers = true;
                }
            }
            return length;
        });
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &state);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, +[](void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
            const auto* state = static_cast<CurlState*>(userdata);
            return state->cancelled->load() ? 1 : 0;
        });
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
        std::string range;
        if (request.range_start > 0) {
            range = std::to_string(request.range_start) + "-";
            curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
        }
#if LIBCURL_VERSION_NUM >= 0x075500
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https,file");
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS | CURLPROTO_FILE);
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
        const auto code = curl_easy_perform(curl);
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        curl_easy_cleanup(curl);
        if (cancelled.load()) {
            return {.status_code = 0, .body = {}, .error = "cancelled"};
        }
        if (code != CURLE_OK || state.sink_rejected) {
            return {.status_code = 0, .body = {}, .error = curl_easy_strerror(code)};
        }
        auto response = state.headers.response;
        response.status_code = static_cast<int>(response_code);
        if (response.total_size == 0) response.total_size = state.received +
            (response.status_code == 206 ? request.range_start : 0);
        return response;
#else
        (void)cancelled;
        return {.status_code = 0, .body = {}, .error = "HTTP download transport requires libcurl", .range_start = {}};
#endif
    }
};

}  // namespace

DownloadResponse DownloadTransport::stream(
    const DownloadRequest& request, const std::atomic_bool& cancelled,
    const std::function<bool(const DownloadResponse&)>& on_headers,
    const std::function<bool(std::string_view)>& on_chunk) {
    auto response = fetch(request, cancelled);
    if (cancelled.load() || !on_headers(response)) return response;
    if (!response.body.empty() && !on_chunk(response.body)) {
        return {.status_code = 0, .body = {}, .error = "download sink rejected bytes", .range_start = {}};
    }
    return response;
}

struct DownloadManager::JobControl {
    enum class StopReason { None, Pause, Cancel };
    mutable std::mutex mutex;
    DownloadJob job;
    std::atomic_bool cancel_requested = false;
    std::atomic_bool worker_finished = true;
    StopReason stop_reason = StopReason::None;
    std::thread worker;
};

DownloadManager::DownloadManager(std::shared_ptr<DownloadTransport> transport)
    : DownloadManager(std::move(transport), Options{}) {}

DownloadManager::DownloadManager(std::shared_ptr<DownloadTransport> transport, Options options)
    : transport_(std::move(transport)), options_(options) {
    if (!transport_) {
        transport_ = std::make_shared<FileDownloadTransport>();
    }
}

DownloadManager::~DownloadManager() {
    std::vector<std::shared_ptr<JobControl>> controls;
    {
        std::lock_guard lock(mutex_);
        for (const auto& [id, control] : jobs_) {
            (void)id;
            control->cancel_requested.store(true);
            controls.push_back(control);
        }
    }
    for (auto& control : controls) {
        if (control->worker.joinable()) {
            control->worker.join();
        }
    }
}

std::string DownloadManager::start(std::string url, std::filesystem::path destination) {
    auto control = std::make_shared<JobControl>();
    {
        std::lock_guard lock(mutex_);
        control->job.id = "download-" + std::to_string(++next_id_);
        control->job.url = std::move(url);
        control->job.final_path = std::move(destination);
        control->job.part_path = part_path_for(control->job.final_path);
        jobs_[control->job.id] = control;
    }
    const auto id = control->job.id;
    spawn_worker(control);
    return id;
}

void DownloadManager::pause(std::string_view id) {
    auto control = find_control(id);
    if (!control) {
        return;
    }
    std::lock_guard lock(control->mutex);
    if (control->job.status == DownloadStatus::Running) {
        control->stop_reason = JobControl::StopReason::Pause;
        control->job.status = DownloadStatus::Paused;
        control->cancel_requested.store(true);
    }
}

void DownloadManager::resume(std::string_view id) {
    auto control = find_control(id);
    if (!control) {
        return;
    }
    {
        std::lock_guard lock(control->mutex);
        if (control->job.status == DownloadStatus::Running || control->job.status == DownloadStatus::Completed) {
            return;
        }
    }
    if (!join_finished_worker(control)) {
        return;
    }
    spawn_worker(control);
}

void DownloadManager::cancel(std::string_view id) {
    auto control = find_control(id);
    if (!control) {
        return;
    }
    std::lock_guard lock(control->mutex);
    if (control->job.status != DownloadStatus::Completed) {
        control->stop_reason = JobControl::StopReason::Cancel;
        control->job.status = DownloadStatus::Cancelled;
        control->cancel_requested.store(true);
        bool keep_part_files = true;
        {
            std::lock_guard manager_lock(mutex_);
            keep_part_files = options_.keep_part_files_on_cancel;
        }
        if (control->worker_finished.load() && !keep_part_files) {
            std::error_code ignored;
            std::filesystem::remove(control->job.part_path, ignored);
        }
    }
}

std::optional<DownloadJob> DownloadManager::get(std::string_view id) const {
    auto control = find_control(id);
    if (!control) {
        return std::nullopt;
    }
    std::lock_guard lock(control->mutex);
    return control->job;
}

void DownloadManager::wait(std::string_view id) {
    auto control = find_control(id);
    if (control && control->worker.joinable()) {
        control->worker.join();
    }
}

void DownloadManager::set_options(Options options) {
    std::lock_guard lock(mutex_);
    options_ = options;
}

void DownloadManager::spawn_worker(const std::shared_ptr<JobControl>& control) {
    {
        std::lock_guard lock(control->mutex);
        control->stop_reason = JobControl::StopReason::None;
        control->cancel_requested.store(false);
        control->worker_finished.store(false);
        control->job.status = DownloadStatus::Running;
        control->job.error.clear();
        control->job.downloaded = file_size_or_zero(control->job.part_path);
        if (control->job.total < control->job.downloaded) {
            control->job.total = control->job.downloaded;
        }
    }

    auto transport = transport_;
    Options options;
    {
        std::lock_guard lock(mutex_);
        options = options_;
    }
    control->worker = std::thread([control, transport, options] {
        const auto finish = [&control, &options] {
            std::lock_guard lock(control->mutex);
            if (control->stop_reason == JobControl::StopReason::Cancel &&
                !options.keep_part_files_on_cancel) {
                std::error_code ignored;
                std::filesystem::remove(control->job.part_path, ignored);
            }
            control->worker_finished.store(true);
        };
        try {
        DownloadRequest request;
        std::filesystem::path part_path;
        std::filesystem::path final_path;
        {
            std::lock_guard lock(control->mutex);
            request = DownloadRequest{
                .url = control->job.url,
                .range_start = control->job.downloaded,
            };
            part_path = control->job.part_path;
            final_path = control->job.final_path;
        }

        std::ofstream output;
        bool headers_accepted = false;
        std::string sink_error;
        DownloadResponse accepted_headers;
        const auto on_headers = [&](const DownloadResponse& headers) {
            std::lock_guard lock(control->mutex);
            if (control->cancel_requested.load()) return false;
            if (headers.status_code <= 0 || headers.status_code >= 400) return false;
            const bool resuming = request.range_start > 0;
            const bool append = resuming && headers.status_code == 206 &&
                headers.range_start == request.range_start;
            const bool restart = headers.status_code == 200;
            if (!append && !restart) {
                sink_error = "range response did not match requested offset";
                return false;
            }
            std::error_code error;
            std::filesystem::create_directories(part_path.parent_path(), error);
            if (error) {
                sink_error = error.message();
                return false;
            }
            const auto mode = append ? (std::ios::binary | std::ios::app)
                                     : (std::ios::binary | std::ios::trunc);
            output.open(part_path, mode);
            if (!output) {
                sink_error = "failed to open partial download";
                return false;
            }
            headers_accepted = true;
            accepted_headers = headers;
            control->job.downloaded = append ? request.range_start : 0;
            control->job.total = headers.total_size;
            control->job.range_supported = append;
            return true;
        };
        const auto on_chunk = [&](std::string_view bytes) {
            std::lock_guard lock(control->mutex);
            if (control->cancel_requested.load() || !headers_accepted) return false;
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            output.flush();
            if (!output) {
                sink_error = "failed to write partial download";
                return false;
            }
            control->job.downloaded += bytes.size();
            if (control->job.total < control->job.downloaded) {
                control->job.total = control->job.downloaded;
            }
            return true;
        };

        const auto response = transport->stream(request, control->cancel_requested, on_headers, on_chunk);
        if (output.is_open()) output.close();
        if (control->cancel_requested.load()) {
            finish();
            return;
        }
        if (!headers_accepted || response.status_code <= 0 || response.status_code >= 400 ||
            !sink_error.empty() || !output) {
            {
                std::lock_guard lock(control->mutex);
                control->job.status = DownloadStatus::Failed;
                control->job.error = !sink_error.empty() ? sink_error :
                    (response.error.empty() ? "download failed" : response.error);
            }
            finish();
            return;
        }
        std::error_code error;
        {
            std::lock_guard lock(control->mutex);
            if (control->cancel_requested.load()) {
                // The requested stop wins over a transfer that just completed.
            } else {
                std::filesystem::rename(part_path, final_path, error);
                if (error) {
                    control->job.status = DownloadStatus::Failed;
                    control->job.error = error.message();
                } else {
                    control->job.status = DownloadStatus::Completed;
                    control->job.total = accepted_headers.total_size == 0
                        ? control->job.downloaded : accepted_headers.total_size;
                }
            }
        }
        finish();
        } catch (const std::exception& error) {
            {
                std::lock_guard lock(control->mutex);
                if (!control->cancel_requested.load()) {
                    control->job.status = DownloadStatus::Failed;
                    control->job.error = error.what();
                }
            }
            finish();
        } catch (...) {
            {
                std::lock_guard lock(control->mutex);
                if (!control->cancel_requested.load()) {
                    control->job.status = DownloadStatus::Failed;
                    control->job.error = "download failed with an unknown error";
                }
            }
            finish();
        }
    });
}

bool DownloadManager::join_finished_worker(const std::shared_ptr<JobControl>& control) {
    if (!control->worker.joinable()) {
        return true;
    }
    if (!control->worker_finished.load()) {
        return false;
    }
    control->worker.join();
    return true;
}

std::shared_ptr<DownloadManager::JobControl> DownloadManager::find_control(std::string_view id) const {
    std::lock_guard lock(mutex_);
    const auto found = jobs_.find(std::string{id});
    return found == jobs_.end() ? nullptr : found->second;
}

}  // namespace pastit
