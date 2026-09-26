#include "actions/action_catalog.hpp"
#include "djev/decision_ranker.hpp"
#include "djev/djev_client.hpp"
#include "decision/candidate_selector.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

std::string trim(std::string value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
    }
    return value;
}

void load_dotenv_if_present() {
    std::ifstream in(".env");
    if (!in) {
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos || line.empty() || line.front() == '#') {
            continue;
        }
        const auto key = trim(line.substr(0, equals));
        auto value = trim(line.substr(equals + 1));
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        if ((key == "DJEV_URL" || key == "DJEV_MODEL" || key == "DJEV_API_KEY" || key == "API_KEY" || key == "TYPESAFE_API_KEY") &&
            std::getenv(key.c_str()) == nullptr) {
            setenv(key.c_str(), value.c_str(), 0);
        }
    }
}

std::string sanitized_endpoint(std::string endpoint) {
    const auto query = endpoint.find_first_of("?#");
    if (query != std::string::npos) {
        endpoint.erase(query);
    }
    const auto scheme = endpoint.find("://");
    const auto at = endpoint.find('@');
    if (scheme != std::string::npos && at != std::string::npos && at > scheme + 3) {
        endpoint.replace(scheme + 3, at - (scheme + 3), "***");
    }
    return endpoint;
}

pastit::DecisionRequest make_request(std::string request_id, pastit::ContentKind kind, std::string ref, std::string preview) {
    pastit::DecisionSnapshot snapshot;
    snapshot.clipboard_hash = request_id + "_clip_hash";
    snapshot.focused_target_hash = request_id + "_target_hash";
    snapshot.captured_at_ms = 1726800000123;
    snapshot.recent_paths.push_back(pastit::PathLocation{
        .ref = "path_01",
        .path = std::filesystem::temp_directory_path(),
        .kind = pastit::PathKind::Directory,
        .last_seen_ms = 10,
        .source = "live-test",
        .exists = true,
    });
    snapshot.clipboard_items.push_back(pastit::ClipboardItem{
        .ref = std::move(ref),
        .mime_types = {kind == pastit::ContentKind::Json ? "application/json" : "text/plain"},
        .kind = kind,
        .preview = std::move(preview),
        .size_bytes = 1,
        .captured_at_ms = snapshot.captured_at_ms,
    });
    const auto catalog = pastit::build_catalog(snapshot);

    pastit::DecisionRequest request;
    request.request_id = std::move(request_id);
    request.snapshot = std::move(snapshot);
    request.snapshot.available_actions = catalog.actions;
    return request;
}

void append_historical_items(pastit::DecisionRequest& request, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        request.snapshot.clipboard_items.push_back(pastit::ClipboardItem{
            .ref = "historical_" + std::to_string(index),
            .mime_types = {index % 2 == 0 ? "image/png" : "message/rfc822"},
            .kind = index % 2 == 0 ? pastit::ContentKind::Image : pastit::ContentKind::Email,
            .preview = "MUST_NOT_REACH_DJEV_HISTORY_" + std::to_string(index),
            .size_bytes = 4096,
            .captured_at_ms = request.snapshot.captured_at_ms - static_cast<std::int64_t>(index + 1),
        });
    }
}

bool run_case(const std::string& name, const pastit::DecisionRequest& request, const pastit::DjevClient& client) {
    const auto payload = pastit::DjevClient::build_payload(request, pastit::DjevClient::model_from_env());
    if (payload.size() > 10 * 1024) {
        std::cerr << name << " failed: payload_bytes=" << payload.size() << "\n";
        return false;
    }
    const auto catalog = pastit::ActionCatalog{request.snapshot.available_actions};
    const auto response = client.decide(request);
    if (!response.valid) {
        std::cerr << name << " failed: " << response.error << "\n";
        return false;
    }
    const auto ranked = pastit::rank_top_actions(response, catalog);
    if (ranked.empty()) {
        std::cerr << name << " failed: response probabilities did not map to request-local action IDs\n";
        return false;
    }
    std::cout << name << " ok: candidates=" << request.snapshot.available_actions.size()
              << " payload_bytes=" << payload.size() << " choice=" << response.choice
              << " mapped_actions=" << ranked.size() << "\n";
    return true;
}

