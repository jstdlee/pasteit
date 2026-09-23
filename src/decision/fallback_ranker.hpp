#pragma once
#include "djev/decision_ranker.hpp"
namespace pastit {
std::vector<RankedAction> rank_fallback(const ActionCatalog& catalog,const DecisionSnapshot& snapshot,std::size_t limit=5,
                                        const ActionPreferenceWeights& preferences = {},
                                        const ActionRankingContext& context = {});
}
