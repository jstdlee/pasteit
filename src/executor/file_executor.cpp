#include "executor/file_executor.hpp"

#include "detect/resume_detector.hpp"
#include "history/path_history.hpp"
#include "util/json.hpp"
#include "util/path_utf8.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace pastit {
namespace {

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
        const auto path = path_from_utf8_string(confirmed->second);
        std::error_code error;
        if (!std::filesystem::is_directory(path, error) || error) {
            throw std::runtime_error("confirmed target directory not found");
        }
        return path;
    }
    if (action.target_ref == "temp") {
        return std::filesystem::temp_directory_path();
    }
    const auto target = context.path_history.find(action.target_ref);
    if (!target.has_value() || target->kind != PathKind::Directory) {
        throw std::runtime_error("target directory not found");
    }
    return target->path;
}

std::string default_filename_for(const ActionInstance& action, std::string_view extension) {
    if (!action.filename.empty()) {
        return action.filename;
    }
    return action.source_ref + "-" + std::string(extension);
}

void write_bytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to open output file");
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::string json_escape(std::string_view value) {
    std::ostringstream out;
    out << std::hex;
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\u" << std::setw(4) << std::setfill('0') << static_cast<unsigned int>(ch);
                    out << std::setfill(' ');
                } else {
                    out << static_cast<char>(ch);
                }
        }
    }
    return out.str();
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

void record_output_path(ExecutionContext& context, const std::filesystem::path& path, ExecutionResult& result) {
    result.output_path = path;
    result.output_paths.push_back(path);
    result.output_clipboard_ref = record_text_clipboard(context, path_to_utf8_string(path), ContentKind::Path);
    context.path_history.observe(path_to_utf8_string(path), "pastit", context.now_ms);
}

std::filesystem::path available_generated_path(std::filesystem::path path) {
    if (!std::filesystem::exists(path)) return path;
    const auto parent=path.parent_path();const auto stem=path_to_utf8_string(path.stem());const auto extension=path_to_utf8_string(path.extension());
    for(std::size_t index=2;;++index){auto candidate=parent/path_from_utf8_string(stem+" ("+std::to_string(index)+")"+extension);if(!std::filesystem::exists(candidate))return candidate;}
}

ExecutionResult save_text_like(const ActionInstance& action, ExecutionContext& context, std::string text, ContentKind clipboard_kind,
                               std::string_view extension) {
    auto result = result_for(action, context, ExecutionStatus::Completed, "saved");
    const auto output = available_generated_path(target_dir_for(action, context) /
                                                 path_from_utf8_string(default_filename_for(action, extension)));
    write_bytes(output, bytes_from_string(text));
    record_output_path(context, output, result);
    if (clipboard_kind != ContentKind::Unknown) {
        (void)clipboard_kind;
    }
    return result;
}

std::string resume_json_for(const std::string& text) {
    const auto resume = detect_resume_fields(text);
    std::ostringstream out;
    out << "{\n  \"is_resume\": " << (resume.is_resume ? "true" : "false") << ",\n  \"fields\": [";
    for (std::size_t index = 0; index < resume.fields.size(); ++index) {
        const auto& field = resume.fields[index];
        if (index != 0) {
            out << ",";
        }
        out << "\n    {\"kind\": \"" << json_escape(field.kind) << "\", \"value\": \"" << json_escape(field.value)
            << "\", \"source_range\": ["
            << field.source_range.first << ", " << field.source_range.second << "]}";
    }
    if (!resume.fields.empty()) {
        out << "\n  ";
    }
    out << "]\n}\n";
    return out.str();
}

ExecutionResult copy_or_move_path(const ActionInstance& action, ExecutionContext& context, bool move) {
    auto result = result_for(action, context, ExecutionStatus::Completed, move ? "moved path" : "copied path");
    const auto source_text = context.clipboard_store.read_text(action.source_ref);
    auto sources = paths_from_uri_list(source_text);
    if (sources.empty()) {
        const auto first = source_text.find_first_not_of(" \t\r\n");
        const auto last = source_text.find_last_not_of(" \t\r\n");
        if (first != std::string::npos) {
            sources.push_back(path_from_utf8_string(source_text.substr(first, last - first + 1)));
        }
    }
    if (sources.empty()) {
        return result_for(action, context, ExecutionStatus::Failed, "source path is empty");
    }

    const auto target_directory = target_dir_for(action, context);
    std::vector<std::filesystem::path> outputs;
    const bool single_source = sources.size() == 1;
    const bool confirmed_name = action.parameters.contains("confirmed_destination");
    for (const auto& source : sources) {
        if (source.filename().empty()) {
            return result_for(action, context, ExecutionStatus::Failed, "source path has no filename");
        }
        const auto requested_name = confirmed_name && single_source && !action.filename.empty()
            ? path_from_utf8_string(action.filename)
            : source.filename();
        auto target = target_directory / requested_name;
        std::error_code error;
        if (std::filesystem::equivalent(source, target, error) && !error) {
            return result_for(action, context, ExecutionStatus::Failed, "source and target are the same path");
        }
        error.clear();
        target = available_generated_path(target);
        if (move) {
            std::filesystem::rename(source, target, error);
            if (error) {
                error.clear();
                std::filesystem::copy(source, target,
                                      std::filesystem::copy_options::recursive |
                                          std::filesystem::copy_options::none,
                                      error);
                if (!error) {
                    std::filesystem::remove_all(source, error);
                }
            }
        } else if (std::filesystem::is_directory(source, error)) {
            error.clear();
            std::filesystem::copy(source, target,
                                  std::filesystem::copy_options::recursive |
                                      std::filesystem::copy_options::none,
                                  error);
        } else if (!error) {
            std::filesystem::copy_file(source, target, std::filesystem::copy_options::none, error);
        }
        if (error) {
            return result_for(action, context, ExecutionStatus::Failed, error.message());
        }
        outputs.push_back(target);
        context.path_history.observe(path_to_utf8_string(target), "pastit", context.now_ms);
    }

    std::ostringstream copied_paths;
    for (std::size_t index = 0; index < outputs.size(); ++index) {
        if (index != 0) {
            copied_paths << '\n';
        }
        copied_paths << path_to_utf8_string(outputs[index]);
    }
    result.output_path = outputs.back();
    result.output_paths = outputs;
    result.output_clipboard_ref = record_text_clipboard(context, copied_paths.str(), ContentKind::Path);
    return result;
}

}  // namespace

