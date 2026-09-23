#include "ui/popup.hpp"

#include "decision/candidate_selector.hpp"
#include "util/path_utf8.hpp"

#include <algorithm>

namespace pastit {
namespace {

std::string target_path_for(const DecisionSnapshot& snapshot, const std::string& ref) {
    if (ref.empty()) {
        return {};
    }
    if (ref == "temp") {
        return "temp";
    }
    for (const auto& path : snapshot.recent_paths) {
        if (path.ref == ref) {
            return path_to_utf8_string(path.path);
        }
    }
    return ref;
}

}  // namespace

PopupModel build_popup_model(const DecisionSnapshot& snapshot, const std::vector<RankedAction>& ranked_actions) {
    PopupModel model;
    if (!snapshot.clipboard_items.empty()) {
        model.preview = snapshot.clipboard_items.front().preview;
    }
    model.target_summary = snapshot.focused_target_hash.empty() ? "focused target" : snapshot.focused_target_hash;

    model.rows.reserve(std::min<std::size_t>(ranked_actions.size(), kDisplayedActionLimit));
    for (const auto& ranked : ranked_actions) {
        if (model.rows.size() >= kDisplayedActionLimit) {
            break;
        }
        if (!is_current_source_action(ranked.action, snapshot)) {
            continue;
        }
        model.rows.push_back(PopupRow{
            .action_id = ranked.action.id,
            .label = ranked.action.label,
            .target_path = target_path_for(snapshot, ranked.action.target_ref),
            .probability = ranked.probability,
            .selected = ranked.selected,
            .enabled = ranked.action.enabled,
            .status = ExecutionStatus::Ranked,
        });
    }
    return model;
}

PreparedDecision prepare_popup_decision(const DecisionRequest& request, const DecisionResponse& response,
                                        const ActionCatalog& catalog, const DecisionSnapshot& current_snapshot,
                                        const ActionPreferenceWeights& preferences) {
    return prepare_ranked_decision(request, response, catalog, current_snapshot, preferences);
}

}  // namespace pastit
