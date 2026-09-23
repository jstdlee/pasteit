#include "actions/action_catalog.hpp"
#include "config/prompt_template_service.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

pastit::ClipboardItem item(std::string ref, pastit::ContentKind kind, std::string preview) {
    pastit::ClipboardItem out;
    out.ref = std::move(ref);
    out.kind = kind;
    out.preview = std::move(preview);
    out.size_bytes = out.preview.size();
    out.mime_types = {"text/plain"};
    return out;
}

pastit::PathLocation directory(std::string ref, const std::filesystem::path& path, std::int64_t seen) {
    pastit::PathLocation out;
    out.ref = std::move(ref);
    out.path = path;
    out.kind = pastit::PathKind::Directory;
    out.last_seen_ms = seen;
    out.source = "test";
    out.exists = true;
    return out;
}

pastit::DecisionSnapshot snapshot_with(pastit::ClipboardItem clipboard_item) {
    pastit::DecisionSnapshot snapshot;
    snapshot.clipboard_hash = "hash";
    snapshot.focused_target_hash = "target";
    snapshot.captured_at_ms = 101;
    snapshot.clipboard_items = {std::move(clipboard_item)};
    snapshot.recent_paths = {directory("downloads", std::filesystem::temp_directory_path(), 400)};
    return snapshot;
}

pastit::DecisionSnapshot snapshot_with_items(std::vector<pastit::ClipboardItem> clipboard_items) {
    pastit::DecisionSnapshot snapshot;
    snapshot.clipboard_hash = "hash";
    snapshot.focused_target_hash = "target";
    snapshot.captured_at_ms = 101;
    snapshot.clipboard_items = std::move(clipboard_items);
    snapshot.recent_paths = {directory("downloads", std::filesystem::temp_directory_path(), 400)};
    return snapshot;
}

std::vector<pastit::PromptTemplate> seeded_templates() {
    auto settings = pastit::default_settings();
    return settings.prompt_templates;
}

pastit::ProviderSettings provider() {
    return {.endpoint = "http://127.0.0.1:9999/v1/chat/completions", .model_id = "local-model"};
}

std::optional<pastit::ActionInstance> find_kind(const pastit::ActionCatalog& catalog, pastit::ActionKind kind) {
    for (const auto& action : catalog.actions) {
        if (action.kind == kind) {
            return action;
        }
    }
    return std::nullopt;
}

std::optional<pastit::ActionInstance> find_label(const pastit::ActionCatalog& catalog, std::string_view label) {
    for (const auto& action : catalog.actions) {
        if (action.label == label) {
            return action;
        }
    }
    return std::nullopt;
}

bool has_kind(const pastit::ActionCatalog& catalog, pastit::ActionKind kind) {
    return find_kind(catalog, kind).has_value();
}

bool has_id(const pastit::ActionCatalog& catalog, const std::string& id) {
    return catalog.find(id).has_value();
}

std::vector<pastit::PromptTemplate> without_explain_templates(std::vector<pastit::PromptTemplate> templates) {
    std::erase_if(templates, [](const auto& value) {
        return value.id == "builtin-explain-text" || value.id == "builtin-explain-code";
    });
    return templates;
}

std::vector<pastit::PromptTemplate> disabled_explain_templates(std::vector<pastit::PromptTemplate> templates) {
    for (auto& value : templates) {
        if (value.id == "builtin-explain-text" || value.id == "builtin-explain-code") {
            value.enabled = false;
        }
    }
    return templates;
}

}  // namespace

