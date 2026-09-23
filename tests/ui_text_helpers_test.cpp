#include "ui/imgui_widgets.hpp"
#include "platform/platform_services.hpp"

#include <cassert>
#include <string>
#include <utility>

#if defined(PASTIT_HAS_DESKTOP_DEPS)
#include <imgui.h>

namespace {
class ClipboardPlatform final : public pastit::PlatformServices {
public:
    void apply_settings(const pastit::AppSettings&) override {}
    std::optional<pastit::ClipboardCapture> poll_clipboard() override { return std::nullopt; }
    void process_events() override {}
    bool publish_text(std::string_view) override { return true; }
    bool publish_image(const std::vector<std::byte>&, std::string_view) override { return true; }
    pastit::PlatformFocusContext focused_context() override { return {}; }
    std::vector<pastit::PlatformRecentPath> recent_paths() override { return {}; }
    bool register_global_shortcut() override { return false; }
    bool global_shortcut_activated() override { return false; }
    bool restore_focus_and_paste(const pastit::PlatformFocusContext&) override { return false; }
    bool open_path(const std::filesystem::path&) override { return false; }
    bool open_uri(std::string_view) override { return false; }
    bool copy_text(std::string_view text) override { copied = std::string{text}; return true; }
    bool move_popup_by(int, int) override { return false; }
    bool set_popup_opacity(float) override { return false; }
    std::vector<std::filesystem::path> preferred_ui_fonts() override { return {}; }
    std::optional<std::filesystem::path> choose_directory(const std::filesystem::path&) override { return std::nullopt; }
    std::optional<std::string> owned_clipboard_text() const override { return owned; }
    std::string copied;
    std::optional<std::string> owned;
};
}
#endif

int main() {
    assert(pastit::multiline_editor_row_count("") == 1);
    assert(pastit::multiline_editor_row_count("one\ntwo\nthree") == 3);
    assert(pastit::multiline_editor_visible_rows(std::string(100, 'x')) == 3);
    assert(pastit::multiline_editor_visible_rows("1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11") == 10);

#if defined(PASTIT_HAS_DESKTOP_DEPS)
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(720.0F, 560.0F);
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    std::string value = "/home/user/Pictures";
    const auto draw_input = [&] {
        ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoDecoration);
        pastit::input_text_string("Default image directory", value);
        const bool active = ImGui::IsItemActive();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::End();
        return std::pair{active, ImVec2((min.x + max.x) / 2.0F, (min.y + max.y) / 2.0F)};
    };
    ImGui::NewFrame();
    const auto [initial_active, input_center] = draw_input();
    assert(!initial_active);
    ImGui::Render();

    io.AddMousePosEvent(input_center.x, input_center.y);
    io.AddMouseButtonEvent(0, true);
    ImGui::NewFrame();
    const auto [clicked_input_active, ignored_center] = draw_input();
    (void)ignored_center;
    assert(clicked_input_active && "settings input must receive clicks inside its text box");
    ImGui::Render();

    io.AddInputCharacter('a');
    io.AddMouseButtonEvent(0, false);
    ImGui::NewFrame();
    draw_input();
    ImGui::Render();
    assert(value != "/home/user/Pictures" && "typing must update the settings input");

    ClipboardPlatform clipboard;
    auto& platform_io = ImGui::GetPlatformIO();
    platform_io.Platform_GetClipboardTextFn = [](ImGuiContext*) { return "external text"; };
    pastit::install_imgui_clipboard_bridge(clipboard);
    ImGui::SetClipboardText("Jev API endpoint");
    assert(clipboard.copied == "Jev API endpoint" && "copy must use PasteIt's clipboard owner");
    clipboard.owned = "http://127.0.0.1:8011";
    assert(std::string{ImGui::GetClipboardText()} == *clipboard.owned && "paste must read app-owned text without GLFW");
    clipboard.owned.reset();
    assert(std::string{ImGui::GetClipboardText()} == "external text");
    ImGui::DestroyContext();
#endif
}
