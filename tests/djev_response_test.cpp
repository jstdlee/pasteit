#include "actions/action_catalog.hpp"
#include "djev/decision_ranker.hpp"
#include "djev/djev_client.hpp"

#include <cassert>
#include <string>

namespace {

pastit::ActionInstance action(std::string id, pastit::ActionKind kind, double index) {
    pastit::ActionInstance out;
    out.id = std::move(id);
    out.kind = kind;
    out.source_ref = "clip_1";
    out.label = "Action " + std::to_string(static_cast<int>(index));
    out.description = "Choice " + std::to_string(static_cast<int>(index));
    out.enabled = true;
    return out;
}

pastit::DecisionRequest request_with_actions() {
    pastit::DecisionRequest request;
    request.request_id = "req_184";
    request.snapshot.clipboard_hash = "clip_hash";
    request.snapshot.focused_target_hash = "target_hash";
    request.snapshot.focused_app = "Code";
    request.snapshot.focused_window_title = "notes.txt — Code";
    request.snapshot.focused_current_directory = "/tmp/project";
    request.snapshot.clipboard_items.push_back(pastit::ClipboardItem{
        .ref = "clip_1",
        .mime_types = {"text/plain"},
        .kind = pastit::ContentKind::Text,
        .preview = "hello",
        .size_bytes = 5,
    });
    request.snapshot.recent_paths.push_back(pastit::PathLocation{
        .ref = "path_01",
        .path = "/tmp",
        .kind = pastit::PathKind::Directory,
        .last_seen_ms = 1,
        .source = "test",
        .exists = true,
    });
    request.snapshot.available_actions = {
        action("a1", pastit::ActionKind::PasteText, 1),
        action("a2", pastit::ActionKind::SaveTextFile, 2),
        action("a3", pastit::ActionKind::OpenUrl, 3),
        action("a4", pastit::ActionKind::CopyPath, 4),
        action("a5", pastit::ActionKind::PrettyJson, 5),
        action("a6", pastit::ActionKind::SaveJsonFile, 6),
    };
    return request;
}

}  // namespace

int main() {
    using namespace pastit;

    const auto request = request_with_actions();
    const auto catalog = ActionCatalog{request.snapshot.available_actions};

    const auto payload = DjevClient::build_payload(request, "jev-test");
    assert(payload.find(R"("model":"jev-test")") != std::string::npos);
    assert(payload.find(R"("target")") != std::string::npos);
    assert(payload.find(R"("app":"Code")") != std::string::npos);
    assert(payload.find(R"("window_title":"notes.txt — Code")") != std::string::npos);
    assert(payload.find(R"("current_directory":"/tmp/project")") != std::string::npos);
    assert(payload.find(R"("clipboard")") != std::string::npos);
    assert(payload.find(R"("history")") == std::string::npos);
    assert(payload.find(R"("available_actions")") == std::string::npos);
    assert(payload.find(R"("type":"choice")") != std::string::npos);
    assert(payload.find(R"("a1")") != std::string::npos);
    assert(payload.find(R"("a6")") != std::string::npos);

    const std::string nested_response =
        R"({"request_id":"req_184","answers":{"best_action":{"choice":"a2","confidence":0.7,"probabilities":{"a2":0.7,"a1":0.2}}}})";
    const auto parsed = DjevClient::parse_response(nested_response, request);
    assert(parsed.valid);
    assert(parsed.request_id == "req_184");
    assert(parsed.choice == "a2");
    assert(parsed.confidence == 0.7);
    assert(parsed.probabilities.at("a2") == 0.7);

    const auto ranked = rank_top_actions(parsed, catalog);
    assert(ranked.size() == 2);
    assert(ranked.front().action.id == "a2");
    assert(ranked.front().selected);
    assert(ranked.front().probability == 0.7);

    const std::string flat_response =
        R"({"request_id":"req_184","choice":"a3","confidence":0.6,"probabilities":{"a6":0.1,"a5":0.2,"a4":0.3,"a3":0.4,"a2":0.5,"a1":0.6}})";
    const auto flat = DjevClient::parse_response(flat_response, request);
    assert(flat.valid);
    const auto top_five = rank_top_actions(flat, catalog);
    assert(top_five.size() == 5);
    assert(top_five.front().action.id == "a1");
    assert(top_five[1].action.id == "a2");
    assert(top_five.back().action.id == "a5");
    for (const auto& ranked_action : top_five) {
        assert(ranked_action.action.id != "a6");
    }

    const auto malformed = DjevClient::parse_response(R"({"answers":{}})", request);
    assert(!malformed.valid);
    assert(rank_top_actions(malformed, catalog).empty());

    const auto non_json = DjevClient::parse_response(R"(not json "choice":"a2","probabilities":{"a2":1})", request);
    assert(!non_json.valid);

    const auto wrong_request = DjevClient::parse_response(
        R"({"request_id":"different_req","choice":"a2","confidence":0.9,"probabilities":{"a2":0.9}})",
        request);
    assert(!wrong_request.valid);

    const auto missing_choice_probability = DjevClient::parse_response(
        R"({"request_id":"req_184","choice":"a2","confidence":0.9,"probabilities":{"a1":0.9}})",
        request);
    assert(!missing_choice_probability.valid);

    const auto unknown_probability = DjevClient::parse_response(
        R"({"request_id":"req_184","choice":"a2","confidence":0.9,"probabilities":{"a2":0.8,"not_in_catalog":0.2}})",
        request);
    assert(!unknown_probability.valid);

    const auto unknown_choice = DjevClient::parse_response(
        R"({"request_id":"req_184","choice":"not_in_catalog","confidence":0.9,"probabilities":{"not_in_catalog":0.9}})",
        request);
    assert(!unknown_choice.valid);
    assert(rank_top_actions(unknown_choice, catalog).empty());

    auto changed_snapshot = request.snapshot;
    changed_snapshot.clipboard_hash = "changed";
    assert(response_is_stale(request, changed_snapshot));
    assert(!response_is_stale(request, request.snapshot));
}
