#pragma once

#include "core/action.hpp"

namespace pastit {

// Visual and ranking grouping of action kinds.
enum class ActionCategory { Paste, Open, Save, Convert, Extract, Network, Code, Ai, Media };

ActionCategory action_category(ActionKind kind);

// Local-fallback prior: actions that exist only because the content matched a
// specific shape (a color, a JWT, an IP) outrank generic text utilities, so a
// ranking without Djev still leads with what the content is.
double action_specificity_prior(ActionKind kind);

}  // namespace pastit
