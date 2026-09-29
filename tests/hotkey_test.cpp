#include "config/hotkey.hpp"
#include "config/settings_store.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/settings_model.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <string>

using namespace pasteit;

namespace {

bool has_conflict(const std::string& combo, HotkeyPlatform platform, HotkeyConflictLevel level,
                  const std::string& owner = {}) {
    const auto conflicts = find_hotkey_conflicts(*parse_hotkey(combo), platform);
    return std::any_of(conflicts.begin(), conflicts.end(), [&](const auto& conflict) {
        return conflict.level == level && (owner.empty() || conflict.owner == owner);
    });
}

bool clean(const std::string& combo, HotkeyPlatform platform) {
    return find_hotkey_conflicts(*parse_hotkey(combo), platform).empty();
}

}  // namespace

int main() {
    // Parsing: any order, any case, aliases; canonical output.
    assert(format_hotkey(default_hotkey()) == "Ctrl+Alt+F");
    assert(format_hotkey(*parse_hotkey(" alt + control + f ")) == "Ctrl+Alt+F");
    assert(format_hotkey(*parse_hotkey("win+shift+s")) == "Shift+Super+S");
    assert(format_hotkey(*parse_hotkey("Ctrl+Alt+PgDn")) == "Ctrl+Alt+PageDown");
    assert(format_hotkey(*parse_hotkey("ctrl+alt+esc")) == "Ctrl+Alt+Escape");
    assert(format_hotkey(*parse_hotkey("Ctrl+Shift+period")) == "Ctrl+Shift+.");
    assert(format_hotkey(*parse_hotkey("Super+f12")) == "Super+F12");
    assert(!parse_hotkey(""));
    assert(!parse_hotkey("Ctrl+Alt"));
    assert(!parse_hotkey("Ctrl+Alt+"));
    assert(!parse_hotkey("Hyper+F"));
    assert(!parse_hotkey("Ctrl+F+G"));
    assert(!parse_hotkey("Ctrl+Alt+NoSuchKey"));
    for (const auto& key : hotkey_keys()) {
        const auto parsed = parse_hotkey(std::string("Ctrl+Alt+") + key.name);
        assert(parsed && parsed->key == key.name);
        assert(key.x11_keysym != nullptr && key.windows_vk > 0);
    }

    // The default and typical picks are conflict free on both platforms.
    for (const auto platform : {HotkeyPlatform::Linux, HotkeyPlatform::Windows}) {
        assert(clean("Ctrl+Alt+F", platform));
        assert(clean("Ctrl+Shift+Space", platform));
        assert(clean("Super+Shift+F", platform));
        assert(clean("Ctrl+F13", platform));
    }

    // Typing keys need a real modifier.
    assert(has_conflict("F", HotkeyPlatform::Linux, HotkeyConflictLevel::Blocked, "Typing"));
    assert(has_conflict("Shift+F", HotkeyPlatform::Windows, HotkeyConflictLevel::Blocked, "Typing"));
    assert(has_conflict("Space", HotkeyPlatform::Linux, HotkeyConflictLevel::Blocked));
    assert(!has_conflict("F9", HotkeyPlatform::Linux, HotkeyConflictLevel::Blocked));
    assert(has_conflict("F9", HotkeyPlatform::Linux, HotkeyConflictLevel::Warning));
    assert(clean("F20", HotkeyPlatform::Linux));

    // System-reserved combinations are blocked.
    assert(has_conflict("Ctrl+Alt+Delete", HotkeyPlatform::Windows, HotkeyConflictLevel::Blocked));
    assert(has_conflict("Alt+Tab", HotkeyPlatform::Linux, HotkeyConflictLevel::Blocked));
    assert(has_conflict("Win+L", HotkeyPlatform::Windows, HotkeyConflictLevel::Blocked));
    assert(has_conflict("Ctrl+Alt+F3", HotkeyPlatform::Linux, HotkeyConflictLevel::Blocked, "System"));
    assert(!has_conflict("Ctrl+Alt+F3", HotkeyPlatform::Windows, HotkeyConflictLevel::Blocked));
    assert(has_conflict("Ctrl+Alt+Backspace", HotkeyPlatform::Linux, HotkeyConflictLevel::Blocked));

    // Platform-specific desktop bindings are warnings on their platform only.
    assert(has_conflict("Super+V", HotkeyPlatform::Windows, HotkeyConflictLevel::Warning, "Windows"));
    assert(has_conflict("Ctrl+Alt+T", HotkeyPlatform::Linux, HotkeyConflictLevel::Warning, "Desktop"));
    assert(clean("Ctrl+Alt+T", HotkeyPlatform::Windows));
    assert(!has_conflict("Super+L", HotkeyPlatform::Linux, HotkeyConflictLevel::Blocked));
    assert(has_conflict("Super+L", HotkeyPlatform::Linux, HotkeyConflictLevel::Warning));

    // Application shortcuts: named ones and the generic Ctrl+letter rule.
    assert(has_conflict("Ctrl+V", HotkeyPlatform::Linux, HotkeyConflictLevel::Warning, "Every application"));
    assert(has_conflict("Ctrl+K", HotkeyPlatform::Linux, HotkeyConflictLevel::Warning, "Every application"));
    assert(find_hotkey_conflicts(*parse_hotkey("Ctrl+V"), HotkeyPlatform::Linux).size() == 1);
    assert(has_conflict("Alt+Q", HotkeyPlatform::Windows, HotkeyConflictLevel::Warning, "Every application"));
    assert(has_conflict("Ctrl+Enter", HotkeyPlatform::Linux, HotkeyConflictLevel::Warning, "PasteIt"));

    // Windows AltGr: Ctrl+Alt+E types € on many layouts.
    assert(has_conflict("Ctrl+Alt+E", HotkeyPlatform::Windows, HotkeyConflictLevel::Warning, "Keyboard layout"));
    assert(clean("Ctrl+Alt+E", HotkeyPlatform::Linux));

    // Desktop configuration parsers.
    assert(format_hotkey(*parse_gtk_accelerator("<Primary><Alt>t")) == "Ctrl+Alt+T");
    assert(format_hotkey(*parse_gtk_accelerator("<Super>l")) == "Super+L");
    assert(format_hotkey(*parse_gtk_accelerator("<Shift><Super>Page_Down")) == "Shift+Super+PageDown");
    assert(format_hotkey(*parse_gtk_accelerator("<Alt>space")) == "Alt+Space");
    assert(!parse_gtk_accelerator("<Super>Above_Tab"));
    assert(!parse_gtk_accelerator("XF86Calculator"));
    assert(!parse_gtk_accelerator("<Hyper>a"));
    assert(!parse_gtk_accelerator(""));
    {
        const auto bindings = parse_gsettings_keybindings(
            "org.gnome.desktop.wm.keybindings show-desktop ['<Primary><Super>d', '<Primary><Alt>d', '<Super>d']\n"
            "org.gnome.desktop.wm.keybindings switch-group ['<Super>Above_Tab']\n"
            "org.gnome.settings-daemon.plugins.media-keys screensaver ['<Super>l']\n"
            "org.gnome.settings-daemon.plugins.media-keys calculator ['']\n"
            "org.gnome.settings-daemon.plugins.media-keys terminal @as []\n");
        assert(bindings.size() == 4);
        assert(bindings[1].action == "show desktop" && format_hotkey(bindings[1].hotkey) == "Ctrl+Alt+D");
        assert(bindings[3].owner == "GNOME" && bindings[3].action == "screensaver");
    }
    {
        const auto bindings = parse_kde_global_shortcuts(
            "[ksmserver]\n_k_friendly_name=Session Management\n"
            "Lock Session=Meta+L\tScreensaver,Meta+L\tScreensaver,Lock Session\n"
            "Log Out=Ctrl+Alt+Del,Ctrl+Alt+Del,Show Logout Prompt\n"
            "Halt Without Confirmation=none,,Shut Down Without Confirmation\n");
        assert(bindings.size() == 2);
        assert(format_hotkey(bindings[0].hotkey) == "Super+L" && bindings[0].action == "Lock Session");
        assert(bindings[1].owner == "KDE" && format_hotkey(bindings[1].hotkey) == "Ctrl+Alt+Delete");
    }
    {
        // A live desktop list replaces the generic desktop table and names the action.
        const std::vector<DesktopShortcut> desktop{{*parse_hotkey("Ctrl+Alt+G"), "GNOME", "launch browser"}};
        const auto conflicts = find_hotkey_conflicts(*parse_hotkey("Ctrl+Alt+G"), HotkeyPlatform::Linux, desktop);
        assert(conflicts.size() == 1 && conflicts[0].owner == "GNOME");
        assert(conflicts[0].action_en.find("launch browser") != std::string::npos);
        assert(clean("Ctrl+Alt+T", HotkeyPlatform::Linux) == false);
        assert(find_hotkey_conflicts(*parse_hotkey("Ctrl+Alt+T"), HotkeyPlatform::Linux, desktop).empty());
        // System entries stay even with a desktop list.
        assert(has_blocking_conflict(find_hotkey_conflicts(*parse_hotkey("Alt+Tab"), HotkeyPlatform::Linux, desktop)));
    }

    // Editor model: live availability first, then conflicts; save gating.
    {
        const auto model = build_hotkey_editor_model("Ctrl+Alt+F", HotkeyAvailability::Current, UiLanguage::English,
                                                     HotkeyPlatform::Linux);
        assert(model.can_save && model.notes.size() == 1 && model.notes[0].level == HotkeyNoteLevel::Ok);
    }
    {
        const auto model = build_hotkey_editor_model("Ctrl+Alt+G", HotkeyAvailability::InUse, UiLanguage::English,
                                                     HotkeyPlatform::Linux);
        assert(!model.can_save && model.notes[0].level == HotkeyNoteLevel::Error);
    }
    {
        const auto model = build_hotkey_editor_model("Ctrl+Alt+T", HotkeyAvailability::Available,
                                                     UiLanguage::SimplifiedChinese, HotkeyPlatform::Linux);
        assert(model.can_save && model.notes.size() == 2 && model.notes[1].level == HotkeyNoteLevel::Warning);
        assert(model.notes[1].text.find("打开终端") != std::string::npos);
    }
    {
        const auto model = build_hotkey_editor_model("Super+Shift+F", HotkeyAvailability::Available,
                                                     UiLanguage::English, HotkeyPlatform::Windows);
        assert(model.display == "Shift+Win+F");
    }
    assert(!build_hotkey_editor_model("Alt+Tab", HotkeyAvailability::Available, UiLanguage::English,
                                      HotkeyPlatform::Linux).can_save);
    assert(!build_hotkey_editor_model("nonsense", HotkeyAvailability::Unknown, UiLanguage::English).can_save);

    // Settings: normalized, persisted, validated.
    auto settings = default_settings();
    assert(settings.global_hotkey == "Ctrl+Alt+F");
    settings.global_hotkey = "shift+ctrl+space";
    normalize_settings(settings);
    assert(settings.global_hotkey == "Ctrl+Shift+Space");
    settings.global_hotkey = "garbage";
    normalize_settings(settings);
    assert(settings.global_hotkey == "Ctrl+Alt+F");

    const auto directory = std::filesystem::temp_directory_path() / "pasteit-hotkey-test";
    std::filesystem::create_directories(directory);
    const SettingsStore store(directory / "settings.json");
    settings.global_hotkey = "Ctrl+Shift+F";
    std::string error;
    assert(store.save(settings, error));
    assert(store.load().settings.global_hotkey == "Ctrl+Shift+F");

    auto applied = default_settings();
    applied.default_image_directory = directory;
    applied.default_text_directory = directory;
    SettingsModel model(applied);
    model.working().global_hotkey = "Ctrl+Alt+Delete";
    AppSettings saved;
    assert(!model.save(saved, error));
    assert(error.find("Ctrl+Alt+Delete") != std::string::npos);
    model.working().global_hotkey = "Ctrl+Alt+V";
    assert(model.save(saved, error));
    assert(saved.global_hotkey == "Ctrl+Alt+V");
    model.reset(SettingsSection::General);
    assert(model.working().global_hotkey == "Ctrl+Alt+F");

    std::filesystem::remove_all(directory);
    return 0;
}
