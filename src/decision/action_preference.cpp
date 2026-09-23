#include "decision/action_preference.hpp"

#include <algorithm>
#include <cmath>

namespace pastit {
namespace {

constexpr double kMaximumBonus = 0.20;
constexpr double kLearningRate = 0.25;

}  // namespace

std::string action_preference_key(const ActionInstance& action) {
    std::string key = "kind:" + std::to_string(static_cast<int>(action.kind));
    if (!action.target_ref.empty()) {
        key += "|target:" + action.target_ref;
    }
    if (action.kind == ActionKind::TransformText) {
        const auto template_id = action.parameters.find("template_id");
        if (template_id != action.parameters.end()) {
            key += "|template:" + template_id->second;
        }
    }
    return key;
}

double action_preference_bonus(const ActionPreferenceWeights& weights, const ActionInstance& action) {
    const auto value = weights.find(action_preference_key(action));
    if (value == weights.end() || !std::isfinite(value->second)) return 0.0;
    return std::clamp(value->second, 0.0, kMaximumBonus);
}

void record_action_preference(ActionPreferenceWeights& weights, const ActionInstance& action) {
    auto& value = weights[action_preference_key(action)];
    value = std::clamp(value + (kMaximumBonus - value) * kLearningRate, 0.0, kMaximumBonus);
}

}  // namespace pastit
