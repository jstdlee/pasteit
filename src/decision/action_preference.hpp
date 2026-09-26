#pragma once

#include "core/action.hpp"
#include "core/types.hpp"

#include <map>
#include <string>

namespace pasteit {

// Additive ranking nudges computed once per decision batch: local detector
// matches plus the learned usage frequency (see decision/usage_model.hpp).
struct ActionRankingContext {
    ContentKind input_kind = ContentKind::Unknown;
    std::map<ActionKind, double> local_action_bonus;
    std::map<std::string, double> local_action_bonus_by_id;
    std::map<std::string, double> usage_bonus_by_id;
    // Affects only which actions reach Djev, never the displayed ranking.
    std::map<std::string, double> candidate_priority_by_id;
};

double action_ranking_bonus(const ActionInstance& action, const ActionRankingContext& context);

}  // namespace pasteit
