#include "actions/action_catalog.hpp"

#include "ai/mermaid_prompt.hpp"
#include "ai/prompt_expander.hpp"
#include "detect/fast_content_detector.hpp"
#include "detect/resume_detector.hpp"
#include "graph/graph_data.hpp"
#include "storage/path_history.hpp"
#include "util/json.hpp"
#include "util/path_utf8.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <regex>
#include <set>
#include <sstream>

namespace pastit {
namespace {

constexpr std::size_t kMaxPathTargets = 3;
constexpr std::size_t kMaxStructuredCandidateBytes = 2048;
constexpr std::string_view kExplainTextTemplateId = "builtin-explain-text";
constexpr std::string_view kExplainCodeTemplateId = "builtin-explain-code";

struct GithubRepo {
    std::string owner;
    std::string repo;
    std::string https_url;
    std::string ssh_url;
};

std::string sanitize_ref(std::string value) {
    for (auto& ch : value) {
        if (!std::isalnum(static_cast<unsigned char>(ch))) {
            ch = '_';
        }
    }
    return value;
}

std::string kind_slug(ActionKind kind) {
    switch (kind) {
        case ActionKind::PasteText:
            return "paste_text";
        case ActionKind::SaveTextFile:
            return "save_text_file";
        case ActionKind::PasteImage:
            return "paste_image";
        case ActionKind::SaveImageFile:
            return "save_image_file";
        case ActionKind::CopyTemporaryImagePath:
            return "copy_temporary_image_path";
        case ActionKind::PasteUrl:
            return "paste_url";
        case ActionKind::OpenUrl:
            return "open_url";
        case ActionKind::DownloadUrl:
            return "download_url";
        case ActionKind::SaveUrlFile:
            return "save_url_file";
        case ActionKind::PasteEmail:
            return "paste_email";
        case ActionKind::SaveEmailFile:
            return "save_email_file";
        case ActionKind::ComposeEmail:
            return "compose_email";
        case ActionKind::SendEmail:
            return "send_email";
        case ActionKind::CopyPath:
            return "copy_path";
        case ActionKind::MovePath:
            return "move_path";
        case ActionKind::CopyPathToDirectory:
            return "copy_path_to_directory";
        case ActionKind::PrettyJson:
            return "pretty_json";
        case ActionKind::SaveJsonFile:
            return "save_json_file";
        case ActionKind::SaveJsonPrettyFile:
            return "save_json_pretty_file";
        case ActionKind::CopyResumeField:
            return "copy_resume_field";
        case ActionKind::CopyResumeAsJson:
            return "copy_resume_as_json";
        case ActionKind::SaveResumeFile:
            return "save_resume_file";
        case ActionKind::TransformText:
            return "transform_text";
        case ActionKind::ExtractContactInfo:
            return "extract_contact";
        case ActionKind::CopyContactAsJson:
            return "copy_contact_as_json";
        case ActionKind::SaveContactAsJson:
            return "save_contact_as_json";
        case ActionKind::CopyContactField:
            return "copy_contact_field";
        case ActionKind::OpenTerminalAtPath:
            return "open_terminal_at_path";
        case ActionKind::PingIp:
            return "ping_ip";
        case ActionKind::TraceRouteIp:
            return "trace_route_ip";
        case ActionKind::ReverseDnsIp:
            return "reverse_dns_ip";
        case ActionKind::DigIp:
            return "dig_ip";
        case ActionKind::NetworkDiagnosticReport:
            return "network_diagnostic_report";
        case ActionKind::CloneGithubHttps:
            return "clone_github_https";
        case ActionKind::CloneGithubSsh:
            return "clone_github_ssh";
        case ActionKind::CopyGithubHttpsUrl:
            return "copy_github_https_url";
        case ActionKind::CopyGithubSshUrl:
            return "copy_github_ssh_url";
        case ActionKind::ConvertTimezone:
            return "convert_timezone";
        case ActionKind::ToUnixTimestamp:
            return "to_unix_timestamp";
        case ActionKind::CopyNormalizedDateTime:
            return "copy_normalized_date_time";
        case ActionKind::DrawMermaidDiagram:
            return "draw_mermaid_diagram";
        case ActionKind::GenerateQr:
            return "generate_qr";
        case ActionKind::HashSha256:
            return "hash_sha256";
        case ActionKind::HashSha512:
            return "hash_sha512";
        case ActionKind::AnnotateImage:
            return "annotate_image";
        case ActionKind::ExplainText:
            return "explain_text";
        case ActionKind::ExplainCode:
            return "explain_code";
        case ActionKind::Graph:
            return "graph";
        case ActionKind::CustomPrompt:
            return "custom_prompt";
    }
    return "action";
}

std::string action_slug(const ActionInstance& action) {
    if (action.kind == ActionKind::TransformText) {
        const auto template_id = action.parameters.find("template_id");
        if (template_id != action.parameters.end()) {
            if (template_id->second == kExplainTextTemplateId) {
                return "explain_text";
            }
            if (template_id->second == kExplainCodeTemplateId) {
                return "explain_code";
            }
        }
    }
    return kind_slug(action.kind);
}

std::string default_filename(const ClipboardItem& item, std::string_view representation) {
    switch (item.kind) {
        case ContentKind::Image:
            for (const auto& mime : item.mime_types) {
                if (mime == "image/png") {
                    return "clipboard-image.png";
                }
                if (mime == "image/jpeg" || mime == "image/jpg") {
                    return "clipboard-image.jpg";
                }
            }
            return "clipboard-image.bin";
        case ContentKind::Url:
            return representation == "download" ? "download.bin" : "clipboard.url";
        case ContentKind::Email:
            return "clipboard.eml";
        case ContentKind::Json:
            return representation == "pretty" ? "clipboard-pretty.json" : "clipboard.json";
        case ContentKind::Text:
        case ContentKind::DateTime:
        case ContentKind::Path:
        case ContentKind::Unknown:
            return "clipboard.txt";
    }
    return "clipboard.dat";
}

std::string trim_copy(std::string_view input) {
    auto begin = input.begin();
    auto end = input.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(begin, end);
}

bool valid_ipv4_value(std::string_view value) {
    int octets = 0;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto dot = value.find('.', start);
        const auto end = dot == std::string_view::npos ? value.size() : dot;
        if (end == start || ++octets > 4) {
            return false;
        }
        int octet = 0;
        for (const unsigned char ch : value.substr(start, end - start)) {
            if (!std::isdigit(ch)) {
                return false;
            }
            octet = octet * 10 + static_cast<int>(ch - '0');
        }
        if (octet > 255) {
            return false;
        }
        if (dot == std::string_view::npos) {
            break;
        }
        start = dot + 1;
    }
    return octets == 4;
}

std::string strip_ip_token_punctuation(std::string_view token) {
    while (!token.empty() && std::string_view{"[](){}<>,;\"'"}.find(token.front()) != std::string_view::npos) {
        token.remove_prefix(1);
    }
    while (!token.empty() && std::string_view{"[](){}<>,;.\"'"}.find(token.back()) != std::string_view::npos) {
        token.remove_suffix(1);
    }
    return std::string{token};
}

std::optional<std::string> ip_value_from(std::string_view source_text) {
    static const std::regex ipv4_pattern(R"((^|[^0-9.])((?:[0-9]{1,3}\.){3}[0-9]{1,3})(?=$|[^0-9.]))");
    const std::string value{source_text};
    std::set<std::string> candidates;
    for (std::sregex_iterator it(value.begin(), value.end(), ipv4_pattern), end; it != end; ++it) {
        const auto candidate = (*it)[2].str();
        if (valid_ipv4_value(candidate)) {
            candidates.insert(candidate);
        }
    }

    std::istringstream input(value);
    std::string token;
    while (input >> token) {
        auto candidate = strip_ip_token_punctuation(token);
        if (candidate.find(':') != std::string::npos &&
            detect_fast_content(ContentKind::Text, candidate).ip) {
            candidates.insert(candidate);
        }
    }
    if (candidates.size() != 1) return std::nullopt;
    return *candidates.begin();
}

std::string join_strings(const std::vector<std::string>& values, std::string_view separator) {
    std::ostringstream out;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            out << separator;
        }
        out << values[index];
    }
    return out.str();
}

