#include "djev/decision_ranker.hpp"

#include <algorithm>

namespace pastit {

std::vector<RankedAction> rank_top_actions(const DecisionResponse& response, const ActionCatalog& catalog, std::size_t limit,
                                           const ActionPreferenceWeights& preferences,
                                           const ActionRankingContext& context) {
    if (!response.valid) {
        return {};
    }

    std::vector<RankedAction> ranked;
    for (const auto& [id, probability] : response.probabilities) {
        const auto action = catalog.find(id);
        if (!action.has_value()) {
            continue;
        }
        ranked.push_back(RankedAction{
            .action = *action,
            .probability = std::clamp(probability + action_preference_bonus(preferences, *action, context), 0.0, 1.0),
            .selected = id == response.choice,
            .confidence = response.confidence,
        });
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedAction& left, const RankedAction& right) {
        return left.probability > right.probability;
    });
    if (ranked.size() > limit) {
        const auto chosen = std::find_if(ranked.begin(), ranked.end(), [](const RankedAction& action) {
            return action.selected;
        });
        if (limit > 0 && chosen != ranked.end() &&
            static_cast<std::size_t>(std::distance(ranked.begin(), chosen)) >= limit) {
            ranked[limit - 1] = *chosen;
        }
        ranked.resize(limit);
    }
    return ranked;
}

bool response_is_stale(const DecisionRequest& request, const DecisionSnapshot& current_snapshot) {
    return request.snapshot.clipboard_hash != current_snapshot.clipboard_hash ||
           request.snapshot.focused_target_hash != current_snapshot.focused_target_hash;
}

}  // namespace pastit
