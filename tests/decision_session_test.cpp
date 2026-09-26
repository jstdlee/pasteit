#include "djev/decision_session.hpp"

#include <algorithm>
#include <cassert>
#include <string>

int main() {
    using namespace pasteit;

    DecisionRequest request;
    request.request_id = "req_session";
    request.snapshot.clipboard_hash = "clip_hash";
    request.snapshot.focused_target_hash = "target_hash";
    request.snapshot.clipboard_items.push_back(ClipboardItem{
        .ref = "clip_1",
        .mime_types = {"text/plain"},
        .kind = ContentKind::Text,
        .preview = "hello",
        .size_bytes = 5,
        .captured_at_ms = 1,
    });
    request.snapshot.available_actions.push_back(ActionInstance{
        .id = "a_paste",
        .kind = ActionKind::PasteText,
        .source_ref = "clip_1",
        .label = "Paste text",
        .enabled = true,
    });

    ActionCatalog catalog{request.snapshot.available_actions};

    DecisionResponse response;
    response.request_id = request.request_id;
    response.choice = "a_paste";
    response.confidence = 0.9;
    response.probabilities["a_paste"] = 0.9;
    response.valid = true;

    const auto accepted = prepare_ranked_decision(request, response, catalog, request.snapshot);
    assert(accepted.status == DecisionSessionStatus::Ready);
    assert(accepted.ranked.size() == 1);
    assert(accepted.ranked.front().action.id == "a_paste");

    auto changed = request.snapshot;
    changed.clipboard_hash = "changed_hash";
    const auto stale = prepare_ranked_decision(request, response, catalog, changed);
    assert(stale.status == DecisionSessionStatus::Stale);
    assert(stale.ranked.empty());

    changed = request.snapshot;
    changed.focused_target_hash = "another_target_hash";
    const auto target_stale = prepare_ranked_decision(request, response, catalog, changed);
    assert(target_stale.status == DecisionSessionStatus::Stale);
    assert(target_stale.ranked.empty());

    DecisionResponse unknown_choice;
    unknown_choice.request_id = request.request_id;
    unknown_choice.choice = "a_not_in_catalog";
    unknown_choice.confidence = 0.95;
    unknown_choice.probabilities["a_not_in_catalog"] = 0.95;
    unknown_choice.valid = true;
    const auto fallback = prepare_ranked_decision(request, unknown_choice, catalog, request.snapshot);
    assert(fallback.status == DecisionSessionStatus::Ready);
    assert(fallback.ranked.size() == 1);
    assert(fallback.ranked.front().action.id == "a_paste");
    assert(fallback.message.find("fallback") != std::string::npos);

    auto extra = request.snapshot.available_actions.front();
    extra.id = "a_full_catalog_only";
    extra.label = "Not applicable to this request";
    catalog.actions.push_back(extra);
    auto inapplicable_choice = response;
    inapplicable_choice.choice = extra.id;
    inapplicable_choice.probabilities[extra.id] = 0.99;
    const auto inapplicable = prepare_ranked_decision(request, inapplicable_choice, catalog, request.snapshot);
    assert(inapplicable.status == DecisionSessionStatus::Ready);
    assert(inapplicable.message.find("fallback") != std::string::npos);
    assert(inapplicable.ranked.size() == 1);
    assert(inapplicable.ranked.front().action.id == "a_paste");

    auto extra_probability = response;
    extra_probability.probabilities[extra.id] = 0.99;
    const auto request_only = prepare_ranked_decision(request, extra_probability, catalog, request.snapshot);
    assert(request_only.ranked.size() == 1);
    assert(request_only.ranked.front().action.id == "a_paste");

    // The selected action must remain visible even when eight alternatives
    // have larger ranking scores.
    auto crowded = request;
    auto crowded_response = response;
    crowded_response.probabilities["a_paste"] = 0.01;
    for (int index = 0; index < 8; ++index) {
        auto alternative = request.snapshot.available_actions.front();
        alternative.id = "alternative_" + std::to_string(index);
        crowded.snapshot.available_actions.push_back(alternative);
        crowded_response.probabilities[alternative.id] = 0.90 - index * 0.1;
    }
    const auto crowded_result = prepare_ranked_decision(crowded, crowded_response,
                                                        ActionCatalog{crowded.snapshot.available_actions},
                                                        crowded.snapshot);
    assert(crowded_result.ranked.size() == kDisplayedActionLimit);
    assert(std::any_of(crowded_result.ranked.begin(), crowded_result.ranked.end(), [](const auto& ranked) {
        return ranked.selected && ranked.action.id == "a_paste";
    }));

    auto mermaid_request = crowded;
    auto direct_mermaid = request.snapshot.available_actions.front();
    direct_mermaid.id = "a_mermaid";
    direct_mermaid.kind = ActionKind::DrawMermaidDiagram;
    direct_mermaid.parameters["mermaid_source"] = "direct";
    mermaid_request.snapshot.available_actions.push_back(direct_mermaid);
    auto mermaid_response = crowded_response;
    mermaid_response.choice = "alternative_0";
    mermaid_response.probabilities[direct_mermaid.id] = 0.01;
    const auto mermaid_ranked = prepare_ranked_decision(
        mermaid_request, mermaid_response, ActionCatalog{mermaid_request.snapshot.available_actions},
        mermaid_request.snapshot);
    assert(mermaid_ranked.ranked.size() == kDisplayedActionLimit);
    assert(mermaid_ranked.ranked.front().action.id == "a_mermaid");
    assert(mermaid_ranked.ranked.front().probability == 0.01);
    assert(std::any_of(mermaid_ranked.ranked.begin(), mermaid_ranked.ranked.end(), [](const auto& ranked) {
        return ranked.selected && ranked.action.id == "alternative_0";
    }));
}
