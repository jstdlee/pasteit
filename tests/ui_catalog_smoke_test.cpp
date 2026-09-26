#include "actions/action_catalog.hpp"
#include "djev/decision_ranker.hpp"
#include "ui/popup.hpp"

#include <cassert>
#include <filesystem>
#include <string>

int main() {
    using namespace pasteit;

    DecisionSnapshot snapshot;
    snapshot.clipboard_hash = "clip_hash";
    snapshot.focused_target_hash = "target_hash";
    snapshot.clipboard_items.push_back(ClipboardItem{
        .ref = "clip_text",
        .mime_types = {"text/plain"},
        .kind = ContentKind::Text,
        .preview = "hello popup",
        .size_bytes = 11,
    });
    snapshot.recent_paths.push_back(PathLocation{
        .ref = "path_01",
        .path = std::filesystem::temp_directory_path(),
        .kind = PathKind::Directory,
        .last_seen_ms = 10,
        .source = "test",
        .exists = true,
    });

    auto catalog = build_catalog(snapshot);
    for (int index = 0; index < 6; ++index) {
        ActionInstance extra;
        extra.id = "extra-" + std::to_string(index);
        extra.kind = ActionKind::PasteText;
        extra.source_ref = "clip_text";
        extra.label = "Extra action " + std::to_string(index);
        extra.enabled = true;
        catalog.actions.push_back(extra);
    }
    DecisionResponse response;
    response.request_id = "req_popup";
    response.choice = catalog.actions.front().id;
    response.valid = true;
    double probability = 0.9;
    for (const auto& action : catalog.actions) {
        response.probabilities[action.id] = probability;
        probability -= 0.05;
    }

    const auto ranked = rank_top_actions(response, catalog, 8);
    const auto model = build_popup_model(snapshot, ranked);

    assert(model.preview == "hello popup");
    assert(model.rows.size() == 8);
    assert(!model.rows.empty());
    assert(model.rows.front().action_id == catalog.actions.front().id);
    assert(model.rows.front().label == catalog.actions.front().label);
    assert(model.rows.front().selected);
    for (const auto& row : model.rows) {
        assert(!row.action_id.empty());
        assert(!row.label.empty());
        assert(row.probability >= 0.0);
    }
}
