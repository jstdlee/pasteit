#include "decision/action_preference.hpp"
#include "decision/candidate_selector.hpp"
#include "djev/decision_ranker.hpp"

#include <cassert>

int main() {
    using namespace pastit;

    ActionInstance annotate_image;
    annotate_image.id = "annotate-current-image";
    annotate_image.kind = ActionKind::AnnotateImage;
    annotate_image.source_ref = "clip-image";
    annotate_image.enabled = true;
    ActionInstance save_image = annotate_image;
    save_image.id = "save-current-image";
    save_image.kind = ActionKind::SaveImageFile;

    // Usage bonuses are bounded and combine with local detector nudges.
    ActionRankingContext context;
    context.input_kind = ContentKind::Image;
    context.usage_bonus_by_id[annotate_image.id] = 5.0;
    context.local_action_bonus[ActionKind::AnnotateImage] = 5.0;
    assert(action_ranking_bonus(annotate_image, context) <= 0.25);
    assert(action_ranking_bonus(save_image, context) == 0.0);

    ActionRankingContext learned;
    learned.usage_bonus_by_id[annotate_image.id] = 0.12;
    ActionCatalog image_catalog{{annotate_image, save_image}};
    DecisionResponse image_response;
    image_response.valid = true;
    image_response.choice = save_image.id;
    image_response.probabilities[annotate_image.id] = 0.60;
    image_response.probabilities[save_image.id] = 0.61;
    const auto ranked = rank_top_actions(image_response, image_catalog, 5, learned);
    assert(ranked.front().action.kind == ActionKind::AnnotateImage);
    assert(ranked.front().probability <= 0.73);

    ActionInstance destination_a;
    destination_a.kind = ActionKind::SaveTextFile;
    destination_a.id = "save-a";
    destination_a.source_ref = "clip-current";
    destination_a.target_ref = "path-a";
    destination_a.enabled = true;
    ActionInstance destination_b = destination_a;
    destination_b.id = "save-b";
    destination_b.target_ref = "path-b";
    ActionRankingContext destination_context;
    destination_context.usage_bonus_by_id[destination_b.id] = 0.05;
    DecisionSnapshot snapshot;
    snapshot.clipboard_items.push_back({.ref = "clip-current", .kind = ContentKind::Text, .preview = "text"});
    const auto selected = select_djev_candidates(ActionCatalog{{destination_a, destination_b}}, snapshot, 1,
                                                 destination_context);
    assert(selected.actions.size() == 1);
    assert(selected.actions.front().target_ref == "path-b");
}
