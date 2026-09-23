#include "ui/recent_paths_panel.hpp"

#include <cassert>
#include <string>
#include <utility>

#if defined(PASTIT_HAS_DESKTOP_DEPS)
#include <imgui.h>

int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(720.0F, 400.0F);
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    pastit::RecentPathsModel model;
    for (const auto* type : {"file", "folder"}) {
        pastit::RecentPathRow row;
        row.ref = type;
        row.type = type;
        row.type_label = type;
        row.display_path = std::string{"/tmp/"} + type;
        model.rows.push_back(std::move(row));
    }
    pastit::RecentPathsState state;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Path History", nullptr, ImGuiWindowFlags_NoDecoration);
    pastit::render_recent_paths_panel(state, model, pastit::UiLanguage::English);
    ImGui::End();
    ImGui::Render();

    // In the Type column's data rows, icons use the accent color and there
    // should be no glyph vertices spelling out "file" or "folder".
    const ImU32 accent = ImGui::GetColorU32(ImGuiCol_CheckMark);
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    const ImVec2 white_uv = io.Fonts->TexUvWhitePixel;
    int file_accent_vertices = 0;
    int folder_accent_vertices = 0;
    int text_glyph_vertices = 0;
    const auto* draw_data = ImGui::GetDrawData();
    for (int list_index = 0; list_index < draw_data->CmdListsCount; ++list_index) {
        const auto* list = draw_data->CmdLists[list_index];
        for (const auto& vertex : list->VtxBuffer) {
            if (vertex.pos.x >= 80.0F || vertex.pos.y <= 25.0F || vertex.pos.y >= 80.0F) continue;
            if (vertex.col == accent) {
                if (vertex.pos.y < 50.0F) ++file_accent_vertices;
                else ++folder_accent_vertices;
            }
            if (vertex.pos.y >= 35.0F && vertex.col == text &&
                (vertex.uv.x != white_uv.x || vertex.uv.y != white_uv.y)) {
                ++text_glyph_vertices;
            }
        }
    }
    assert(file_accent_vertices > 0 && "file icon must draw in the Type column");
    assert(folder_accent_vertices > 0 && "folder icon must draw in the Type column");
    assert(text_glyph_vertices == 0 && "Type column must not render file/folder words");
    ImGui::DestroyContext();
}
#else
int main() {}
#endif
