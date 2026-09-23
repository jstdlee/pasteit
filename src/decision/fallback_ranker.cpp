#include "decision/fallback_ranker.hpp"
#include "decision/candidate_selector.hpp"

namespace pastit {
std::vector<RankedAction> rank_fallback(const ActionCatalog& catalog,const DecisionSnapshot& snapshot,std::size_t limit,
                                        const ActionPreferenceWeights& preferences,
                                        const ActionRankingContext& context){
    const auto ordered=select_djev_candidates(catalog,snapshot,catalog.actions.size(),preferences,context);
    std::vector<RankedAction> result;
    const auto count=std::min(limit,ordered.actions.size());
    result.reserve(count);
    for(std::size_t i=0;i<count;++i){
        const auto base = 1.0-static_cast<double>(i)*0.01;
        result.push_back({.action=ordered.actions[i],.probability=std::min(1.0,base+action_preference_bonus(preferences,ordered.actions[i],context)),.selected=i==0,.confidence=0.0});
    }
    return result;
}
}  // namespace pastit
