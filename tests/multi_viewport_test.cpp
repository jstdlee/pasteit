#include "ui/multi_viewport.hpp"

#include <cassert>

int main() {
    const auto descriptor = pasteit::independent_panel_viewport("Example##panel", {640.0F, 420.0F});
    assert(descriptor.independent);
    assert(descriptor.title == "Example##panel");
    assert(descriptor.initial_width == 640.0F);
    assert(descriptor.initial_height == 420.0F);

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    auto& style = ImGui::GetStyle();

    pasteit::configure_independent_viewports(io, style);
    assert((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0);

    const auto window_class = pasteit::independent_window_class();
    assert((window_class.ViewportFlagsOverrideSet & ImGuiViewportFlags_NoAutoMerge) != 0);

    ImGui::DestroyContext();
#endif
}
