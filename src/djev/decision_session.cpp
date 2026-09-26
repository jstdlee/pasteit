#include "djev/decision_session.hpp"
#include "decision/fallback_ranker.hpp"

#include <algorithm>

namespace pastit {
namespace {

std::vector<RankedAction> prioritize_direct_mermaid(std::vector<RankedAction> ranked,
                                                    const ActionCatalog& catalog) {
    const auto direct = std::find_if(catalog.actions.begin(), catalog.actions.end(), [](const ActionInstance& action) {
        const auto source = action.parameters.find("mermaid_source");
        return action.enabled && action.kind == ActionKind::DrawMermaidDiagram &&
               source != action.parameters.end() && source->second == "direct";
    });
    if (direct != catalog.actions.end()) {
        auto match = std::find_if(ranked.begin(), ranked.end(), [&](const RankedAction& action) {
            return action.action.id == direct->id;
        });
        if (match == ranked.end()) {
            ranked.push_back(RankedAction{.action = *direct});
            match = std::prev(ranked.end());
        }
        std::rotate(ranked.begin(), match, std::next(match));
    }
    const auto selected = std::find_if(ranked.begin(), ranked.end(), [](const RankedAction& action) {
        return action.selected;
    });
    if (ranked.size() > kDisplayedActionLimit) {
        if (selected != ranked.end() &&
            static_cast<std::size_t>(std::distance(ranked.begin(), selected)) >= kDisplayedActionLimit) {
            ranked[kDisplayedActionLimit - 1] = *selected;
        }
        ranked.resize(kDisplayedActionLimit);
    }
    return ranked;
}

std::vector<RankedAction> request_local_fallback(const DecisionRequest& request,
                                                 const ActionRankingContext& context) {
    const ActionCatalog catalog{.actions = request.snapshot.available_actions};
    return prioritize_direct_mermaid(rank_fallback(catalog, request.snapshot, catalog.actions.size(), context), catalog);
}

PreparedDecision fallback_decision(const DecisionRequest& request,
                                   const ActionRankingContext& context,
                                   std::string reason,
                                   DecisionSessionStatus empty_status) {
    auto fallback = request_local_fallback(request, context);
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
                                         const ActionRankingContext& context) {
    (void)catalog;
    if (response_is_stale(request, current_snapshot)) {
        return PreparedDecision{
            .status = DecisionSessionStatus::Stale,
            .message = "discarded stale Djev response",
            .ranked = {},
        };
    }
    if (!response.valid) {
        return fallback_decision(request, context, response.error, DecisionSessionStatus::InvalidResponse);
    }
    const ActionCatalog request_catalog{.actions = request.snapshot.available_actions};
    if (!request_catalog.find(response.choice).has_value()) {
        return fallback_decision(request, context,
                                 "Djev selected an action outside the request-local catalog",
                                 DecisionSessionStatus::EmptyRanking);
    }
    auto ranked = rank_top_actions(response, request_catalog, request_catalog.actions.size(), context);
    if (ranked.empty()) {
        return fallback_decision(request, context,
                                 "Djev probabilities did not map to executable action IDs",
                                 DecisionSessionStatus::EmptyRanking);
    }
    return PreparedDecision{
        .status = DecisionSessionStatus::Ready,
        .message = "ranked",
        .ranked = prioritize_direct_mermaid(std::move(ranked), request_catalog),
    };
}

}  // namespace pastit
