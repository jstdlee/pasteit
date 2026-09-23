#pragma once
#include "actions/action_catalog.hpp"
#include "decision/action_preference.hpp"
#include <cstddef>
namespace pastit {
bool is_current_source_action(const ActionInstance& action, const DecisionSnapshot& snapshot);
ActionCatalog select_djev_candidates(const ActionCatalog& full, const DecisionSnapshot& snapshot, std::size_t limit = 26,
                                     const ActionPreferenceWeights& preferences = {});
}
