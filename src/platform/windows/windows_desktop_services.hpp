#pragma once

#include "platform/platform_services.hpp"
#include "platform/windows/windows_fast_action_services.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace pastit {

class WindowsDesktopServices final : public PlatformServices {
public:
    WindowsDesktopServices();
    ~WindowsDesktopServices() override;

    void attach_popup_window(std::uint64_t window_id);

    void apply_settings(const AppSettings& settings) override;
    std::optional<ClipboardCapture> poll_clipboard() override;
    void process_events() override;
    bool publish_text(std::string_view text) override;
    bool publish_image(const std::vector<std::byte>& bytes, std::string_view mime_type) override;
    PlatformFocusContext focused_context() override;
    std::vector<PlatformRecentPath> recent_paths() override;
    bool register_global_shortcut() override;
    bool global_shortcut_activated() override;
    bool restore_focus_and_paste(const PlatformFocusContext& context) override;
    bool open_path(const std::filesystem::path& path) override;
    bool open_uri(std::string_view uri) override;
    bool copy_text(std::string_view text) override;
    bool move_popup_by(int delta_x, int delta_y) override;
    bool set_popup_opacity(float opacity) override;
    void keep_above_popup(std::uint64_t window_id) override;
    std::vector<std::filesystem::path> preferred_ui_fonts() override;
    std::optional<std::filesystem::path> choose_directory(const std::filesystem::path& initial_directory) override;
    FastActionServices& fast_actions() override;

private:
    bool ensure_message_window();
    void mark_clipboard_dirty();
    void mark_shortcut_activated();

    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    HWND message_window_ = nullptr;
    HWND popup_window_ = nullptr;
    UINT png_format_ = 0;
    UINT jpeg_format_ = 0;
    DWORD clipboard_sequence_ = 0;
    bool clipboard_dirty_ = true;
    bool shortcut_activated_ = false;
    bool shortcut_registered_ = false;
    WindowsFastActionServices fast_actions_;
};

}  // namespace pastit
