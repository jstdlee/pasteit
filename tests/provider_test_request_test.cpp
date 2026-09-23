#include "ui/provider_test_request.hpp"

#include <cassert>

int main() {
    const auto request = pastit::provider_test_request();
    assert(request.snapshot.available_actions.size() >= 2);
    assert(request.snapshot.available_actions[0].id != request.snapshot.available_actions[1].id);
    for (const auto& action : request.snapshot.available_actions) {
        assert(!action.label.empty());
        assert(!action.description.empty());
    }
}
