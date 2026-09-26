#pragma once

#include "executor/action_executor.hpp"

namespace pastit {

// Local text utilities that copy a transformed value to the clipboard, plus
// open/reveal for paths. Saving variants live in the file executor.
bool is_utility_action(ActionKind kind);
ExecutionResult execute_utility_action(const ActionInstance& action, ExecutionContext& context);

}  // namespace pastit
