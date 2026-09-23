#include "ui/main_popup_panel.hpp"
namespace pastit {
void MainPopupPanelState::clamp_rows(){if(smart_rows.size()>kDisplayedActionLimit)smart_rows.resize(kDisplayedActionLimit);}

std::vector<ActionInstance> current_prompt_actions(const ActionCatalog& catalog, std::string_view current_ref) {
    std::vector<ActionInstance> result;
    for (const auto& action : catalog.actions) {
        if (action.kind == ActionKind::TransformText && action.enabled && action.source_ref == current_ref) {
            result.push_back(action);
        }
    }
    return result;
}
}
