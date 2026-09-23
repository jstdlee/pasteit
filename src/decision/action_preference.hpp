#pragma once

#include "actions/action_catalog.hpp"
#include "core/types.hpp"

#include <map>
#include <optional>
#include <string>

namespace pastit {

using ActionPreferenceWeights = std::map<std::string, double>;

struct ActionRankingContext {
    ContentKind input_kind = ContentKind::Unknown;
    std::optional<ActionKind> djev_proposed_kind;
    std::map<ActionKind, double> local_action_bonus;
    std::map<std::string, double> local_action_bonus_by_id;
};

// Builds a stable semantic key, excluding request-local clipboard/action IDs.
std::string action_preference_key(const ActionInstance& action);

double action_preference_bonus(const ActionPreferenceWeights& weights, const ActionInstance& action);
double action_preference_bonus(const ActionPreferenceWeights& weights, const ActionInstance& action,
                               const ActionRankingContext& context);

// Record a bounded, diminishing-return reward for a user-selected action.
void record_action_preference(ActionPreferenceWeights& weights, const ActionInstance& action);
void record_action_preference(ActionPreferenceWeights& weights, const ActionInstance& action,
                              const ActionRankingContext& context);

}  // namespace pastit
