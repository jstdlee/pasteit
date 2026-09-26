#pragma once

#include "app/network_report_state.hpp"
#include "ui/multi_viewport.hpp"

namespace pasteit {

FastActionPanelModel build_network_report_panel_model(const NetworkReportState& state);

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
void draw_network_report_panel(const NetworkReportState& state, bool& open, bool& focus_pending);
#endif

}  // namespace pasteit
