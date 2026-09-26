#include "executor/utility_executor.hpp"

#include "detect/fast_content_detector.hpp"
#include "transform/text_transforms.hpp"
#include "util/path_utf8.hpp"

#include <optional>
#include <string>

namespace pastit {
namespace {

struct UtilityOutput {
    std::string text;
    ContentKind kind = ContentKind::Text;
    std::string message;
};

ExecutionResult result_for(const ActionInstance& action, const ExecutionContext& context, ExecutionStatus status,
                           std::string message) {
    ExecutionResult result;
    result.request_id = context.request_id;
    result.action_id = action.id;
    result.status = status;
    result.message = std::move(message);
    return result;
}

std::string parameter(const ActionInstance& action, const std::string& key) {
    const auto found = action.parameters.find(key);
    return found == action.parameters.end() ? std::string{} : found->second;
}

std::string trimmed(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return std::string{text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1)};
}

std::optional<UtilityOutput> transform(const ActionInstance& action, const std::string& source) {
    const auto text = trimmed(source);
    const auto wrap = [](std::optional<std::string> value, ContentKind kind, std::string message) -> std::optional<UtilityOutput> {
        if (!value) return std::nullopt;
        return UtilityOutput{std::move(*value), kind, std::move(message)};
    };
    const auto path = path_from_utf8_string(parameter(action, "path"));
    switch (action.kind) {
        case ActionKind::ToUpperCase: return UtilityOutput{to_upper_ascii(source), ContentKind::Text, "copied upper case"};
        case ActionKind::ToLowerCase: return UtilityOutput{to_lower_ascii(source), ContentKind::Text, "copied lower case"};
        case ActionKind::ToTitleCase: return UtilityOutput{to_title_case(source), ContentKind::Text, "copied title case"};
        case ActionKind::TidyWhitespace: return UtilityOutput{tidy_whitespace(source), ContentKind::Text, "copied tidied text"};
        case ActionKind::SortLines: return UtilityOutput{sort_lines(source), ContentKind::Text, "copied sorted lines"};
        case ActionKind::DedupeLines: return UtilityOutput{dedupe_lines(source), ContentKind::Text, "copied unique lines"};
        case ActionKind::TextStatistics: {
            auto stats = text_statistics(source);
            auto summary = stats;
            for (auto& ch : summary) if (ch == '\n') ch = ',';
            return UtilityOutput{std::move(stats), ContentKind::Text, summary};
        }
        case ActionKind::Base64Encode: return UtilityOutput{base64_encode(source), ContentKind::Text, "copied Base64"};
        case ActionKind::Base64Decode: return wrap(base64_decode(text), ContentKind::Text, "copied decoded Base64");
        case ActionKind::UrlEncode: return UtilityOutput{url_encode(text), ContentKind::Text, "copied URL-encoded text"};
        case ActionKind::UrlDecode: return wrap(url_decode(text), ContentKind::Text, "copied URL-decoded text");
        case ActionKind::MinifyJson: return wrap(minify_json(source), ContentKind::Json, "copied minified JSON");
        case ActionKind::JsonToYaml: return wrap(json_to_yaml(source), ContentKind::Text, "copied YAML");
        case ActionKind::JsonToCsv: return wrap(json_to_csv(source), ContentKind::Text, "copied CSV");
        case ActionKind::CopyJsonPaths: return wrap(json_paths(source), ContentKind::Text, "copied JSON paths");
        case ActionKind::CleanUrl: return wrap(clean_tracking_url(text), ContentKind::Url, "copied clean URL");
        case ActionKind::CopyMarkdownLink: return UtilityOutput{markdown_link(text), ContentKind::Text, "copied Markdown link"};
        case ActionKind::CopyPath: return UtilityOutput{path_to_utf8_string(path), ContentKind::Text, "copied path"};
        case ActionKind::CopyFileName: return UtilityOutput{path_to_utf8_string(path.filename()), ContentKind::Text, "copied name"};
        case ActionKind::CopyParentPath: return UtilityOutput{path_to_utf8_string(path.parent_path()), ContentKind::Path, "copied parent folder"};
        case ActionKind::NumberStatistics: return wrap(number_statistics(source), ContentKind::Text, "copied statistics");
        case ActionKind::ToMarkdownTable: return wrap(to_markdown_table(source), ContentKind::Text, "copied Markdown table");
        case ActionKind::CopyColorHex:
        case ActionKind::CopyColorRgb:
        case ActionKind::CopyColorHsl: {
            const auto color = parse_color(text);
            if (!color) return std::nullopt;
            const auto value = action.kind == ActionKind::CopyColorHex ? color_hex(*color)
                             : action.kind == ActionKind::CopyColorRgb ? color_rgb(*color) : color_hsl(*color);
            return UtilityOutput{value, ContentKind::Text, "copied " + value};
        }
        case ActionKind::DecodeJwt: return wrap(decode_jwt(text), ContentKind::Text, "copied decoded JWT");
        case ActionKind::GenerateUuid: return UtilityOutput{generate_uuid_v4(), ContentKind::Text, "copied new UUID"};
        case ActionKind::CopyContactVCard: {
            const auto signals = detect_fast_content(ContentKind::Text, source);
            if (!signals.contact) return std::nullopt;
            std::vector<std::pair<std::string, std::string>> fields;
            for (const auto& field : signals.contact_fields) fields.emplace_back(field.kind, field.value);
            return UtilityOutput{contact_vcard(fields), ContentKind::Text, "copied vCard"};
        }
        default:
            return std::nullopt;
    }
}

}  // namespace

