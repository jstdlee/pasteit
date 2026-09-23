#pragma once

#include "app/renderer_result_state.hpp"
#include "ui/multi_viewport.hpp"

namespace pastit {

FastActionPanelModel build_mermaid_preview_panel_model(const RendererResultState& state);

#if defined(PASTIT_HAS_DESKTOP_DEPS)
void draw_mermaid_preview_panel(const RendererResultState& state, RendererPreviewPanelState& panel,
                                bool& open, bool& focus_pending,
                                unsigned int texture_id, int texture_width, int texture_height,
                                const std::function<bool(const std::filesystem::path&)>& open_path);
#endif

}  // namespace pastit
