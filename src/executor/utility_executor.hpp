#pragma once

#include "executor/action_executor.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace pasteit {

// Local text utilities that copy a transformed value to the clipboard, plus
// open/reveal for paths. Saving variants live in the file executor.
bool is_utility_action(ActionKind kind);
ExecutionResult execute_utility_action(const ActionInstance& action, ExecutionContext& context);

// Text actions whose result can be shown before running them: the output is
// a deterministic function of the clipboard text.
bool can_preview_text_action(ActionKind kind);
// The text the action would copy, without touching the clipboard; nullopt
// when the action has no preview or the text does not fit it.
std::optional<std::string> preview_text_action(const ActionInstance& action, std::string_view source);

}  // namespace pasteit
