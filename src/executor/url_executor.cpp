#include "executor/url_executor.hpp"

#include "app/download_job.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace pastit {
namespace {

std::filesystem::path available_generated_path(std::filesystem::path path) {
    if (!std::filesystem::exists(path)) return path;
    const auto parent=path.parent_path();const auto stem=path.stem().string();const auto extension=path.extension().string();
    for(std::size_t index=2;;++index){auto candidate=parent/(stem+" ("+std::to_string(index)+")"+extension);if(!std::filesystem::exists(candidate))return candidate;}
}

std::vector<std::byte> bytes_from_string(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (unsigned char ch : text) {
        out.push_back(static_cast<std::byte>(ch));
    }
    return out;
}

ExecutionResult result_for(const ActionInstance& action, const ExecutionContext& context, ExecutionStatus status, std::string message) {
    ExecutionResult result;
    result.request_id = context.request_id;
    result.action_id = action.id;
    result.status = status;
    result.message = std::move(message);
    return result;
}

std::filesystem::path target_dir_for(const ActionInstance& action, const ExecutionContext& context) {
    if (const auto confirmed = action.parameters.find("confirmed_destination");
        confirmed != action.parameters.end() && !confirmed->second.empty()) {
        const std::filesystem::path path{confirmed->second};
        std::error_code error;
        if (!std::filesystem::is_directory(path, error) || error) {
            throw std::runtime_error("confirmed target directory not found");
        }
        return path;
    }
    const auto target = context.path_history.find(action.target_ref);
    if (!target.has_value() || target->kind != PathKind::Directory) {
        throw std::runtime_error("target directory not found");
    }
    return target->path;
}

std::optional<std::string> record_text_clipboard(ExecutionContext& context, std::string text, ContentKind kind) {
    const auto item = context.clipboard_store.put(ClipboardData{
        .mime_types = {"text/plain"},
        .bytes = bytes_from_string(text),
        .kind = kind,
        .source_app = "pastit",
        .captured_at_ms = context.now_ms,
    });
    return item.ref;
}

}  // namespace

ExecutionResult execute_url_action(const ActionInstance& action, ExecutionContext& context) {
    try {
        const auto url = context.clipboard_store.read_text(action.source_ref);
        switch (action.kind) {
            case ActionKind::PasteUrl: {
                auto result = result_for(action, context, ExecutionStatus::Completed, "copied URL");
                result.output_clipboard_ref = record_text_clipboard(context, url, ContentKind::Url);
                return result;
            }
            case ActionKind::OpenUrl:
                if (!context.open_uri) {
                    return result_for(action, context, ExecutionStatus::Unsupported, "desktop URL launcher is not configured");
                }
                return result_for(action, context, context.open_uri(url) ? ExecutionStatus::Completed : ExecutionStatus::Failed,
                                  "opened URL");
            case ActionKind::SaveUrlFile:
            case ActionKind::DownloadUrl: {
                const auto filename = action.filename.empty() ? "download.bin" : action.filename;
                const auto output = available_generated_path(target_dir_for(action, context) / filename);
                if (context.download_manager != nullptr) {
                    auto result = result_for(action, context, ExecutionStatus::Executing, "download started");
                    const auto job_id = context.download_manager->start(url, output);
                    result.download_job_id = job_id;
                    result.job_id = job_id;
                    result.output_path = output;
                    result.output_paths.push_back(output);
                    return result;
                }

                DownloadManager manager;
                const auto job_id = manager.start(url, output);
                manager.wait(job_id);
                const auto job = manager.get(job_id);
                if (!job.has_value() || job->status != DownloadStatus::Completed) {
                    return result_for(action, context, ExecutionStatus::Failed,
                                      job.has_value() && !job->error.empty() ? job->error : "URL download failed");
                }
                auto result = result_for(action, context, ExecutionStatus::Completed, "downloaded URL");
                result.output_path = output;
                result.output_paths.push_back(output);
                result.output_clipboard_ref = record_text_clipboard(context, output.string(), ContentKind::Path);
                context.path_history.observe(output.string(), "pastit", context.now_ms);
                return result;
            }
            default:
                return result_for(action, context, ExecutionStatus::Unsupported, "unsupported URL action");
        }
    } catch (const std::exception& error) {
        return result_for(action, context, ExecutionStatus::Failed, error.what());
    }
}

}  // namespace pastit