void add(ActionCatalog& catalog, const ClipboardItem& item, ActionKind kind, std::string target_ref,
         std::string label, std::string description, std::string representation,
         std::map<std::string, std::string> parameters = {}, bool enabled = true) {
    ActionInstance action;
    action.kind = kind;
    action.source_ref = item.ref;
    action.target_ref = std::move(target_ref);
    action.representation = std::move(representation);
    action.filename = default_filename(item, action.representation);
    action.label = std::move(label);
    action.description = std::move(description);
    action.parameters = std::move(parameters);
    action.enabled = enabled;
    action.id = "a_" + action_slug(action) + "_" + sanitize_ref(item.ref);
    if (!action.target_ref.empty()) {
        action.id += "_" + sanitize_ref(action.target_ref);
    }
    if (action.kind == ActionKind::CopyResumeField) {
        action.id += "_" + sanitize_ref(action.parameters["field_kind"]);
    }
    if (action.kind == ActionKind::CopyContactField) {
        action.id += "_" + sanitize_ref(action.parameters["field_key"]);
    }
    if (action.kind == ActionKind::TransformText) {
        const auto template_id = action.parameters.find("template_id");
        if (template_id != action.parameters.end() && template_id->second != kExplainTextTemplateId &&
            template_id->second != kExplainCodeTemplateId) {
            action.id += "_" + sanitize_ref(template_id->second);
        }
    }
    catalog.actions.push_back(std::move(action));
}

