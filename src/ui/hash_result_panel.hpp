#pragma once

#include "app/hash_result_state.hpp"
#include "ui/multi_viewport.hpp"

namespace pastit {

FastActionPanelModel build_hash_result_panel_model(const HashResultState& state);

#if defined(PASTIT_HAS_DESKTOP_DEPS)
void draw_hash_result_panel(const HashResultState& state, bool& open, bool& focus_pending);
#endif

}  // namespace pastit
