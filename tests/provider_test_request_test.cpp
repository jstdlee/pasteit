#include "ui/provider_test_request.hpp"
#include "djev/djev_client.hpp"
#include "util/json.hpp"

#include <cassert>

int main() {
    const auto request = pastit::provider_test_request();
    assert(request.snapshot.available_actions.size() >= 2);
    assert(request.snapshot.available_actions[0].id != request.snapshot.available_actions[1].id);
    for (const auto& action : request.snapshot.available_actions) {
        assert(!action.label.empty());
        assert(!action.description.empty());
    }

    const auto payload = pastit::parse_json(pastit::DjevClient::build_payload(request, "test-model"));
    assert(payload.has_value());
    const auto* questions = payload->get("questions");
    assert(questions != nullptr);
    const auto* best_action = questions->get("best_action");
    assert(best_action != nullptr);
    const auto* criteria = best_action->get("criteria");
    assert(criteria != nullptr && criteria->object() != nullptr);
    const auto& choices = *criteria->object();
    assert(choices.size() == 2 && "Settings Jev test must send two named choices, not an empty map");
    assert(choices.at("settings_test_action").string() != nullptr);
    assert(choices.at("settings_test_alternative").string() != nullptr);
    assert(!choices.at("settings_test_action").string()->empty());
    assert(!choices.at("settings_test_alternative").string()->empty());
}
