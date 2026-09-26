#pragma once

#include "actions/action_catalog.hpp"
#include "core/protocol.hpp"
#include "decision/action_preference.hpp"

#include <cstddef>
#include <vector>

namespace pastit {

struct RankedAction {
    ActionInstance action;
    double probability = 0.0;
    bool selected = false;
    double confidence = 0.0;
};

std::vector<RankedAction> rank_top_actions(const DecisionResponse& response, const ActionCatalog& catalog, std::size_t limit = 5,
                                           const ActionRankingContext& context = {});
bool response_is_stale(const DecisionRequest& request, const DecisionSnapshot& current_snapshot);

}  // namespace pastit
