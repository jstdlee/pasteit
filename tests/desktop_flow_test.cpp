#include "ui/desktop_flow.hpp"
#include "decision/fallback_ranker.hpp"
#include "ui/popup.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>

int main() {
    using namespace pastit;

    ActionInstance mermaid_action;
    mermaid_action.kind = ActionKind::DrawMermaidDiagram;
    assert(mermaid_action_requires_generation(mermaid_action));
    mermaid_action.parameters["mermaid_source"] = "direct";
    assert(!mermaid_action_requires_generation(mermaid_action));

    ClipboardItem current_text{
        .ref = "clip_current_text",
        .mime_types = {"text/plain"},
        .kind = ContentKind::Text,
        .preview = "ship only the newest clipboard value",
        .size_bytes = 36,
        .captured_at_ms = 400,
        .source_app = "terminal",
    };
    ClipboardItem older_image{
        .ref = "clip_older_image",
        .mime_types = {"image/png"},
        .kind = ContentKind::Image,
        .preview = "older image",
        .size_bytes = 2048,
        .captured_at_ms = 300,
        .source_app = "browser",
    };
    ClipboardItem older_email{
        .ref = "clip_older_email",
        .mime_types = {"text/plain"},
        .kind = ContentKind::Email,
        .preview = "history@example.test",
        .size_bytes = 20,
        .captured_at_ms = 200,
        .source_app = "mail",
    };
    ClipboardItem older_path{
        .ref = "clip_older_path",
        .mime_types = {"text/plain"},
        .kind = ContentKind::Path,
        .preview = "/tmp/old-source.txt",
        .size_bytes = 19,
        .captured_at_ms = 100,
        .source_app = "files",
    };
    PathLocation target{
        .ref = "path_01",
        .path = std::filesystem::temp_directory_path(),
        .kind = PathKind::Directory,
        .last_seen_ms = 190,
        .source = "focused-app",
        .exists = true,
    };

    DesktopDecisionInput input;
    input.request_id = "req_desktop";
    input.captured_at_ms = 250;
    input.clipboard_items = {older_path, older_email, current_text, older_image};
    input.recent_paths = {target};
    input.focused_target_hash = "focus_hash";
    input.focused_app = "Code";
    input.focused_window_title = "settings.json — Code";
    input.focused_current_directory = target.path;
    input.direct_send_available = true;

    const auto batch = build_desktop_decision(input);
    assert(batch.request.request_id == "req_desktop");
    assert(batch.request.snapshot.clipboard_items.size() == 1);
    assert(batch.request.snapshot.clipboard_items.front().ref == "clip_current_text");
    assert(batch.request.snapshot.focused_app == "Code");
    assert(batch.request.snapshot.focused_window_title == "settings.json — Code");
    assert(batch.request.snapshot.focused_current_directory == target.path.string());
    assert(batch.request.snapshot.focused_target_hash == "focus_hash");
    assert(!batch.request.snapshot.clipboard_hash.empty());
    assert(!batch.catalog.actions.empty());
    assert(batch.request.snapshot.available_actions.size() == batch.catalog.actions.size());

    for (const auto& action : batch.catalog.actions) {
        assert(action.source_ref == "clip_current_text");
        assert(action.kind != ActionKind::PasteImage);
        assert(action.kind != ActionKind::SaveImageFile);
        assert(action.kind != ActionKind::PasteEmail);
        assert(action.kind != ActionKind::CopyPath);
    }
    for (const auto& action : batch.request.snapshot.available_actions) {
        assert(action.source_ref == "clip_current_text");
    }

    const auto fallback = rank_fallback(batch.catalog, batch.request.snapshot, 5);
    assert(!fallback.empty());
    for (const auto& ranked : fallback) {
        assert(ranked.action.source_ref == "clip_current_text");
    }

    const ActionInstance stale_row{
        .id = "stale-image-row",
        .kind = ActionKind::PasteImage,
        .source_ref = "clip_older_image",
        .label = "Paste stale image",
        .description = "Should never render for current text",
        .enabled = true,
    };
    const auto model = build_popup_model(batch.request.snapshot, {
        RankedAction{.action = stale_row, .probability = 0.99, .selected = true, .confidence = 0.0},
        fallback.front(),
    });
    assert(model.rows.size() == 1);
    assert(model.rows.front().action_id == fallback.front().action.id);

    DesktopDecisionInput email_input;
    email_input.request_id = "req_email";
    email_input.captured_at_ms = 300;
    email_input.clipboard_items.push_back(ClipboardItem{
        .ref = "clip_email",
        .mime_types = {"text/plain"},
        .kind = ContentKind::Email,
        .preview = "jane@example.com",
        .captured_at_ms = 300,
    });
    email_input.direct_send_available = true;
    const auto email_batch = build_desktop_decision(email_input);
    const auto enabled_send = std::find_if(email_batch.catalog.actions.begin(), email_batch.catalog.actions.end(),
                                           [](const ActionInstance& action) {
                                               return action.kind == ActionKind::SendEmail && action.enabled;
                                           });
    assert(enabled_send != email_batch.catalog.actions.end());

    email_input.direct_send_available = false;
    const auto no_send_batch = build_desktop_decision(email_input);
    const auto disabled_catalog_send = std::find_if(
        no_send_batch.catalog.actions.begin(), no_send_batch.catalog.actions.end(), [](const ActionInstance& action) {
            return action.kind == ActionKind::SendEmail && !action.enabled;
        });
    assert(disabled_catalog_send != no_send_batch.catalog.actions.end());
    const auto requested_send = std::find_if(
        no_send_batch.request.snapshot.available_actions.begin(), no_send_batch.request.snapshot.available_actions.end(),
        [](const ActionInstance& action) { return action.kind == ActionKind::SendEmail; });
    assert(requested_send == no_send_batch.request.snapshot.available_actions.end());

    DesktopDecisionInput tied_input;
    tied_input.request_id = "req_tied";
    tied_input.clipboard_items = {
        ClipboardItem{.ref = "first_equal_timestamp", .kind = ContentKind::Text, .preview = "first", .captured_at_ms = 500},
        ClipboardItem{.ref = "second_equal_timestamp", .kind = ContentKind::Text, .preview = "second", .captured_at_ms = 500},
    };
    const auto tied_batch = build_desktop_decision(tied_input);
    assert(tied_batch.request.snapshot.clipboard_items.size() == 1);
    assert(tied_batch.request.snapshot.clipboard_items.front().ref == "first_equal_timestamp");

    DesktopDecisionInput empty_input;
    empty_input.request_id = "req_empty";
    const auto empty_batch = build_desktop_decision(empty_input);
    assert(empty_batch.request.snapshot.clipboard_items.empty());
    assert(empty_batch.catalog.actions.empty());
    assert(empty_batch.request.snapshot.available_actions.empty());
}
