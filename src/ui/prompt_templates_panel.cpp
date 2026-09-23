#include "ui/prompt_templates_panel.hpp"

#include "config/prompt_template_service.hpp"

#include <algorithm>

namespace pastit {
namespace {

std::vector<std::string> prompt_template_columns() {
    return {"Enabled", "Name", "Temperature", "Built-in/Custom", "Actions"};
}

std::vector<std::string> prompt_template_actions() {
    return {"View", "Edit", "Duplicate", "Delete"};
}

std::vector<std::string> prompt_template_tooltips() {
    return {
        "View prompt template",
        "Edit prompt template",
        "Duplicate prompt template",
        "Delete prompt template",
    };
}

std::optional<PromptTemplate> find_template(const PromptTemplatesPanelModel& model, const std::string& template_id) {
    const auto row = std::find_if(model.rows.begin(), model.rows.end(), [&](const PromptTemplateRow& value) {
        return value.id == template_id;
    });
    if (row == model.rows.end()) {
        return std::nullopt;
    }

    PromptTemplate prompt;
    prompt.id = row->id;
    prompt.name = row->name;
    prompt.system_prompt = row->system_prompt;
    prompt.temperature = row->temperature;
    prompt.enabled = row->enabled;
    prompt.built_in = row->built_in;
    return prompt;
}

PromptTemplateCommandResult missing_template(PromptTemplateCommand command, const std::string& template_id) {
    PromptTemplateCommandResult result;
    result.command = command;
    result.template_id = template_id;
    result.error = "Template not found.";
    return result;
}

void clear_modal(PromptTemplatesPanelModel& model) {
    model.modal = PromptTemplateModal::None;
    model.draft.reset();
    model.frozen_delete_template_id.clear();
    model.error.clear();
}

}  // namespace

PromptTemplatesPanelModel build_prompt_templates_panel_model(const std::vector<PromptTemplate>& templates) {
    PromptTemplatesPanelModel model;
    model.columns = prompt_template_columns();
    model.rows.reserve(templates.size());
    for (const auto& prompt : templates) {
        model.rows.push_back({
            .id = prompt.id,
            .enabled = prompt.enabled,
            .name = prompt.name,
            .system_prompt = prompt.system_prompt,
            .temperature = prompt.temperature,
            .built_in = prompt.built_in,
            .columns = model.columns,
            .actions = prompt_template_actions(),
            .tooltips = prompt_template_tooltips(),
        });
    }
    if (!model.rows.empty()) {
        model.selected_template_id = model.rows.front().id;
    }
    return model;
}

PromptTemplateCommandResult view_prompt_template(PromptTemplatesPanelModel& model, const std::string& template_id) {
    const auto prompt = find_template(model, template_id);
    if (!prompt) {
        return missing_template(PromptTemplateCommand::View, template_id);
    }
    model.modal = PromptTemplateModal::View;
    model.draft = *prompt;
    model.selected_template_id = template_id;
    model.error.clear();
    PromptTemplateCommandResult result;
    result.command = PromptTemplateCommand::View;
    result.template_id = template_id;
    result.frozen_draft = model.draft;
    return result;
}

PromptTemplateCommandResult begin_prompt_template_edit(PromptTemplatesPanelModel& model, const std::string& template_id) {
    const auto prompt = find_template(model, template_id);
    if (!prompt) {
        return missing_template(PromptTemplateCommand::BeginEdit, template_id);
    }
    model.modal = PromptTemplateModal::Edit;
    model.draft = *prompt;
    model.selected_template_id = template_id;
    model.error.clear();
    PromptTemplateCommandResult result;
    result.command = PromptTemplateCommand::BeginEdit;
    result.template_id = template_id;
    result.frozen_draft = model.draft;
    return result;
}

PromptTemplateCommandResult save_prompt_template_edit(PromptTemplatesPanelModel& model, PromptTemplateService& service) {
    if (!model.draft) {
        PromptTemplateCommandResult result;
        result.command = PromptTemplateCommand::SaveEdit;
        result.error = "No prompt draft is open.";
        return result;
    }

    std::string error;
    const auto draft = *model.draft;
    if (!service.update(draft.id, draft.name, draft.system_prompt, draft.temperature, error) ||
        !service.set_enabled(draft.id, draft.enabled, error)) {
        model.error = error;
        PromptTemplateCommandResult result;
        result.command = PromptTemplateCommand::SaveEdit;
        result.template_id = draft.id;
        result.frozen_draft = draft;
        result.error = error;
        return result;
    }

    clear_modal(model);
    model.selected_template_id = draft.id;
    PromptTemplateCommandResult result;
    result.command = PromptTemplateCommand::SaveEdit;
    result.template_id = draft.id;
    result.frozen_draft = draft;
    return result;
}

PromptTemplateCommandResult duplicate_prompt_template(PromptTemplatesPanelModel& model, PromptTemplateService& service,
                                                      const std::string& template_id) {
    std::string error;
    const auto duplicate = service.duplicate(template_id, error);
    if (!duplicate) {
        model.error = error;
        PromptTemplateCommandResult result;
        result.command = PromptTemplateCommand::Duplicate;
        result.template_id = template_id;
        result.error = error;
        return result;
    }

    model.modal = PromptTemplateModal::Edit;
    model.draft = *duplicate;
    model.selected_template_id = duplicate->id;
    model.frozen_delete_template_id.clear();
    model.error.clear();
    PromptTemplateCommandResult result;
    result.command = PromptTemplateCommand::Duplicate;
    result.template_id = duplicate->id;
    result.frozen_draft = model.draft;
    return result;
}

PromptTemplateCommandResult begin_prompt_template_delete(PromptTemplatesPanelModel& model, const std::string& template_id) {
    if (!find_template(model, template_id)) {
        return missing_template(PromptTemplateCommand::BeginDelete, template_id);
    }

    model.modal = PromptTemplateModal::Delete;
    model.draft.reset();
    model.frozen_delete_template_id = template_id;
    model.error.clear();
    PromptTemplateCommandResult result;
    result.command = PromptTemplateCommand::BeginDelete;
    result.template_id = template_id;
    return result;
}

PromptTemplateCommandResult confirm_prompt_template_delete(PromptTemplatesPanelModel& model, PromptTemplateService& service) {
    if (model.frozen_delete_template_id.empty()) {
        PromptTemplateCommandResult result;
        result.command = PromptTemplateCommand::ConfirmDelete;
        result.error = "No prompt template is pending deletion.";
        return result;
    }

    const auto frozen_id = model.frozen_delete_template_id;
    std::string error;
    if (!service.erase(frozen_id, error)) {
        model.error = error;
        PromptTemplateCommandResult result;
        result.command = PromptTemplateCommand::ConfirmDelete;
        result.template_id = frozen_id;
        result.error = error;
        return result;
    }

    clear_modal(model);
    PromptTemplateCommandResult result;
    result.command = PromptTemplateCommand::ConfirmDelete;
    result.template_id = frozen_id;
    return result;
}

PromptTemplateCommandResult cancel_prompt_template_modal(PromptTemplatesPanelModel& model) {
    const auto template_id = model.draft ? model.draft->id : model.frozen_delete_template_id;
    const auto draft = model.draft;
    clear_modal(model);
    PromptTemplateCommandResult result;
    result.command = PromptTemplateCommand::CancelModal;
    result.template_id = template_id;
    result.frozen_draft = draft;
    return result;
}

}  // namespace pastit