std::vector<PathLocation> path_targets(const DecisionSnapshot& snapshot) {
    std::vector<PathLocation> out;
    for (const auto& path : snapshot.recent_paths) {
        if (path.kind == PathKind::Directory && path.exists) {
            out.push_back(path);
        }
    }
    std::sort(out.begin(), out.end(), [](const PathLocation& left, const PathLocation& right) {
        return left.last_seen_ms > right.last_seen_ms;
    });
    if (out.size() > kMaxPathTargets) {
        out.resize(kMaxPathTargets);
    }
    if (out.empty()) {
        PathLocation temp;
        temp.ref = "temp";
        temp.path = std::filesystem::temp_directory_path();
        temp.kind = PathKind::Directory;
        temp.exists = true;
        out.push_back(temp);
    }
    return out;
}

std::string source_text_for_detection(const ClipboardItem& item) {
    if (!item.blob_path.empty()) {
        std::ifstream input(item.blob_path, std::ios::binary);
        if (input) {
            return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        }
    }
    return item.preview;
}

std::string contact_json(const std::vector<ContactField>& fields) {
    std::ostringstream out;
    out << "{\"fields\":[";
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        const auto& field = fields[index];
        out << "{\"kind\":" << json_quote(field.kind) << ",\"label\":" << json_quote(field.label)
            << ",\"value\":" << json_quote(field.value) << '}';
    }
    out << "]}";
    return out.str();
}

void add_resume_actions(ActionCatalog& catalog, const ClipboardItem& item, const std::vector<PathLocation>& targets,
                        const std::string& source_text) {
    const auto resume = detect_resume_fields(source_text);
    if (!resume.is_resume) {
        return;
    }
    for (const auto& field : resume.fields) {
        add(catalog, item, ActionKind::CopyResumeField, "", "Copy " + field.kind,
            "Copy extracted resume " + field.kind + " directly to the clipboard", "field",
            {{"field_kind", field.kind}, {"value", field.value}});
    }
    add(catalog, item, ActionKind::CopyResumeAsJson, "", "Copy resume JSON",
        "Copy all locally extracted resume fields as JSON", "resume_json");
    for (const auto& target : targets) {
        add(catalog, item, ActionKind::SaveResumeFile, target.ref, "Save resume JSON",
            "Save locally extracted resume fields as JSON to " + path_to_utf8_string(target.path), "resume_json");
    }
}