pastit::DecisionRequest make_bounded_request() {
    pastit::DecisionSnapshot snapshot;
    snapshot.clipboard_hash="bounded_hash";snapshot.focused_target_hash="bounded_target";snapshot.captured_at_ms=1726800000999;
    for(int p=0;p<6;++p)snapshot.recent_paths.push_back({.ref="path_"+std::to_string(p),.path=std::filesystem::temp_directory_path()/std::string(220,'p'),.kind=pastit::PathKind::Directory,.last_seen_ms=100-p,.source="live",.exists=true});
    snapshot.clipboard_items.push_back({.ref="clip_current",.mime_types={"text/plain"},.kind=pastit::ContentKind::Text,.preview=std::string(900,'x'),.size_bytes=900,.captured_at_ms=1000});
    for(int i=0;i<26;++i){pastit::ActionInstance action;action.id="live_action_"+std::to_string(i);action.kind=i%2==0?pastit::ActionKind::TransformText:pastit::ActionKind::SaveTextFile;action.source_ref="clip_current";action.target_ref=i%2?"path_"+std::to_string(i%6):"";action.label="candidate "+std::to_string(i);action.description=std::string(400,'d');action.enabled=true;snapshot.available_actions.push_back(std::move(action));}
    return {.protocol_version=1,.request_id="req_live_bounded",.snapshot=std::move(snapshot)};
}

}  // namespace

int main() {
    load_dotenv_if_present();
    const auto endpoint = pastit::DjevClient::endpoint_from_env();
    std::cout << "endpoint=" << sanitized_endpoint(endpoint) << "\n";
    std::cout << "model=" << pastit::DjevClient::model_from_env() << "\n";

    pastit::DjevClient client(endpoint, pastit::DjevClient::model_from_env());
    auto text_request = make_request("req_live_text", pastit::ContentKind::Text, "clip_text", "hello local djev");
    append_historical_items(text_request, 49);
    const auto text_payload = pastit::DjevClient::build_payload(text_request, pastit::DjevClient::model_from_env());
    if (text_payload.find("MUST_NOT_REACH_DJEV_HISTORY_") != std::string::npos) {
        std::cerr << "text_catalog failed: historical clipboard content leaked into the live request\n";
        return 1;
    }
    const auto json_request = make_request("req_live_json", pastit::ContentKind::Json, "clip_json", "{\"a\":1,\"b\":[true,null]}");
    const auto image_request = make_request("req_live_image", pastit::ContentKind::Image, "clip_image", "image bytes (1024 bytes)");
    const auto path_request = make_request("req_live_path", pastit::ContentKind::Path, "clip_path", "/tmp/example.txt");

    const bool text_ok = run_case("text_catalog", text_request, client);
    const bool json_ok = run_case("json_catalog", json_request, client);
    const bool image_ok = run_case("image_catalog", image_request, client);
    const bool path_ok = run_case("path_catalog", path_request, client);
    const auto bounded_request=make_bounded_request();
    std::cout<<"bounded_catalog candidates="<<bounded_request.snapshot.available_actions.size()<<"\n";
    const bool bounded_ok=run_case("bounded_26_catalog",bounded_request,client);
    // A learned habit for a non-default action must be accepted by Djev.
    auto habit_request = make_request("req_live_habit", pastit::ContentKind::Json, "clip_habit", "{\"a\":1}");
    const auto& habit_actions = habit_request.snapshot.available_actions;
    if (!habit_actions.empty()) habit_request.snapshot.usage_hints.push_back({habit_actions.back().id, 0.8, 12});
    const bool habit_ok = run_case("usage_habits", habit_request, client);
    if (habit_ok && !habit_actions.empty()) std::cout << "usage_habits hinted=" << habit_actions.back().id << "\n";
    // An uncertain local shape adds a content_type question; the live model
    // must answer both questions.
    auto shape_request = make_request("req_live_shape", pastit::ContentKind::Text, "clip_shape", "a;1\nb;2");
    shape_request.snapshot.profile = pastit::ClipboardProfileHint{
        .shape = "csv", .confidence = 0.5, .lines = 2, .columns = 2, .header = false, .tags = {},
        .alternatives = {{"csv", "separated table rows"}, {"prose", "natural-language text"}, {"key_value", "key: value settings"}}};
    pastit::DjevClient shape_client(endpoint, pastit::DjevClient::model_from_env());
    const auto shape_response = shape_client.decide(shape_request);
    const bool shape_ok = shape_response.valid && !shape_response.content_type.empty();
    std::cout << "content_type " << (shape_ok ? "ok" : "failed") << ": " << shape_response.content_type << " ("
              << shape_response.content_type_confidence << ") choice=" << shape_response.choice
              << (shape_response.valid ? "" : " error=" + shape_response.error) << "\n";
    return text_ok && json_ok && image_ok && path_ok && bounded_ok && habit_ok && shape_ok ? 0 : 1;
}
