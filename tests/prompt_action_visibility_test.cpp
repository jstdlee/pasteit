#include "actions/action_catalog.hpp"
#include "decision/candidate_selector.hpp"
#include "ui/main_popup_panel.hpp"

#include <cassert>
#include <string>
#include <vector>

namespace {

pasteit::DecisionSnapshot snapshot_for(pasteit::ContentKind kind) {
    pasteit::DecisionSnapshot snapshot;
    snapshot.clipboard_items.push_back({
        .ref = "current",
        .mime_types = {"text/plain"},
        .kind = kind,
        .preview = kind == pasteit::ContentKind::Url ? "https://example.com" : "hello",
        .size_bytes = 5,
        .captured_at_ms = 100,
    });
    return snapshot;
}

std::vector<pasteit::PromptTemplate> templates() {
    std::vector<pasteit::PromptTemplate> out;
    for (int index = 0; index < 7; ++index) {
        out.push_back({
            .id = "prompt-" + std::to_string(index),
            .name = "Prompt " + std::to_string(index),
            .system_prompt = "Transform {text}",
            .temperature = 0.2,
            .enabled = true,
        });
    }
    out.push_back({
        .id = "disabled",
        .name = "Disabled",
        .system_prompt = "Disabled {text}",
        .temperature = 0.2,
        .enabled = false,
    });
    return out;
}

}  // namespace

int main() {
    using namespace pasteit;

    ProviderSettings provider{.endpoint = "http://localhost/v1/chat/completions", .model_id = "model"};
    const auto catalog = build_catalog(snapshot_for(ContentKind::Text), templates(), provider);
    const auto djev_top_five = select_djev_candidates(catalog, snapshot_for(ContentKind::Text), 5);
    const auto prompt_actions = current_prompt_actions(catalog, "current");

    assert(djev_top_five.actions.size() == 5);
    assert(prompt_actions.size() == 7);
    assert(prompt_actions.front().id == "a_transform_text_current_prompt_0");
    assert(prompt_actions.back().id == "a_transform_text_current_prompt_6");
    for (const auto& action : prompt_actions) {
        assert(action.kind == ActionKind::TransformText);
        assert(action.source_ref == "current");
        assert(action.enabled);
        assert(action.parameters.at("template_id") != "disabled");
    }

    ActionCatalog mixed = catalog;
    ActionInstance old_prompt = prompt_actions.front();
    old_prompt.id = "old-source-prompt";
    old_prompt.source_ref = "history";
    mixed.actions.push_back(old_prompt);
    ActionInstance disabled_prompt = prompt_actions.front();
    disabled_prompt.id = "disabled-current-prompt";
    disabled_prompt.enabled = false;
    mixed.actions.push_back(disabled_prompt);
    assert(current_prompt_actions(mixed, "current").size() == 7);

    assert(current_prompt_actions(build_catalog(snapshot_for(ContentKind::Url), templates(), provider), "current").empty());
    assert(current_prompt_actions(build_catalog(snapshot_for(ContentKind::Image), templates(), provider), "current").empty());
    assert(current_prompt_actions(build_catalog(snapshot_for(ContentKind::Path), templates(), provider), "current").empty());
}