int main() {
    using namespace pastit;

    const auto templates = seeded_templates();
    const auto llm = provider();

    const auto text_catalog = build_catalog(
        snapshot_with(item("card", ContentKind::Text,
                           "Name: Ada Lovelace\nAda Labs\nada@example.com\n+1 415 555 0100\nA -> B -> C")),
        templates, llm);
    assert(has_kind(text_catalog, ActionKind::ExtractContactInfo));
    assert(has_kind(text_catalog, ActionKind::CopyContactAsJson));
    assert(has_kind(text_catalog, ActionKind::SaveContactAsJson));
    assert(has_kind(text_catalog, ActionKind::CopyContactField));
    assert(has_kind(text_catalog, ActionKind::DrawMermaidDiagram));
    assert(has_kind(text_catalog, ActionKind::GenerateQr));
    assert(has_id(text_catalog, "a_extract_contact_card"));
    assert(has_id(text_catalog, "a_copy_contact_as_json_card"));
    assert(has_id(text_catalog, "a_draw_mermaid_diagram_card"));
    assert(has_id(text_catalog, "a_generate_qr_card"));

    const auto explain_text = find_label(text_catalog, "Explain text");
    assert(explain_text.has_value());
    assert(explain_text->kind == ActionKind::TransformText);
    assert(explain_text->id == "a_explain_text_card");
    assert(explain_text->parameters.at("template_id") == "builtin-explain-text");
    assert(explain_text->parameters.at("template_name") == "Explain text");
    assert(explain_text->parameters.at("system_prompt").find("{text}") != std::string::npos);
    assert(explain_text->parameters.at("llm_endpoint") == llm.endpoint);
    assert(explain_text->parameters.at("llm_model_id") == llm.model_id);
    assert(explain_text->parameters.at("input_variable") == "text");
    assert(explain_text->parameters.contains("variable_names"));

    const auto explain_code = find_label(text_catalog, "Explain code");
    assert(explain_code.has_value());
    assert(explain_code->kind == ActionKind::TransformText);
    assert(explain_code->id == "a_explain_code_card");

    const auto deleted_explain_catalog = build_catalog(
        snapshot_with(item("plain", ContentKind::Text, "Plain text for explanation")),
        without_explain_templates(templates), llm);
    assert(!find_label(deleted_explain_catalog, "Explain text").has_value());
    assert(!find_label(deleted_explain_catalog, "Explain code").has_value());

    const auto disabled_explain_catalog = build_catalog(
        snapshot_with(item("plain", ContentKind::Text, "Plain text for explanation")),
        disabled_explain_templates(templates), llm);
    assert(!find_label(disabled_explain_catalog, "Explain text").has_value());
    assert(!find_label(disabled_explain_catalog, "Explain code").has_value());

    const auto multi_item_catalog = build_catalog(
        snapshot_with_items({
            item("current", ContentKind::Text, "Just the current clipboard text"),
            item("stale", ContentKind::Text, "Name: Stale Person\nstale@example.com\nA -> B -> C"),
        }),
        templates, llm);
    for (const auto& action : multi_item_catalog.actions) {
        assert(action.source_ref == "current");
    }
    assert(!has_kind(multi_item_catalog, ActionKind::ExtractContactInfo));
    assert(!has_kind(multi_item_catalog, ActionKind::DrawMermaidDiagram));
    assert(has_id(multi_item_catalog, "a_paste_text_current"));
    assert(!has_id(multi_item_catalog, "a_paste_text_stale"));

    const auto file_path = std::filesystem::temp_directory_path() / "pastit-action-catalog-hash-input.txt";
    {
        std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
        output << "hash me";
    }
    const auto path_catalog = build_catalog(snapshot_with(item("path", ContentKind::Path, file_path.string())),
                                            templates, llm);
    assert(has_kind(path_catalog, ActionKind::OpenTerminalAtPath));
    assert(has_kind(path_catalog, ActionKind::CopyPathToDirectory));
    assert(has_kind(path_catalog, ActionKind::MovePath));
    const auto sha256 = find_kind(path_catalog, ActionKind::HashSha256);
    const auto sha512 = find_kind(path_catalog, ActionKind::HashSha512);
    assert(sha256.has_value());
    assert(sha512.has_value());
    assert(sha256->parameters.at("algorithm") == "SHA-256");
    assert(sha512->parameters.at("algorithm") == "SHA-512");
    std::filesystem::remove(file_path);

    const auto ip_catalog = build_catalog(snapshot_with(item("ip", ContentKind::Text, "Probe 192.0.2.10 now")),
                                          templates, llm);
    assert(has_kind(ip_catalog, ActionKind::PingIp));
    assert(has_kind(ip_catalog, ActionKind::TraceRouteIp));
    assert(has_kind(ip_catalog, ActionKind::ReverseDnsIp));
    assert(has_kind(ip_catalog, ActionKind::DigIp));
    const auto report = find_kind(ip_catalog, ActionKind::NetworkDiagnosticReport);
    assert(report.has_value());
    assert(report->parameters.at("ip") == "192.0.2.10");

    const auto github_catalog = build_catalog(
        snapshot_with(item("repo", ContentKind::Url, "https://github.com/acme/widget.git?tab=readme#intro")),
        templates, llm);
    assert(has_kind(github_catalog, ActionKind::CloneGithubHttps));
    assert(has_kind(github_catalog, ActionKind::CloneGithubSsh));
    const auto copy_https = find_kind(github_catalog, ActionKind::CopyGithubHttpsUrl);
    const auto copy_ssh = find_kind(github_catalog, ActionKind::CopyGithubSshUrl);
    assert(copy_https.has_value());
    assert(copy_ssh.has_value());
    assert(copy_https->parameters.at("https_url") == "https://github.com/acme/widget.git");
    assert(copy_ssh->parameters.at("ssh_url") == "git@github.com:acme/widget.git");

    const auto date_catalog = build_catalog(
        snapshot_with(item("date", ContentKind::Text, "2026-09-21T08:30:00Z")), templates, llm);
    assert(has_kind(date_catalog, ActionKind::ConvertTimezone));
    assert(has_kind(date_catalog, ActionKind::ToUnixTimestamp));
    const auto normalized = find_kind(date_catalog, ActionKind::CopyNormalizedDateTime);
    assert(normalized.has_value());
    assert(normalized->parameters.at("normalized") == "2026-09-21T08:30:00Z");
    assert(normalized->parameters.at("source_zone") == "UTC");

    auto image = item("image", ContentKind::Image, "preview");
    image.mime_types = {"image/png"};
    const auto image_catalog = build_catalog(snapshot_with(image), templates, llm);
    assert(has_kind(image_catalog, ActionKind::AnnotateImage));
}
