#include "config/prompt_template_service.hpp"
#include "ui/prompt_templates_panel.hpp"

#include <cassert>
#include <string>
#include <vector>

int main() {
    using namespace pastit;

    std::vector<PromptTemplate> templates{
        {.id = "builtin", .name = "Summarize", .system_prompt = "Summarize {text}", .temperature = 0.2, .enabled = true, .built_in = true},
        {.id = "custom", .name = "Polish", .system_prompt = "Polish {text}", .temperature = 0.4, .enabled = true, .built_in = false},
    };
    int next_id = 0;
    PromptTemplateService service(templates, [&] { return "duplicate-" + std::to_string(++next_id); });

    auto model = build_prompt_templates_panel_model(templates);
    assert(model.rows.size() == 2);
    assert(model.rows[0].columns.size() == 5);
    assert(model.rows[0].columns[0] == "Enabled");
    assert(model.rows[0].columns[1] == "Name");
    assert(model.rows[0].columns[2] == "Temperature");
    assert(model.rows[0].columns[3] == "Built-in/Custom");
    assert(model.rows[0].columns[4] == "Actions");

    auto view = view_prompt_template(model, "custom");
    assert(view.command == PromptTemplateCommand::View);
    assert(view.template_id == "custom");
    assert(view.frozen_draft.has_value());
    assert(view.frozen_draft->system_prompt == "Polish {text}");
    assert(model.modal == PromptTemplateModal::View);
    auto cancelled_view = cancel_prompt_template_modal(model);
    assert(cancelled_view.command == PromptTemplateCommand::CancelModal);
    assert(cancelled_view.template_id == "custom");
    assert(model.modal == PromptTemplateModal::None);

    auto edit = begin_prompt_template_edit(model, "custom");
    assert(edit.command == PromptTemplateCommand::BeginEdit);
    assert(edit.template_id == "custom");
    assert(edit.frozen_draft.has_value());
    assert(model.modal == PromptTemplateModal::Edit);
    model.draft->name = "Edited locally";
    cancel_prompt_template_modal(model);
    assert(templates[1].name == "Polish");
    assert(!model.draft.has_value());
    assert(model.modal == PromptTemplateModal::None);

    begin_prompt_template_edit(model, "custom");
    model.draft->name = "Polished";
    model.draft->system_prompt = "Polish carefully: {text}";
    model.draft->temperature = 0.6;
    model.draft->enabled = false;
    auto saved = save_prompt_template_edit(model, service);
    assert(saved.command == PromptTemplateCommand::SaveEdit);
    assert(saved.template_id == "custom");
    assert(saved.frozen_draft.has_value());
    const auto persisted = service.find("custom");
    assert(persisted.has_value());
    assert(persisted->name == "Polished");
    assert(persisted->system_prompt == "Polish carefully: {text}");
    assert(persisted->temperature == 0.6);
    assert(!persisted->enabled);
    assert(model.modal == PromptTemplateModal::None);

    auto duplicate = duplicate_prompt_template(model, service, "custom");
    assert(duplicate.command == PromptTemplateCommand::Duplicate);
    assert(duplicate.template_id == "duplicate-1");
    assert(duplicate.frozen_draft.has_value());
    assert(templates.size() == 3);
    assert(templates[2].id == "duplicate-1");
    assert(!templates[2].built_in);
    assert(model.modal == PromptTemplateModal::Edit);
    assert(model.draft->id == "duplicate-1");

    auto deleting = begin_prompt_template_delete(model, "custom");
    assert(deleting.command == PromptTemplateCommand::BeginDelete);
    assert(deleting.template_id == "custom");
    model.selected_template_id = "duplicate-1";
    auto confirmed = confirm_prompt_template_delete(model, service);
    assert(confirmed.command == PromptTemplateCommand::ConfirmDelete);
    assert(confirmed.template_id == "custom");
    assert(!service.find("custom").has_value());
    assert(service.find("duplicate-1").has_value());
    assert(model.modal == PromptTemplateModal::None);
}
