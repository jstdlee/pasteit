#pragma once

#include "core/action.hpp"
#include "core/protocol.hpp"
#include "config/app_settings.hpp"
#include "detect/content_profile.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pasteit {

struct ActionCatalog {
    std::vector<ActionInstance> actions;
    // General shape of the current clipboard item, judged from a sample.
    std::optional<ContentProfile> profile;

    std::optional<ActionInstance> find(const std::string& id) const;
    std::optional<ActionInstance> find_by_kind_and_label(ActionKind kind, const std::string& label) const;
};

// Adds a Run pipeline action for each enabled recipe whose applies_to
// matches the current clipboard's profile.
void add_pipeline_recipe_actions(ActionCatalog& catalog, const DecisionSnapshot& snapshot,
                                 const std::vector<PipelineRecipe>& recipes);
bool pipeline_recipe_applies(const PipelineRecipe& recipe, const ContentProfile& profile);

// Stable, human-readable name for an action kind (persisted in usage history).
std::string action_kind_slug(ActionKind kind);

ActionCatalog build_catalog(const DecisionSnapshot& snapshot);
ActionCatalog build_catalog(const DecisionSnapshot& snapshot, const std::vector<PromptTemplate>& templates,
                            const ProviderSettings& provider);

}  // namespace pasteit