void add_contact_actions(ActionCatalog& catalog, const ClipboardItem& item, const std::vector<PathLocation>& targets,
                         const FastContentSignals& signals) {
    if (!signals.contact) {
        return;
    }
    const auto json = contact_json(signals.contact_fields);
    add(catalog, item, ActionKind::ExtractContactInfo, "", "Extract contact info",
        "Open a structured table of locally extracted contact fields", "contact",
        {{"contact_json", json}, {"field_count", std::to_string(signals.contact_fields.size())}});
    add(catalog, item, ActionKind::CopyContactAsJson, "", "Copy contact JSON",
        "Copy the locally extracted contact fields as JSON", "contact_json",
        {{"contact_json", json}, {"field_count", std::to_string(signals.contact_fields.size())}});
    for (std::size_t index = 0; index < signals.contact_fields.size(); ++index) {
        const auto& field = signals.contact_fields[index];
        add(catalog, item, ActionKind::CopyContactField, "", "Copy contact " + field.label,
            "Copy the extracted contact " + field.label + " value", "contact_field",
            {{"field_kind", field.kind},
             {"field_label", field.label},
             {"field_value", field.value},
             {"field_key", field.kind + "_" + std::to_string(index)}});
    }
    for (const auto& target : targets) {
        add(catalog, item, ActionKind::SaveContactAsJson, target.ref, "Save contact JSON",
            "Save locally extracted contact fields as JSON to " + path_to_utf8_string(target.path), "contact_json",
            {{"contact_json", json}, {"field_count", std::to_string(signals.contact_fields.size())}});
    }
}

void add_path_fast_actions(ActionCatalog& catalog, const ClipboardItem& item, const std::string& source_text) {
    const auto text = trim_copy(source_text);
    if (text.empty()) {
        return;
    }
    const auto uri_paths = paths_from_uri_list(text);
    if (uri_paths.size() > 1 || (uri_paths.empty() &&
        (text.find_first_of("\r\n") != std::string::npos || text.rfind("file://", 0) == 0))) {
        return;
    }
    const auto path = uri_paths.empty() ? path_from_utf8_string(text) : uri_paths.front();
    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    if (ec || !std::filesystem::exists(status)) {
        return;
    }
    const bool regular_file = !ec && std::filesystem::is_regular_file(status);
    if (!regular_file && !std::filesystem::is_directory(status)) {
        return;
    }
    const auto working_directory = regular_file ? path.parent_path() : path;
    add(catalog, item, ActionKind::OpenTerminalAtPath, "", "Open terminal here",
        "Open a terminal with its working directory set to this path location", "terminal",
        {{"path", path_to_utf8_string(path)}, {"working_directory", path_to_utf8_string(working_directory)}});
    if (regular_file) {
        add(catalog, item, ActionKind::HashSha256, "", "Hash SHA-256",
            "Calculate the SHA-256 digest for this file path", "hash",
            {{"path", path_to_utf8_string(path)}, {"algorithm", "SHA-256"}});
        add(catalog, item, ActionKind::HashSha512, "", "Hash SHA-512",
            "Calculate the SHA-512 digest for this file path", "hash",
            {{"path", path_to_utf8_string(path)}, {"algorithm", "SHA-512"}});
    }
}

void add_ip_actions(ActionCatalog& catalog, const ClipboardItem& item, const std::string& source_text) {
    if (source_text.size() > kMaxStructuredCandidateBytes) return;
    const auto detected_ip = ip_value_from(source_text);
    if (!detected_ip) return;
    const auto& ip = *detected_ip;
    const std::map<std::string, std::string> parameters{{"ip", ip}};
    add(catalog, item, ActionKind::PingIp, "", "Ping IP",
        "Measure reachability and latency for this IP address", "network", parameters);
    add(catalog, item, ActionKind::TraceRouteIp, "", "Trace route IP",
        "Show the network path toward this IP address", "network", parameters);
    add(catalog, item, ActionKind::ReverseDnsIp, "", "Reverse DNS IP",
        "Look up host names associated with this IP address", "network", parameters);
    add(catalog, item, ActionKind::DigIp, "", "DNS lookup IP",
        "Look up DNS records relevant to this IP address", "network", parameters);
    add(catalog, item, ActionKind::NetworkDiagnosticReport, "", "Network diagnostic report",
        "Run the applicable network checks and combine the results into a report", "network_report", parameters);
}

