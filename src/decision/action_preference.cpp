#include "decision/action_preference.hpp"

#include "decision/usage_model.hpp"

#include <algorithm>
#include <cmath>

namespace pastit {
namespace {

constexpr double kMaximumLocalBonus = 0.05;
constexpr double kMaximumTotalBonus = 0.25;

template <typename Map, typename Key>
double bounded(const Map& values, const Key& key, double maximum) {
    const auto found = values.find(key);
    if (found == values.end() || !std::isfinite(found->second)) return 0.0;
    return std::clamp(found->second, 0.0, maximum);
}

}  // namespace

double action_ranking_bonus(const ActionInstance& action, const ActionRankingContext& context) {
    const double bonus = bounded(context.local_action_bonus, action.kind, kMaximumLocalBonus) +
                         bounded(context.local_action_bonus_by_id, action.id, kMaximumLocalBonus) +
                         bounded(context.usage_bonus_by_id, action.id, kMaximumUsageBonus);
    return std::clamp(bonus, 0.0, kMaximumTotalBonus);
}

}  // namespace pastit
