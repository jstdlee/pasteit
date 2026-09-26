#pragma once

#include "actions/action_catalog.hpp"
#include "djev/decision_ranker.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace pasteit {

enum class DecisionSessionStatus {
    Ready,
    InvalidResponse,
    Stale,
    EmptyRanking,
};

struct PreparedDecision {
    DecisionSessionStatus status = DecisionSessionStatus::InvalidResponse;
    std::string message;
    std::vector<RankedAction> ranked;
};

inline constexpr std::size_t kDisplayedActionLimit = 8;

PreparedDecision prepare_ranked_decision(const DecisionRequest& request, const DecisionResponse& response,
                                         const ActionCatalog& catalog, const DecisionSnapshot& current_snapshot,
                                         const ActionRankingContext& context = {});

}  // namespace pasteit
