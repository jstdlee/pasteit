#include "ui/decision_future_slot.hpp"

#include <cassert>
#include <chrono>
#include <future>
#include <string>

int main() {
    using namespace pastit;
    using namespace std::chrono_literals;

    DecisionFutureSlot slot;
    std::promise<void> release_old;
    auto release = release_old.get_future().share();
    slot.replace(std::async(std::launch::async, [release] {
        release.wait();
        DecisionResponse response;
        response.request_id = "old";
        return response;
    }));

    auto replacement = std::async(std::launch::async, [&slot] {
        slot.replace(std::async(std::launch::async, [] {
            DecisionResponse response;
            response.request_id = "new";
            return response;
        }));
    });
    const bool replaced_without_waiting = replacement.wait_for(250ms) == std::future_status::ready;
    release_old.set_value();
    replacement.wait();
    assert(replaced_without_waiting);

    slot.reap();
    assert(slot.pending());
    slot.wait_all();
    auto selected = slot.take_ready();
    assert(selected.has_value());
    assert(selected->request_id == "new");
    assert(!slot.pending());
}
