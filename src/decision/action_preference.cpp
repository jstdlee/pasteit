#include "decision/action_preference.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace pastit {
namespace {

constexpr double kMaximumGlobalBonus = 0.04;
constexpr double kMaximumContextBonus = 0.06;
constexpr double kMaximumLocalBonus = 0.05;
constexpr double kMaximumTotalBonus = 0.15;
constexpr double kLearningRate = 0.25;

std::string contextual_key(const ActionInstance& action, const ActionRankingContext& context) {
    return action_preference_key(action) + "|input:" +
           std::to_string(static_cast<int>(context.input_kind)) + "|proposal:" +
           std::to_string(static_cast<int>(*context.djev_proposed_kind));
}

void reinforce(ActionPreferenceWeights& weights, const std::string& key, double maximum) {
    auto& value = weights[key];
    if (!std::isfinite(value)) value = 0.0;
    value = std::clamp(value + (maximum - std::clamp(value, 0.0, maximum)) * kLearningRate,
                       0.0, maximum);
}

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
    return std::clamp(value->second, 0.0, kMaximumGlobalBonus);
}

double action_preference_bonus(const ActionPreferenceWeights& weights, const ActionInstance& action,
                               const ActionRankingContext& context) {
    double bonus = action_preference_bonus(weights, action);
    if (context.input_kind != ContentKind::Unknown && context.djev_proposed_kind.has_value()) {
        const auto value = weights.find(contextual_key(action, context));
        if (value != weights.end() && std::isfinite(value->second)) {
            bonus += std::clamp(value->second, 0.0, kMaximumContextBonus);
        }
    }
    const auto local = context.local_action_bonus.find(action.kind);
    if (local != context.local_action_bonus.end() && std::isfinite(local->second)) {
        bonus += std::clamp(local->second, 0.0, kMaximumLocalBonus);
    }
    const auto specific_local = context.local_action_bonus_by_id.find(action.id);
    if (specific_local != context.local_action_bonus_by_id.end() && std::isfinite(specific_local->second)) {
        bonus += std::clamp(specific_local->second, 0.0, kMaximumLocalBonus);
    }
    return std::clamp(bonus, 0.0, kMaximumTotalBonus);
}

void record_action_preference(ActionPreferenceWeights& weights, const ActionInstance& action) {
    reinforce(weights, action_preference_key(action), kMaximumGlobalBonus);
}

void record_action_preference(ActionPreferenceWeights& weights, const ActionInstance& action,
                              const ActionRankingContext& context) {
    record_action_preference(weights, action);
    if (context.input_kind != ContentKind::Unknown && context.djev_proposed_kind.has_value()) {
        reinforce(weights, contextual_key(action, context), kMaximumContextBonus);
    }
}

}  // namespace pastit
