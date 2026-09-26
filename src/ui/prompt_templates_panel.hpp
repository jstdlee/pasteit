#pragma once

#include "config/app_settings.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pasteit {

class PromptTemplateService;

enum class PromptTemplateCommand {
    None,
    View,
    BeginEdit,
    SaveEdit,
    Duplicate,
    BeginDelete,
    ConfirmDelete,
    CancelModal,
};

enum class PromptTemplateModal {
    None,
    View,
    Edit,
    Delete,
};

struct PromptTemplateRow {
    std::string id;
    bool enabled = false;
    std::string name;
    std::string system_prompt;
    double temperature = 0.0;
    bool built_in = false;
    std::vector<std::string> columns;
    std::vector<std::string> actions;
    std::vector<std::string> tooltips;
};

struct PromptTemplatesPanelModel {
    std::vector<PromptTemplateRow> rows;
    std::vector<std::string> columns;
    PromptTemplateModal modal = PromptTemplateModal::None;
    std::optional<PromptTemplate> draft;
    std::string selected_template_id;
    std::string frozen_delete_template_id;
    std::string error;
};

struct PromptTemplateCommandResult {
    PromptTemplateCommand command = PromptTemplateCommand::None;
    std::string template_id;
    std::optional<PromptTemplate> frozen_draft;
    std::string error;
};

PromptTemplatesPanelModel build_prompt_templates_panel_model(const std::vector<PromptTemplate>& templates);
PromptTemplateCommandResult view_prompt_template(PromptTemplatesPanelModel& model, const std::string& template_id);
PromptTemplateCommandResult begin_prompt_template_edit(PromptTemplatesPanelModel& model, const std::string& template_id);
PromptTemplateCommandResult save_prompt_template_edit(PromptTemplatesPanelModel& model, PromptTemplateService& service);
PromptTemplateCommandResult duplicate_prompt_template(PromptTemplatesPanelModel& model, PromptTemplateService& service,
                                                      const std::string& template_id);
PromptTemplateCommandResult begin_prompt_template_delete(PromptTemplatesPanelModel& model, const std::string& template_id);
PromptTemplateCommandResult confirm_prompt_template_delete(PromptTemplatesPanelModel& model, PromptTemplateService& service);
PromptTemplateCommandResult cancel_prompt_template_modal(PromptTemplatesPanelModel& model);

}  // namespace pasteit
