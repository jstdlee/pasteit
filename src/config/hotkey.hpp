#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pasteit {

// A global shortcut such as "Ctrl+Alt+F". Stored in settings as its canonical
// text; parse_hotkey accepts any modifier order and case.
struct Hotkey {
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool super = false;  // Windows key / Super
    std::string key;     // canonical key name from hotkey_keys(), e.g. "F", "Space", "F5"

    bool operator==(const Hotkey&) const = default;
};

// One key a global shortcut may use, with the native names each backend needs.
struct HotkeyKey {
    const char* name;         // canonical, shown to the user
    const char* x11_keysym;   // for XStringToKeysym
    int windows_vk;           // virtual-key code
};

const std::vector<HotkeyKey>& hotkey_keys();
const HotkeyKey* find_hotkey_key(std::string_view name);

inline constexpr std::string_view kDefaultHotkey = "Ctrl+Alt+F";
Hotkey default_hotkey();
std::optional<Hotkey> parse_hotkey(std::string_view text);
std::string format_hotkey(const Hotkey& hotkey);

enum class HotkeyPlatform { Linux, Windows };
HotkeyPlatform current_hotkey_platform();

// A shortcut the running desktop has bound, read from its configuration
// (GNOME gsettings, KDE kglobalshortcutsrc). Desktops grab keys in ways a
// test grab does not always see, so these are compared directly.
struct DesktopShortcut {
    Hotkey hotkey;
    std::string owner;  // "GNOME", "KDE"
    std::string action;
};

// "<Primary><Alt>t" -> Ctrl+Alt+T; nullopt for keys PasteIt cannot bind.
std::optional<Hotkey> parse_gtk_accelerator(std::string_view accelerator);
// Output of `gsettings list-recursively <schema>`: "schema key ['<Super>l', ...]".
std::vector<DesktopShortcut> parse_gsettings_keybindings(std::string_view listing);
// ~/.config/kglobalshortcutsrc: "action=Meta+L\tScreensaver,Meta+L,Lock Session".
std::vector<DesktopShortcut> parse_kde_global_shortcuts(std::string_view config);

// Blocked: the shortcut cannot work or would break normal typing, so it is not
// saved. Warning: it works but takes over a well-known shortcut.
enum class HotkeyConflictLevel { Blocked, Warning };

struct HotkeyConflict {
    HotkeyConflictLevel level = HotkeyConflictLevel::Warning;
    std::string owner;       // "Windows", "Desktop", "Every application", "PasteIt"
    std::string action_en;
    std::string action_zh;
};

// Known conflicts from built-in rules and system/application shortcut tables.
// This is instant and offline; it cannot see other applications' grabs, which
// PlatformServices::probe_global_shortcut checks live.
// With a desktop list, it replaces the generic "Desktop" table entries.
std::vector<HotkeyConflict> find_hotkey_conflicts(const Hotkey& hotkey,
                                                  HotkeyPlatform platform = current_hotkey_platform(),
                                                  const std::vector<DesktopShortcut>& desktop = {});
bool has_blocking_conflict(const std::vector<HotkeyConflict>& conflicts);

// Result of asking the OS whether a shortcut can be grabbed right now.
enum class HotkeyAvailability {
    Unknown,      // backend cannot tell (no display, test fake)
    Available,    // nobody holds it
    Current,      // PasteIt already holds it
    InUse,        // another application holds it
    Unsupported,  // the key does not exist on this keyboard layout
};

}  // namespace pasteit
