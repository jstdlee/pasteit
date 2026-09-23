#pragma once

#include "actions/action_catalog.hpp"

#include <map>
#include <string>

namespace pastit {

using ActionPreferenceWeights = std::map<std::string, double>;

// Builds a stable semantic key, excluding request-local clipboard/action IDs.
std::string action_preference_key(const ActionInstance& action);

double action_preference_bonus(const ActionPreferenceWeights& weights, const ActionInstance& action);

// Record a bounded, diminishing-return reward for a user-selected action.
void record_action_preference(ActionPreferenceWeights& weights, const ActionInstance& action);

}  // namespace pastit
