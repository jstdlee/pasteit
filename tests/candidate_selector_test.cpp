#include "decision/candidate_selector.hpp"
#include "decision/fallback_ranker.hpp"

#include <cassert>
#include <set>

int main() {
    pastit::DecisionSnapshot snapshot;
    snapshot.clipboard_items = {
        {.ref="current", .captured_at_ms=200},
    };
    pastit::ActionCatalog full;
    for (int i=0; i<34; ++i) {
        full.actions.push_back({.id="action-"+std::to_string(i),
                                .kind=static_cast<pastit::ActionKind>(i % 10),
                                .source_ref=i < 3 ? "current" : "history",
                                .target_ref=(i%3==0 ? "temp" : "path-"+std::to_string(i%4)),
                                .label="Action "+std::to_string(i),
                                .description="candidate",
                                .enabled=i != 33});
    }
    const auto selected = pastit::select_djev_candidates(full, snapshot);
    assert(selected.actions.size() == 3);
    std::set<std::string> ids;
    for (const auto& action : selected.actions) {
        assert(action.enabled);
        assert(action.source_ref == "current");
        assert(full.find(action.id).has_value());
        assert(ids.insert(action.id).second);
    }
    assert(selected.actions.front().source_ref == "current");
    const auto fallback = pastit::rank_fallback(selected, snapshot, 5);
    assert(fallback.size() == 3);
    for (const auto& ranked : fallback) {
        assert(ranked.action.source_ref == "current");
    }
    assert(fallback.front().probability > fallback.back().probability);
}