std::optional<GithubRepo> github_repo_from(std::string_view source_text) {
    static const std::regex pattern(
        R"(https://github\.com/([A-Za-z0-9][A-Za-z0-9-]{0,38})/([A-Za-z0-9._-]+)(?:[?#][^\s]*)?(?=$|[\s)\]>},.;]))",
        std::regex_constants::icase);
    const std::string value{source_text};
    std::smatch match;
    if (!std::regex_search(value, match, pattern)) {
        return std::nullopt;
    }
    GithubRepo repo;
    repo.owner = match[1].str();
    repo.repo = match[2].str();
    if (repo.repo.size() > 4 && repo.repo.ends_with(".git")) {
        repo.repo.resize(repo.repo.size() - 4);
    }
    repo.https_url = "https://github.com/" + repo.owner + "/" + repo.repo + ".git";
    repo.ssh_url = "git@github.com:" + repo.owner + "/" + repo.repo + ".git";
    return repo;
}

void add_github_actions(ActionCatalog& catalog, const ClipboardItem& item, const std::vector<PathLocation>& targets,
                        const FastContentSignals& signals, const std::string& source_text) {
    if (!signals.github_url) {
        return;
    }
    const auto repo = github_repo_from(source_text);
    if (!repo) {
        return;
    }
    const std::map<std::string, std::string> repo_parameters{
        {"github_owner", repo->owner}, {"github_repo", repo->repo},
        {"https_url", repo->https_url}, {"ssh_url", repo->ssh_url},
    };
    for (const auto& target : targets) {
        auto parameters = repo_parameters;
        parameters["destination_ref"] = target.ref;
        parameters["destination_directory"] = path_to_utf8_string(target.path);
        add(catalog, item, ActionKind::CloneGithubHttps, target.ref, "Clone GitHub HTTPS",
            "Clone this GitHub repository into the selected destination using its HTTPS remote URL", "git_clone",
            parameters);
        add(catalog, item, ActionKind::CloneGithubSsh, target.ref, "Clone GitHub SSH",
            "Clone this GitHub repository into the selected destination using its SSH remote URL", "git_clone",
            parameters);
    }
    add(catalog, item, ActionKind::CopyGithubHttpsUrl, "", "Copy GitHub HTTPS URL",
        "Copy the normalized GitHub HTTPS remote URL", "git_url", repo_parameters);
    add(catalog, item, ActionKind::CopyGithubSshUrl, "", "Copy GitHub SSH URL",
        "Copy the normalized GitHub SSH remote URL", "git_url", repo_parameters);
}

void add_datetime_actions(ActionCatalog& catalog, const ClipboardItem& item, const FastContentSignals& signals,
                          const std::string& source_text) {
    if (source_text.size() > kMaxStructuredCandidateBytes || !signals.date_time_value) return;
    const auto& value = *signals.date_time_value;
    std::map<std::string, std::string> parameters{
        {"original", value.original},
        {"normalized", value.normalized.empty() ? value.original : value.normalized},
        {"source_zone", value.source_zone},
        {"has_epoch", value.has_epoch ? "true" : "false"},
        {"epoch_seconds", std::to_string(value.epoch_seconds)},
    };
    auto timezone_parameters = parameters;
    timezone_parameters["target_zone"] = "UTC";
    add(catalog, item, ActionKind::ConvertTimezone, "", "Convert timezone",
        "Open timezone conversion controls for this date and time", "datetime", timezone_parameters);
    auto timestamp_parameters = parameters;
    timestamp_parameters["timestamp_units"] = "seconds,milliseconds";
    add(catalog, item, ActionKind::ToUnixTimestamp, "", "To Unix timestamp",
        "Convert this date and time to a Unix timestamp", "datetime", timestamp_parameters);
    add(catalog, item, ActionKind::CopyNormalizedDateTime, "", "Copy normalized date/time",
        "Copy this date and time in normalized form", "datetime", parameters);
}

bool qr_supported_kind(ContentKind kind) {
    return kind == ContentKind::Text || kind == ContentKind::Url || kind == ContentKind::Email ||
           kind == ContentKind::DateTime;
}

void add_qr_action(ActionCatalog& catalog, const ClipboardItem& item, const FastContentSignals& signals,
                   const std::string& source_text) {
    if (!signals.qr || !qr_supported_kind(item.kind)) {
        return;
    }
    const auto payload = trim_copy(source_text);
    add(catalog, item, ActionKind::GenerateQr, "", "Generate QR",
        "Generate a QR payload preview for this clipboard value", "qr",
        {{"payload", payload}, {"payload_bytes", std::to_string(payload.size())}});
}

