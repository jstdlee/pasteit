#pragma once

#include "app/contact_result_state.hpp"
#include "ui/multi_viewport.hpp"

namespace pastit {

FastActionPanelModel build_contact_result_panel_model(const ContactResultState& state);

#if defined(PASTIT_HAS_DESKTOP_DEPS)
void draw_contact_result_panel(const ContactResultState& state, bool& open, bool& focus_pending);
#endif

}  // namespace pastit
