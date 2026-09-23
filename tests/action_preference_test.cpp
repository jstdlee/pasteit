#include "decision/action_preference.hpp"
#include "decision/candidate_selector.hpp"
#include "djev/decision_ranker.hpp"

#include <cassert>

namespace {
pastit::ActionInstance transform(std::string id, std::string template_id) {
    pastit::ActionInstance action;
    action.id = std::move(id);
    action.kind = pastit::ActionKind::TransformText;
    action.source_ref = "clip-current";
    action.parameters["template_id"] = std::move(template_id);
    action.enabled = true;
    return action;
}
}

int main() {
    using namespace pastit;

    const auto first = transform("request-a", "translate");
    const auto second = transform("request-b", "rewrite");
    assert(action_preference_key(first) != action_preference_key(second));

    ActionPreferenceWeights preferences;
    record_action_preference(preferences, second);
    const auto first_bonus = action_preference_bonus(preferences, second);
    assert(first_bonus > 0.0 && first_bonus < 0.2);
    for (int count = 0; count < 20; ++count) record_action_preference(preferences, second);
    assert(action_preference_bonus(preferences, second) <= 0.2);

    ActionInstance annotate_image;
    annotate_image.id = "annotate-current-image";
    annotate_image.kind = ActionKind::AnnotateImage;
    annotate_image.source_ref = "clip-image";
    ActionRankingContext image_after_save{
        .input_kind = ContentKind::Image,
        .djev_proposed_kind = ActionKind::SaveImageFile,
    };
    ActionPreferenceWeights contextual_preferences;
    for (int count = 0; count < 12; ++count) {
        record_action_preference(contextual_preferences, annotate_image, image_after_save);
    }
    const auto contextual_bonus = action_preference_bonus(contextual_preferences, annotate_image, image_after_save);
    const ActionRankingContext unrelated_text{
        .input_kind = ContentKind::Text,
        .djev_proposed_kind = ActionKind::SaveTextFile,
    };
    const ActionRankingContext image_with_other_proposal{
        .input_kind = ContentKind::Image,
        .djev_proposed_kind = ActionKind::AnnotateImage,
    };
    const ActionRankingContext same_proposal_different_input{
        .input_kind = ContentKind::Text,
        .djev_proposed_kind = ActionKind::SaveImageFile,
    };
    assert(contextual_bonus > action_preference_bonus(contextual_preferences, annotate_image, unrelated_text));
    assert(contextual_bonus > action_preference_bonus(contextual_preferences, annotate_image,
                                                       image_with_other_proposal));
    assert(contextual_bonus > action_preference_bonus(contextual_preferences, annotate_image,
                                                       same_proposal_different_input));
    assert(contextual_bonus <= 0.15);
    ActionInstance save_image = annotate_image;
    save_image.id = "save-current-image";
    save_image.kind = ActionKind::SaveImageFile;
    ActionCatalog image_catalog{{annotate_image, save_image}};
    DecisionResponse image_response;
    image_response.valid = true;
    image_response.choice = save_image.id;
    image_response.probabilities[annotate_image.id] = 0.60;
    image_response.probabilities[save_image.id] = 0.61;
    const auto contextual_ranked = rank_top_actions(image_response, image_catalog, 5,
                                                     contextual_preferences, image_after_save);
    assert(contextual_ranked.front().action.kind == ActionKind::AnnotateImage);
    assert(contextual_ranked.front().probability <= 0.76);

    ActionCatalog catalog;
    catalog.actions = {first, second};
    DecisionResponse response;
    response.valid = true;
    response.choice = first.id;
    response.probabilities[first.id] = 0.45;
    response.probabilities[second.id] = 0.43;
    const auto ranked = rank_top_actions(response, catalog, 5, preferences);
    assert(ranked.front().action.id == second.id);
    assert(ranked.front().probability > 0.43);

    ActionInstance destination_a;
    destination_a.kind = ActionKind::SaveTextFile;
    destination_a.id = "save-a";
    destination_a.source_ref = "clip-current";
    destination_a.target_ref = "path-a";
    destination_a.enabled = true;
    ActionInstance destination_b = destination_a;
    destination_b.id = "save-b";
    destination_b.target_ref = "path-b";
    assert(action_preference_key(destination_a) != action_preference_key(destination_b));
    ActionPreferenceWeights destination_preferences;
    record_action_preference(destination_preferences, destination_b);
    DecisionSnapshot snapshot;
    snapshot.clipboard_items.push_back({.ref="clip-current", .kind=ContentKind::Text, .preview="text"});
    const auto selected = select_djev_candidates(ActionCatalog{{destination_a, destination_b}}, snapshot, 1,
                                                 destination_preferences);
    assert(selected.actions.size() == 1);
    assert(selected.actions.front().target_ref == "path-b");
}
