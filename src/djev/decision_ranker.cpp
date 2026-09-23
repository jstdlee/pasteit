#include "djev/decision_ranker.hpp"

#include <algorithm>

namespace pastit {

std::vector<RankedAction> rank_top_actions(const DecisionResponse& response, const ActionCatalog& catalog, std::size_t limit,
                                           const ActionPreferenceWeights& preferences) {
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
            .probability = std::clamp(probability + action_preference_bonus(preferences, *action), 0.0, 1.0),
            .selected = id == response.choice,
            .confidence = response.confidence,
        });
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedAction& left, const RankedAction& right) {
        return left.probability > right.probability;
    });
    if (ranked.size() > limit) {
        ranked.resize(limit);
    }
    return ranked;
}

bool response_is_stale(const DecisionRequest& request, const DecisionSnapshot& current_snapshot) {
    return request.snapshot.clipboard_hash != current_snapshot.clipboard_hash ||
           request.snapshot.focused_target_hash != current_snapshot.focused_target_hash;
}

}  // namespace pastit