void add_diagram_action(ActionCatalog& catalog, const ClipboardItem& item, const FastContentSignals& signals,
                        std::string_view source_text) {
    if (!signals.diagram) {
        return;
    }
    if (has_supported_mermaid_header(source_text)) {
        add(catalog, item, ActionKind::DrawMermaidDiagram, "", "Render Mermaid source",
            "Render this clipboard Mermaid source directly", "mermaid", {{"mermaid_source", "direct"}});
    } else {
        add(catalog, item, ActionKind::DrawMermaidDiagram, "", "Draw Mermaid diagram",
            "Generate Mermaid diagram source from this relationship-like text", "mermaid");
    }
}

void add_prompt_actions(ActionCatalog& catalog, const ClipboardItem& item, const std::vector<PromptTemplate>& templates,
                        const ProviderSettings& provider, const FastContentSignals& signals) {
    for (const auto& prompt : templates) {
        if (!prompt.enabled) continue;
        auto variables = prompt_variable_names(prompt.system_prompt);
        std::map<std::string, std::string> parameters{
            {"template_id", prompt.id},
            {"template_name", prompt.name},
            {"system_prompt", prompt.system_prompt},
            {"temperature", std::to_string(prompt.temperature)},
            {"llm_endpoint", provider.endpoint},
            {"llm_model_id", provider.model_id},
            {"input_variable", "text"},
            {"variable_names", join_strings(variables, ",")},
            {"content_signal", signals.code ? "code" : "text"},
        };
        add(catalog, item, ActionKind::TransformText, "", prompt.name,
            "Generate an AI result using the " + prompt.name + " prompt", "ai", std::move(parameters));
    }
}

}  // namespace

std::optional<ActionInstance> ActionCatalog::find(const std::string& id) const {
    for (const auto& action : actions) {
        if (action.id == id) {
            return action;
        }
    }
    return std::nullopt;
}

std::optional<ActionInstance> ActionCatalog::find_by_kind_and_label(ActionKind kind, const std::string& label) const {
    for (const auto& action : actions) {
        if (action.kind == kind && action.label == label) {
            return action;
        }
    }
    return std::nullopt;
}

std::string action_kind_slug(ActionKind kind) {
    return kind_slug(kind);
}

ActionCatalog build_catalog(const DecisionSnapshot& snapshot) {
    return build_catalog(snapshot, {}, {});
}

