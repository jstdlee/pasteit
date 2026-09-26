#pragma once

#include "core/action.hpp"
#include "core/protocol.hpp"
#include "config/app_settings.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pastit {

struct ActionCatalog {
    std::vector<ActionInstance> actions;

    std::optional<ActionInstance> find(const std::string& id) const;
    std::optional<ActionInstance> find_by_kind_and_label(ActionKind kind, const std::string& label) const;
};

// Stable, human-readable name for an action kind (persisted in usage history).
std::string action_kind_slug(ActionKind kind);

ActionCatalog build_catalog(const DecisionSnapshot& snapshot);
ActionCatalog build_catalog(const DecisionSnapshot& snapshot, const std::vector<PromptTemplate>& templates,
                            const ProviderSettings& provider);

}  // namespace pastit
