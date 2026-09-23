#include "djev/decision_session.hpp"

#include <cassert>

int main() {
    using namespace pastit;

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
}