ActionCatalog build_catalog(const DecisionSnapshot& snapshot, const std::vector<PromptTemplate>& templates,
                            const ProviderSettings& provider) {
    ActionCatalog catalog;
    const auto targets = path_targets(snapshot);

    if (snapshot.clipboard_items.empty()) {
        return catalog;
    }

    const auto& item = snapshot.clipboard_items.front();
    const auto source_text = item.kind == ContentKind::Image ? std::string{} : source_text_for_detection(item);
    const auto signals = detect_fast_content(item.kind, source_text);
    switch (item.kind) {
            case ContentKind::Text:
            case ContentKind::DateTime:
                add(catalog, item, ActionKind::PasteText, "", "Paste text", "Paste clipboard text into the focused target", "raw");
                for (const auto& target : targets) {
                    add(catalog, item, ActionKind::SaveTextFile, target.ref, "Save text",
                        "Save clipboard text to " + path_to_utf8_string(target.path), "raw");
                }
                add_contact_actions(catalog, item, targets, signals);
                add_resume_actions(catalog, item, targets, source_text);
                add_ip_actions(catalog, item, source_text);
                add_github_actions(catalog, item, targets, signals, source_text);
                add_datetime_actions(catalog, item, signals, source_text);
                add_diagram_action(catalog, item, signals, source_text);
                if (parse_graph_data(source_text, false, false).has_value() ||
                    parse_graph_data(source_text, true, false).has_value()) {
                    add(catalog, item, ActionKind::Graph, "", "Graph data",
                        "Preview this numeric sequence as a line, bar, or pie chart", "graph");
                }
                add_qr_action(catalog, item, signals, source_text);
                add_prompt_actions(catalog, item, templates, provider, signals);
                add(catalog, item, ActionKind::CustomPrompt, "", "Custom prompt",
                    "Enter a prompt and send this clipboard text to the configured general LLM", "ai");
                break;
            case ContentKind::Image:
                add(catalog, item, ActionKind::PasteImage, "", "Paste image", "Paste the image clipboard item", "raw");
                add(catalog, item, ActionKind::CopyTemporaryImagePath, "temp", "Copy temporary image path",
                    "Write the original image bytes to a temporary file and copy its path", "temporary");
                for (const auto& target : targets) {
                    add(catalog, item, ActionKind::SaveImageFile, target.ref, "Save image",
                    "Save original image bytes to " + path_to_utf8_string(target.path), "raw");
                }
                add(catalog, item, ActionKind::AnnotateImage, "", "Annotate image",
                    "Open the image annotation tools for this clipboard image", "annotation");
                break;
            case ContentKind::Url:
                add(catalog, item, ActionKind::PasteUrl, "", "Paste URL", "Paste the URL text", "raw");
                add(catalog, item, ActionKind::OpenUrl, "", "Open URL", "Open the URL with the desktop browser", "raw");
                for (const auto& target : targets) {
                    add(catalog, item, ActionKind::DownloadUrl, target.ref, "Download URL",
                        "Download URL bytes to " + path_to_utf8_string(target.path), "download");
                    add(catalog, item, ActionKind::SaveUrlFile, target.ref, "Save URL file",
                        "Download the URL response bytes to " + path_to_utf8_string(target.path), "download");
                }
                add_github_actions(catalog, item, targets, signals, source_text);
                add_qr_action(catalog, item, signals, source_text);
                break;
            case ContentKind::Email:
                add(catalog, item, ActionKind::PasteEmail, "", "Paste email", "Paste the email address or mailto value", "raw");
                add(catalog, item, ActionKind::ComposeEmail, "", "Compose email", "Open a local compose adapter for this email", "mailto");
                add(catalog, item, ActionKind::SendEmail, "", "Send email", "Send through a configured local mail adapter", "adapter", {}, false);
                for (const auto& target : targets) {
                    add(catalog, item, ActionKind::SaveEmailFile, target.ref, "Save email file",
                        "Save an .eml representation to " + path_to_utf8_string(target.path), "eml");
                }
                add_qr_action(catalog, item, signals, source_text);
                break;
            case ContentKind::Path:
                for (const auto& target : targets) {
                    add(catalog, item, ActionKind::CopyPathToDirectory, target.ref, "Copy to directory",
                        "Copy the path item into " + path_to_utf8_string(target.path), "filesystem");
                    add(catalog, item, ActionKind::MovePath, target.ref, "Move to directory",
                        "Move the path item into " + path_to_utf8_string(target.path), "filesystem");
                }
                add_path_fast_actions(catalog, item, source_text);
                break;
            case ContentKind::Json:
                if (!is_valid_json(source_text)) {
                    add(catalog, item, ActionKind::PasteText, "", "Paste text", "Paste clipboard text into the focused target", "raw");
                    for (const auto& target : targets) {
                        add(catalog, item, ActionKind::SaveTextFile, target.ref, "Save text",
                            "Save clipboard text to " + path_to_utf8_string(target.path), "raw");
                    }
                    break;
                }
                add(catalog, item, ActionKind::PasteText, "", "Paste JSON", "Paste the raw JSON text", "raw");
                add(catalog, item, ActionKind::PrettyJson, "", "Copy pretty JSON", "Copy JSON with two-space indentation", "pretty");
                for (const auto& target : targets) {
                    add(catalog, item, ActionKind::SaveJsonFile, target.ref, "Save JSON",
                        "Save raw JSON to " + path_to_utf8_string(target.path), "raw");
                    add(catalog, item, ActionKind::SaveJsonPrettyFile, target.ref, "Save pretty JSON",
                        "Save pretty JSON to " + path_to_utf8_string(target.path), "pretty");
                }
                break;
            case ContentKind::Unknown:
                break;
    }

    return catalog;
}

}  // namespace pastit