ExecutionResult execute_file_action(const ActionInstance& action, ExecutionContext& context) {
    try {
        switch (action.kind) {
            case ActionKind::PasteText:
            case ActionKind::CopyPath: {
                auto result = result_for(action, context, ExecutionStatus::Completed, "copied text");
                const auto text = context.clipboard_store.read_text(action.source_ref);
                result.output_clipboard_ref = record_text_clipboard(context, text, ContentKind::Text);
                return result;
            }
            case ActionKind::SaveTextFile:
                return save_text_like(action, context, context.clipboard_store.read_text(action.source_ref), ContentKind::Text, "txt");
            case ActionKind::PasteImage: {
                auto result = result_for(action, context, ExecutionStatus::Completed, "copied image");
                const auto image = context.clipboard_store.read(action.source_ref);
                const auto source_item = context.clipboard_store.item(action.source_ref);
                const auto mime_types = source_item.has_value() && !source_item->mime_types.empty()
                                            ? source_item->mime_types
                                            : std::vector<std::string>{"image/png"};
                const auto item = context.clipboard_store.put(ClipboardData{
                    .mime_types = mime_types,
                    .bytes = image,
                    .kind = ContentKind::Image,
                    .source_app = "pastit",
                    .captured_at_ms = context.now_ms,
                });
                result.output_clipboard_ref = item.ref;
                return result;
            }
            case ActionKind::SaveImageFile: {
                auto result = result_for(action, context, ExecutionStatus::Completed, "saved image");
                const auto output = available_generated_path(target_dir_for(action, context) /
                                                             path_from_utf8_string(default_filename_for(action, "bin")));
                write_bytes(output, context.clipboard_store.read(action.source_ref));
                record_output_path(context, output, result);
                return result;
            }
            case ActionKind::CopyTemporaryImagePath: {
                auto result = result_for(action, context, ExecutionStatus::Completed, "copied temporary image path");
                const auto output = available_generated_path(
                    target_dir_for(action, context) /
                    default_filename_for(action, ".bin"));
                write_bytes(output, context.clipboard_store.read(action.source_ref));
                record_output_path(context, output, result);
                return result;
            }
            case ActionKind::CopyPathToDirectory:
                return copy_or_move_path(action, context, false);
            case ActionKind::MovePath:
                return copy_or_move_path(action, context, true);
            case ActionKind::PrettyJson: {
                const auto pretty = pretty_json(context.clipboard_store.read_text(action.source_ref));
                if (!pretty.has_value()) {
                    return result_for(action, context, ExecutionStatus::Failed, "invalid JSON");
                }
                auto result = result_for(action, context, ExecutionStatus::Completed, "copied pretty JSON");
                result.output_clipboard_ref = record_text_clipboard(context, *pretty, ContentKind::Json);
                return result;
            }
            case ActionKind::SaveJsonFile:
                return save_text_like(action, context, context.clipboard_store.read_text(action.source_ref), ContentKind::Json, "json");
            case ActionKind::SaveJsonPrettyFile: {
                const auto pretty = pretty_json(context.clipboard_store.read_text(action.source_ref));
                if (!pretty.has_value()) {
                    return result_for(action, context, ExecutionStatus::Failed, "invalid JSON");
                }
                return save_text_like(action, context, *pretty, ContentKind::Json, "json");
            }
            case ActionKind::CopyResumeField: {
                const auto found = action.parameters.find("value");
                if (found == action.parameters.end()) {
                    return result_for(action, context, ExecutionStatus::Failed, "resume field value missing");
                }
                auto result = result_for(action, context, ExecutionStatus::Completed, "copied resume field");
                result.output_clipboard_ref = record_text_clipboard(context, found->second, ContentKind::Text);
                return result;
            }
            case ActionKind::CopyResumeAsJson: {
                auto result = result_for(action, context, ExecutionStatus::Completed, "copied resume JSON");
                result.output_clipboard_ref = record_text_clipboard(context, resume_json_for(context.clipboard_store.read_text(action.source_ref)),
                                                                    ContentKind::Json);
                return result;
            }
            case ActionKind::SaveResumeFile:
                return save_text_like(action, context, resume_json_for(context.clipboard_store.read_text(action.source_ref)), ContentKind::Json, "json");
            default:
                return result_for(action, context, ExecutionStatus::Unsupported, "unsupported action");
        }
    } catch (const std::exception& error) {
        return result_for(action, context, ExecutionStatus::Failed, error.what());
    }
}

}  // namespace pastit
