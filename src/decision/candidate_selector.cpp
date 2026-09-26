#include "decision/candidate_selector.hpp"
#include "decision/action_category.hpp"

#include <algorithm>
#include <set>
#include <tuple>

namespace pasteit {
namespace {
std::int64_t source_time(const ActionInstance& action, const DecisionSnapshot& snapshot) {
    const auto it=std::find_if(snapshot.clipboard_items.begin(),snapshot.clipboard_items.end(),[&](const auto&i){return i.ref==action.source_ref;});
    return it==snapshot.clipboard_items.end()?0:it->captured_at_ms;
}
std::int64_t target_time(const ActionInstance& action, const DecisionSnapshot& snapshot) {
    const auto it=std::find_if(snapshot.recent_paths.begin(),snapshot.recent_paths.end(),[&](const auto&p){return p.ref==action.target_ref;});
    return it==snapshot.recent_paths.end()?0:it->last_seen_ms;
}
bool default_target(const ActionInstance& action,const DecisionSnapshot& snapshot){
    if(action.target_ref=="default"||action.target_ref=="default_text"||action.target_ref=="default_image")return true;
    const auto it=std::find_if(snapshot.recent_paths.begin(),snapshot.recent_paths.end(),[&](const auto&p){return p.ref==action.target_ref;});
    return it!=snapshot.recent_paths.end()&&it->source.starts_with("configured");
}
}
bool is_current_source_action(const ActionInstance& action,const DecisionSnapshot& snapshot){
    return !snapshot.clipboard_items.empty()&&action.source_ref==snapshot.clipboard_items.front().ref;
}
ActionCatalog select_djev_candidates(const ActionCatalog& full,const DecisionSnapshot& snapshot,std::size_t limit,
                                     const ActionRankingContext& context){
    std::vector<ActionInstance> ordered;
    for(const auto& action:full.actions) if(action.enabled&&is_current_source_action(action,snapshot)) ordered.push_back(action);
    const std::string current=snapshot.clipboard_items.empty()?std::string{}:snapshot.clipboard_items.front().ref;
    const auto priority = [&](const ActionInstance& action) {
        // Explicit, bounded nudges share one additive scale with the small
        // contextual defaults; no non-zero detector match is an absolute gate.
        const double source_prior = action.source_ref == current ? 0.01 : 0.0;
        const double target_prior = default_target(action, snapshot) ? 0.02 : 0.0;
        const double recency_prior = target_time(action, snapshot) > 0 ? 0.002 : 0.0;
        const auto candidate = context.candidate_priority_by_id.find(action.id);
        return action_ranking_bonus(action, context) + action_specificity_prior(action.kind) + source_prior +
               target_prior + recency_prior +
               (candidate == context.candidate_priority_by_id.end() ? 0.0 : candidate->second);
    };
    std::stable_sort(ordered.begin(),ordered.end(),[&](const auto& a,const auto& b){
        return std::tuple{priority(a),source_time(a,snapshot),target_time(a,snapshot),std::string_view{a.id}} >
               std::tuple{priority(b),source_time(b,snapshot),target_time(b,snapshot),std::string_view{b.id}};
    });
    ActionCatalog selected; selected.actions.reserve(std::min(limit,ordered.size()));
    std::set<ActionKind> kinds; std::set<std::string> ids;
    for(const auto& action:ordered){
        if(selected.actions.size()>=limit) break;
        if(kinds.insert(action.kind).second && ids.insert(action.id).second) selected.actions.push_back(action);
    }
    for(const auto& action:ordered){
        if(selected.actions.size()>=limit) break;
        if(ids.insert(action.id).second) selected.actions.push_back(action);
    }
    return selected;
}
}  // namespace pasteit
