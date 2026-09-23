#pragma once

#include "core/protocol.hpp"
#include "djev/decision_session.hpp"
#include "djev/decision_ranker.hpp"

#include <string>
#include <vector>

namespace pastit {

struct PopupRow {
    std::string action_id;
    std::string label;
    std::string target_path;
    double probability = 0.0;
    bool selected = false;
    bool enabled = true;
    ExecutionStatus status = ExecutionStatus::Ranked;
};

struct PopupModel {
    std::string preview;
    std::string target_summary;
    std::vector<PopupRow> rows;
};

PopupModel build_popup_model(const DecisionSnapshot& snapshot, const std::vector<RankedAction>& ranked_actions);
PreparedDecision prepare_popup_decision(const DecisionRequest& request, const DecisionResponse& response,
                                        const ActionCatalog& catalog, const DecisionSnapshot& current_snapshot,
                                        const ActionPreferenceWeights& preferences = {});

}  // namespace pastit
