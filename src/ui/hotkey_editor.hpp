#pragma once

#include "config/app_settings.hpp"
#include "config/hotkey.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pasteit {

class PlatformServices;
struct UiPalette;

enum class HotkeyNoteLevel { Ok, Info, Warning, Error };

struct HotkeyNote {
    HotkeyNoteLevel level = HotkeyNoteLevel::Info;
    std::string text;
};

// What the Settings row says under the shortcut: the live OS check first,
// then every known conflict. can_save is false for anything that would fail.
struct HotkeyEditorModel {
    std::string display;
    std::vector<HotkeyNote> notes;
    bool can_save = true;
};

HotkeyEditorModel build_hotkey_editor_model(const std::string& hotkey_text, HotkeyAvailability availability,
                                            UiLanguage language,
                                            HotkeyPlatform platform = current_hotkey_platform(),
                                            const std::vector<DesktopShortcut>& desktop = {});

// "Super" is "Win" on Windows; the stored text keeps "Super".
std::string display_hotkey(const Hotkey& hotkey, HotkeyPlatform platform = current_hotkey_platform());

struct HotkeyEditorState {
    bool capturing = false;
    // Live probe cache: probing grabs the key on the X server, so it runs only
    // when the draft changes, not every frame.
    std::string probed_text;
    HotkeyAvailability probed = HotkeyAvailability::Unknown;
    std::optional<std::vector<DesktopShortcut>> desktop;
};

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
// Capture button plus conflict notes. Returns true when value changed.
bool draw_hotkey_editor(const char* id, std::string& value, HotkeyEditorState& state, PlatformServices& platform,
                        UiLanguage language, const UiPalette& palette);
#endif

}  // namespace pasteit
