#include "ui/recent_paths_panel.hpp"

#include <algorithm>
#include <cassert>
#include <string>
#include <utility>

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
#include <imgui.h>
#include <imgui_internal.h>

int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(720.0F, 560.0F);
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    pasteit::RecentPathsModel model;
    for (int index = 0; index < 10; ++index) {
        pasteit::RecentPathRow row;
        row.ref = "path-" + std::to_string(index);
        row.type_label = "folder";
        row.display_path = "/tmp/path-" + std::to_string(index);
        row.source = "same source";
        row.last_seen_label = "same time";
        model.rows.push_back(std::move(row));
    }
    pasteit::RecentPathsState state;

    // Scan across cells without clicking. Dear ImGui counts all visible items
    // sharing the ID hovered in the prior frame, even those in other rows.
    int hovered_widgets = 0;
    int maximum_matching_items = 0;
    for (float x : {28.0F, 140.0F, 400.0F}) {
        for (float y = 25.0F; y < 325.0F; y += 8.0F) {
            io.AddMousePosEvent(x, y);
            for (int frame = 0; frame < 2; ++frame) {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
                ImGui::SetNextWindowSize(io.DisplaySize);
                ImGui::Begin("Path History", nullptr, ImGuiWindowFlags_NoDecoration);
                pasteit::render_recent_paths_panel(state, model, pasteit::UiLanguage::English);
                ImGui::End();
                ImGui::Render();
            }
            const auto& context = *ImGui::GetCurrentContext();
            if (context.HoveredIdPreviousFrame != 0) ++hovered_widgets;
            maximum_matching_items = std::max(maximum_matching_items, context.HoveredIdPreviousFrameItemCount);
        }
    }
    assert(hovered_widgets > 0 && "the test must hover path-history widgets");
    assert(maximum_matching_items == 1 && "path-history row widgets must have distinct IDs");
    ImGui::DestroyContext();
}
#else
int main() {}
#endif
