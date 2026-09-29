#pragma once

#include "core/types.hpp"
#include "config/app_settings.hpp"
#include "platform/fast_action_services.hpp"
#include "storage/clipboard_store.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pasteit {

using ClipboardCapture = ClipboardData;

struct PlatformFocusContext {
    std::uint64_t window_id = 0;
    std::string app_name;
    std::string window_title;
    std::string focused_target_hash;
    std::uint32_t pid = 0;
    std::optional<std::filesystem::path> current_directory;
};

struct PlatformRecentPath {
    std::filesystem::path path;
    PathKind kind = PathKind::Directory;
    std::string source;
    std::int64_t observed_at_ms = 0;
};

class PlatformServices {
public:
    virtual ~PlatformServices() = default;

    virtual void apply_settings(const AppSettings& settings) = 0;
    virtual std::optional<ClipboardCapture> poll_clipboard() = 0;
    virtual void process_events() = 0;
    virtual bool publish_text(std::string_view text) = 0;
    virtual bool publish_image(const std::vector<std::byte>& bytes, std::string_view mime_type) = 0;
    virtual PlatformFocusContext focused_context() = 0;
    virtual std::vector<PlatformRecentPath> recent_paths() = 0;
    // Replaces the active global shortcut. On failure the previous one stays active.
    virtual bool register_global_shortcut(const Hotkey& hotkey) = 0;
    virtual bool global_shortcut_activated() = 0;
    // Asks the OS whether the shortcut could be grabbed now, without keeping it.
    virtual HotkeyAvailability probe_global_shortcut(const Hotkey& hotkey) { (void)hotkey; return HotkeyAvailability::Unknown; }
    // Shortcuts the desktop environment has bound (read once, then cached).
    virtual std::vector<DesktopShortcut> desktop_shortcuts() { return {}; }
    virtual bool restore_focus_and_paste(const PlatformFocusContext& context) = 0;
    virtual bool open_path(const std::filesystem::path& path) = 0;
    virtual bool open_uri(std::string_view uri) = 0;
    virtual bool copy_text(std::string_view text) = 0;
    // If this process owns the clipboard, return its text without a synchronous
    // request that would wait for this same UI thread to serve the selection.
    virtual std::optional<std::string> owned_clipboard_text() const { return std::nullopt; }
    virtual bool move_popup_by(int delta_x, int delta_y) = 0;
    virtual bool set_popup_opacity(float opacity) = 0;
    // Makes a sub-window owned by the popup so the window manager keeps it
    // above the popup (a transient window on X11, an owned window on Win32).
    virtual void keep_above_popup(std::uint64_t window_id) { (void)window_id; }
    virtual std::vector<std::filesystem::path> preferred_ui_fonts() = 0;
    virtual std::optional<std::filesystem::path> choose_directory(
        const std::filesystem::path& initial_directory) = 0;
    virtual FastActionServices& fast_actions() { return empty_fast_action_services(); }
};

}  // namespace pasteit
