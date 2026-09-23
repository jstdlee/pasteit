#include "djev/decision_session.hpp"
#include "decision/fallback_ranker.hpp"

namespace pastit {
namespace {

std::vector<RankedAction> request_local_fallback(const DecisionRequest& request,
                                                 const ActionPreferenceWeights& preferences) {
    return rank_fallback(ActionCatalog{.actions = request.snapshot.available_actions}, request.snapshot,
                         kDisplayedActionLimit, preferences);
}

PreparedDecision fallback_decision(const DecisionRequest& request,
                                   const ActionPreferenceWeights& preferences,
                                   std::string reason,
                                   DecisionSessionStatus empty_status) {
    auto fallback = request_local_fallback(request, preferences);
    if (!fallback.empty()) {
        return PreparedDecision{
            .status = DecisionSessionStatus::Ready,
            .message = reason.empty() ? "local fallback" : "local fallback: " + reason,
            .ranked = std::move(fallback),
        };
    }
    return PreparedDecision{
        .status = empty_status,
        .message = std::move(reason),
        .ranked = {},
    };
}

}  // namespace

PreparedDecision prepare_ranked_decision(const DecisionRequest& request, const DecisionResponse& response,
                                         const ActionCatalog& catalog, const DecisionSnapshot& current_snapshot,
                                         const ActionPreferenceWeights& preferences) {
    (void)catalog;
    if (response_is_stale(request, current_snapshot)) {
        return PreparedDecision{
            .status = DecisionSessionStatus::Stale,
            .message = "discarded stale Djev response",
            .ranked = {},
        };
    }
    if (!response.valid) {
        return fallback_decision(request, preferences, response.error, DecisionSessionStatus::InvalidResponse);
    }
    const ActionCatalog request_catalog{.actions = request.snapshot.available_actions};
    if (!request_catalog.find(response.choice).has_value()) {
        return fallback_decision(request, preferences,
                                 "Djev selected an action outside the request-local catalog",
                                 DecisionSessionStatus::EmptyRanking);
    }
    auto ranked = rank_top_actions(response, request_catalog, kDisplayedActionLimit, preferences);
    if (ranked.empty()) {
        return fallback_decision(request, preferences,
                                 "Djev probabilities did not map to executable action IDs",
                                 DecisionSessionStatus::EmptyRanking);
    }
    return PreparedDecision{
        .status = DecisionSessionStatus::Ready,
        .message = "ranked",
        .ranked = std::move(ranked),
    };
}

}  // namespace pastit
