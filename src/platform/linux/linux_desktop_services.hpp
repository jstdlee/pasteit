#pragma once

#include "platform/linux/linux_fast_action_services.hpp"
#include "platform/linux/linux_x11_clipboard.hpp"
#include "platform/linux/linux_x11_focus.hpp"
#include "platform/platform_services.hpp"
#include "config/app_settings.hpp"

namespace pasteit {

std::optional<std::filesystem::path> browser_open_path(const std::filesystem::path& path,
                                                      const std::filesystem::path& home_directory);

// Xlib's default error handler exits the process; windows can disappear
// between a query and its reply (e.g. a closing dialog that had focus), so
// PasteIt logs X errors instead. Scoped traps still override it locally.
void install_nonfatal_x11_error_handler(bool log_errors);

// _NET_WM_WINDOW_OPACITY value for 0..1; never wraps to 0 at full opacity.
unsigned long x11_opacity_cardinal(float opacity);

class LinuxDesktopServices final : public PlatformServices {
public:
    LinuxDesktopServices() = default;
    explicit LinuxDesktopServices(const AppSettings& settings) { apply_settings(settings); }

    void apply_settings(const AppSettings& settings) override;
    void attach_popup_window(std::uint64_t window_id) { popup_window_id_ = window_id; }
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
    std::optional<std::string> owned_clipboard_text() const override;
    bool move_popup_by(int delta_x, int delta_y) override;
    bool set_popup_opacity(float opacity) override;
    void keep_above_popup(std::uint64_t window_id) override;
    std::vector<std::filesystem::path> preferred_ui_fonts() override;
    std::optional<std::filesystem::path> choose_directory(const std::filesystem::path& initial_directory) override;
    FastActionServices& fast_actions() override;

private:
    LinuxX11Clipboard clipboard_;
    LinuxX11Focus focus_;
    LinuxFastActionServices fast_actions_;
    std::uint64_t popup_window_id_ = 0;
};

}  // namespace pasteit
