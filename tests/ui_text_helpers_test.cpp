#include "ui/imgui_widgets.hpp"
#include "platform/platform_services.hpp"

#include <cassert>
#include <string>
#include <utility>

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
#include <imgui.h>
#include <imgui_internal.h>

namespace {
class ClipboardPlatform final : public pasteit::PlatformServices {
public:
    void apply_settings(const pasteit::AppSettings&) override {}
    std::optional<pasteit::ClipboardCapture> poll_clipboard() override { return std::nullopt; }
    void process_events() override {}
    bool publish_text(std::string_view) override { return true; }
    bool publish_image(const std::vector<std::byte>&, std::string_view) override { return true; }
    pasteit::PlatformFocusContext focused_context() override { return {}; }
    std::vector<pasteit::PlatformRecentPath> recent_paths() override { return {}; }
    bool register_global_shortcut(const pasteit::Hotkey&) override { return false; }
    bool global_shortcut_activated() override { return false; }
    bool restore_focus_and_paste(const pasteit::PlatformFocusContext&) override { return false; }
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
    assert(pasteit::multiline_editor_row_count("") == 1);
    assert(pasteit::multiline_editor_row_count("one\ntwo\nthree") == 3);
    assert(pasteit::multiline_editor_visible_rows(std::string(100, 'x')) == 3);
    assert(pasteit::multiline_editor_visible_rows("1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11") == 10);

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
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
        pasteit::input_text_string("Default image directory", value);
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
    pasteit::install_imgui_clipboard_bridge(clipboard);
    ImGui::SetClipboardText("Jev API endpoint");
    assert(clipboard.copied == "Jev API endpoint" && "copy must use PasteIt's clipboard owner");
    clipboard.owned = "http://127.0.0.1:8011";
    assert(std::string{ImGui::GetClipboardText()} == *clipboard.owned && "paste must read app-owned text without GLFW");
    clipboard.owned.reset();
    assert(std::string{ImGui::GetClipboardText()} == "external text");

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(600.0F, 400.0F));
    ImGui::Begin("Result editor size", nullptr, ImGuiWindowFlags_NoDecoration);
    const ImVec2 result_space = ImGui::GetContentRegionAvail();
    std::string result_text = "Short response";
    pasteit::input_text_string("##result", result_text, true, 0, -1.0F);
    const ImVec2 result_size = ImGui::GetItemRectSize();
    assert(result_size.x >= result_space.x - 2.0F);
    assert(result_size.y >= result_space.y - 2.0F);
    ImGui::End();
    ImGui::Render();

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(600.0F, 400.0F));
    ImGui::Begin("Edit editor size", nullptr, ImGuiWindowFlags_NoDecoration);
    std::string prompt_text = "Short prompt";
    const float footer_height = 3.0F * ImGui::GetFrameHeightWithSpacing();
    pasteit::input_text_string("System prompt", prompt_text, true, 0, -footer_height);
    const float remaining_height = ImGui::GetContentRegionAvail().y;
    assert(ImGui::GetItemRectSize().x >= ImGui::GetContentRegionAvail().x - 2.0F);
    assert(remaining_height >= footer_height - ImGui::GetStyle().ItemSpacing.y - 2.0F);
    assert(remaining_height <= footer_height + ImGui::GetStyle().ItemSpacing.y + 2.0F);
    ImGui::End();
    ImGui::Render();

    const auto wrapped_height = [&](const char* title, float width, int flags) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
        ImGui::SetNextWindowSize(ImVec2(width, 120.0F));
        ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoDecoration);
        std::string long_line(300, 'a');
        pasteit::input_text_string("##wrapped", long_line, true, flags, 80.0F);
        ImGuiWindow* wrap_window = ImGui::GetCurrentWindow();
        assert(wrap_window->DC.ChildWindows.Size == 1);
        const ImGuiWindow* text_area = wrap_window->DC.ChildWindows[0];
        const float text_height = text_area->DC.CursorMaxPos.y - text_area->DC.CursorStartPos.y;
        assert(long_line == std::string(300, 'a') && "soft wrapping must not change stored text");
        ImGui::End();
        ImGui::Render();
        return text_height;
    };
    const float narrow_height = wrapped_height("Editable wrapped text", 220.0F, 0);
    const float wide_height = wrapped_height("Read-only wrapped text", 420.0F, ImGuiInputTextFlags_ReadOnly);
    assert(narrow_height > wide_height && "text must reflow to the available width");
    assert(wide_height > ImGui::GetTextLineHeight() * 3.0F && "read-only text must wrap too");
    ImGui::DestroyContext();
#endif
}
