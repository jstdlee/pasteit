#include "executor/email_executor.hpp"
#include "util/path_utf8.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace pasteit {
namespace {

std::filesystem::path available_generated_path(std::filesystem::path path) {
    if (!std::filesystem::exists(path)) return path;
    const auto parent=path.parent_path();const auto stem=path_to_utf8_string(path.stem());const auto extension=path_to_utf8_string(path.extension());
    for(std::size_t index=2;;++index){auto candidate=parent/path_from_utf8_string(stem+" ("+std::to_string(index)+")"+extension);if(!std::filesystem::exists(candidate))return candidate;}
}

struct EmailIntent {
    std::string address;
    std::string subject;
    std::string body;
    std::string compose_uri;
};

int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
    if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
    return -1;
}

std::string percent_decode(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '+') {
            decoded.push_back(' ');
        } else if (value[index] == '%' && index + 2 < value.size()) {
            const int high = hex_value(value[index + 1]);
            const int low = hex_value(value[index + 2]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                index += 2;
            } else {
                decoded.push_back(value[index]);
            }
        } else {
            decoded.push_back(value[index]);
        }
    }
    return decoded;
}

std::string lower_ascii(std::string_view value) {
    std::string lower;
    lower.reserve(value.size());
    for (const unsigned char ch : value) {
        lower.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lower;
}

EmailIntent parse_email_intent(std::string raw) {
    EmailIntent intent;
    const bool is_mailto = lower_ascii(raw).rfind("mailto:", 0) == 0;
    intent.compose_uri = is_mailto ? raw : "mailto:" + raw;
    std::string_view value{raw};
    if (is_mailto) {
        value.remove_prefix(7);
    }
    const auto query_start = value.find('?');
    intent.address = percent_decode(value.substr(0, query_start));
    if (query_start == std::string_view::npos) {
        return intent;
    }
    auto query = value.substr(query_start + 1);
    while (!query.empty()) {
        const auto separator = query.find('&');
        const auto field = query.substr(0, separator);
        const auto equals = field.find('=');
        const auto key = lower_ascii(field.substr(0, equals));
        const auto encoded = equals == std::string_view::npos ? std::string_view{} : field.substr(equals + 1);
        if (key == "subject") {
            intent.subject = percent_decode(encoded);
        } else if (key == "body") {
            intent.body = percent_decode(encoded);
        }
        if (separator == std::string_view::npos) {
            break;
        }
        query.remove_prefix(separator + 1);
    }
    return intent;
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
        const auto path = path_from_utf8_string(confirmed->second);
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
        .source_app = "pasteit",
        .captured_at_ms = context.now_ms,
    });
    return item.ref;
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("failed to open email output");
    }
    out << text;
}

}  // namespace

ExecutionResult execute_email_action(const ActionInstance& action, ExecutionContext& context) {
    try {
        const auto email = parse_email_intent(context.clipboard_store.read_text(action.source_ref));
        switch (action.kind) {
            case ActionKind::PasteEmail: {
                auto result = result_for(action, context, ExecutionStatus::Completed, "copied email");
                result.output_clipboard_ref = record_text_clipboard(context, email.address, ContentKind::Email);
                return result;
            }
            case ActionKind::ComposeEmail: {
                auto result = result_for(action, context, ExecutionStatus::Completed, "prepared compose mailto");
                result.output_clipboard_ref = record_text_clipboard(context, email.compose_uri, ContentKind::Email);
                if (context.open_uri && !context.open_uri(email.compose_uri)) {
                    return result_for(action, context, ExecutionStatus::Failed, "failed to open email composer");
                }
                return result;
            }
            case ActionKind::SendEmail:
                if (!context.direct_send_configured || !context.send_email) {
                    return result_for(action, context, ExecutionStatus::Unsupported, "direct send adapter is not configured");
                }
                return result_for(action, context, context.send_email(email.address) ? ExecutionStatus::Completed : ExecutionStatus::Failed,
                                  "sent through configured email adapter");
            case ActionKind::SaveEmailFile: {
                auto result = result_for(action, context, ExecutionStatus::Completed, "saved email");
                const auto filename = action.filename.empty() ? "clipboard.eml" : action.filename;
                const auto output = available_generated_path(target_dir_for(action, context) /
                                                             path_from_utf8_string(filename));
                std::ostringstream eml;
                eml << "To: " << email.address << "\n";
                eml << "Subject: " << email.subject << "\n";
                eml << "X-PasteIt-Representation: clipboard-email\n\n";
                if (!email.body.empty()) {
                    eml << email.body << "\n";
                } else {
                    eml << email.address << "\n";
                }
                write_text(output, eml.str());
                result.output_path = output;
                result.output_paths.push_back(output);
                result.output_clipboard_ref = record_text_clipboard(context, path_to_utf8_string(output), ContentKind::Path);
                context.path_history.observe(path_to_utf8_string(output), "pasteit", context.now_ms);
                return result;
            }
            default:
                return result_for(action, context, ExecutionStatus::Unsupported, "unsupported email action");
        }
    } catch (const std::exception& error) {
        return result_for(action, context, ExecutionStatus::Failed, error.what());
    }
}

}  // namespace pasteit