bool is_utility_action(ActionKind kind) {
    switch (kind) {
        case ActionKind::ToUpperCase:
        case ActionKind::ToLowerCase:
        case ActionKind::ToTitleCase:
        case ActionKind::TidyWhitespace:
        case ActionKind::SortLines:
        case ActionKind::DedupeLines:
        case ActionKind::TextStatistics:
        case ActionKind::Base64Encode:
        case ActionKind::Base64Decode:
        case ActionKind::UrlEncode:
        case ActionKind::UrlDecode:
        case ActionKind::MinifyJson:
        case ActionKind::JsonToYaml:
        case ActionKind::JsonToCsv:
        case ActionKind::CopyJsonPaths:
        case ActionKind::CleanUrl:
        case ActionKind::CopyMarkdownLink:
        case ActionKind::CopyPath:
        case ActionKind::OpenPath:
        case ActionKind::RevealPath:
        case ActionKind::CopyFileName:
        case ActionKind::CopyParentPath:
        case ActionKind::NumberStatistics:
        case ActionKind::ToMarkdownTable:
        case ActionKind::CopyColorHex:
        case ActionKind::CopyColorRgb:
        case ActionKind::CopyColorHsl:
        case ActionKind::DecodeJwt:
        case ActionKind::GenerateUuid:
        case ActionKind::CopyContactVCard:
            return true;
        default:
            return false;
    }
}

ExecutionResult execute_utility_action(const ActionInstance& action, ExecutionContext& context) {
    if (action.kind == ActionKind::OpenPath || action.kind == ActionKind::RevealPath) {
        auto path = path_from_utf8_string(parameter(action, "path"));
        if (action.kind == ActionKind::RevealPath) path = path.parent_path();
        const bool ok = !path.empty() && context.open_uri && context.open_uri(path_to_utf8_string(path));
        return result_for(action, context, ok ? ExecutionStatus::Completed : ExecutionStatus::Failed,
                          ok ? "opened " + path_to_utf8_string(path) : "could not open " + path_to_utf8_string(path));
    }
    const bool uses_path = action.kind == ActionKind::CopyPath || action.kind == ActionKind::CopyFileName ||
                           action.kind == ActionKind::CopyParentPath;
    if (uses_path && parameter(action, "path").empty()) {
        return result_for(action, context, ExecutionStatus::Failed, "path missing");
    }
    const auto source = uses_path ? std::string{} : context.clipboard_store.read_text(action.source_ref);
    const auto output = transform(action, source);
    if (!output) {
        return result_for(action, context, ExecutionStatus::Failed, "clipboard content no longer matches this action");
    }
    auto result = result_for(action, context, ExecutionStatus::Completed, output->message);
    const auto item = context.clipboard_store.put(ClipboardData{
        .mime_types = {"text/plain"},
        .bytes = std::vector<std::byte>(reinterpret_cast<const std::byte*>(output->text.data()),
                                        reinterpret_cast<const std::byte*>(output->text.data() + output->text.size())),
        .kind = output->kind,
        .source_app = "pastit",
        .captured_at_ms = context.now_ms,
    });
    result.output_clipboard_ref = item.ref;
    return result;
}

}  // namespace pastit
