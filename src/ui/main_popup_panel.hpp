#pragma once
#include "actions/action_catalog.hpp"
#include "ui/popup.hpp"
#include <string_view>
#include <vector>
namespace pasteit {
struct MainPopupPanelState{
    std::vector<PopupRow> smart_rows;
    std::vector<PopupRow> prompt_rows;
    bool fallback=false;
    std::string banner;
    void clamp_rows();
};
std::vector<ActionInstance> current_prompt_actions(const ActionCatalog& catalog, std::string_view current_ref);
}
