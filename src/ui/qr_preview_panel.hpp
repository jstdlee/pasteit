#pragma once

#include "app/renderer_result_state.hpp"
#include "ui/multi_viewport.hpp"

namespace pastit {

FastActionPanelModel build_qr_preview_panel_model(const RendererResultState& state);

#if defined(PASTIT_HAS_DESKTOP_DEPS)
void draw_qr_preview_panel(const RendererResultState& state, RendererPreviewPanelState& panel,
                           bool& open, bool& focus_pending,
                           const std::function<bool(const std::filesystem::path&)>& open_path);
#endif

}  // namespace pastit
