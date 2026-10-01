#include "ui/hotkey_editor.hpp"

#include "platform/platform_services.hpp"
#include "ui/localization.hpp"

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
#include "ui/icons.hpp"
#include "ui/theme.hpp"

#include <imgui.h>

#include <algorithm>
#endif

namespace pasteit {

std::string display_hotkey(const Hotkey& hotkey, HotkeyPlatform platform) {
    auto text = format_hotkey(hotkey);
    if (platform == HotkeyPlatform::Windows && hotkey.super) {
        text.replace(text.find("Super+"), 6, "Win+");
    }
    return text;
}

HotkeyEditorModel build_hotkey_editor_model(const std::string& hotkey_text, HotkeyAvailability availability,
                                            UiLanguage language, HotkeyPlatform platform,
                                            const std::vector<DesktopShortcut>& desktop) {
    HotkeyEditorModel model;
    const bool chinese = language == UiLanguage::SimplifiedChinese;
    const auto hotkey = parse_hotkey(hotkey_text);
    if (!hotkey) {
        model.display = hotkey_text;
        model.can_save = false;
        model.notes.push_back({HotkeyNoteLevel::Error, tr(language, UiTextKey::ShortcutUnsupported)});
        return model;
    }
    model.display = display_hotkey(*hotkey, platform);
    switch (availability) {
    case HotkeyAvailability::Available:
        model.notes.push_back({HotkeyNoteLevel::Ok, tr(language, UiTextKey::ShortcutAvailable)});
        break;
    case HotkeyAvailability::Current:
        model.notes.push_back({HotkeyNoteLevel::Ok, tr(language, UiTextKey::ShortcutCurrent)});
        break;
    case HotkeyAvailability::InUse:
        model.can_save = false;
        model.notes.push_back({HotkeyNoteLevel::Error, tr(language, UiTextKey::ShortcutInUse)});
        break;
    case HotkeyAvailability::Unsupported:
        model.can_save = false;
        model.notes.push_back({HotkeyNoteLevel::Error, tr(language, UiTextKey::ShortcutUnsupported)});
        break;
    case HotkeyAvailability::Unknown:
        model.notes.push_back({HotkeyNoteLevel::Info, tr(language, UiTextKey::ShortcutUnknown)});
        break;
    }
    for (const auto& conflict : find_hotkey_conflicts(*hotkey, platform, desktop)) {
        const bool blocked = conflict.level == HotkeyConflictLevel::Blocked;
        if (blocked) model.can_save = false;
        const auto prefix = tr(language, blocked ? UiTextKey::ShortcutBlocked : UiTextKey::ShortcutWarning);
        model.notes.push_back({blocked ? HotkeyNoteLevel::Error : HotkeyNoteLevel::Warning,
                               prefix + " \xC2\xB7 " + conflict.owner + ": " +
                                   (chinese ? conflict.action_zh : conflict.action_en)});
    }
    return model;
}

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
namespace {

struct CaptureKey {
    ImGuiKey imgui;
    const char* name;
};

const std::vector<CaptureKey>& capture_keys() {
    static const std::vector<CaptureKey> keys = [] {
        std::vector<CaptureKey> out;
        for (const auto& key : hotkey_keys()) {
            const std::string_view name = key.name;
            ImGuiKey imgui = ImGuiKey_None;
            if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z') {
                imgui = static_cast<ImGuiKey>(ImGuiKey_A + (name[0] - 'A'));
            } else if (name.size() == 1 && name[0] >= '0' && name[0] <= '9') {
                imgui = static_cast<ImGuiKey>(ImGuiKey_0 + (name[0] - '0'));
            } else if (name.size() >= 2 && name[0] == 'F' && name[1] >= '0' && name[1] <= '9') {
                imgui = static_cast<ImGuiKey>(ImGuiKey_F1 + (std::stoi(std::string(name.substr(1))) - 1));
            } else {
                struct Named { std::string_view name; ImGuiKey key; };
                static constexpr Named named[] = {
                    {"Space", ImGuiKey_Space},        {"Enter", ImGuiKey_Enter},
                    {"Tab", ImGuiKey_Tab},            {"Backspace", ImGuiKey_Backspace},
                    {"Escape", ImGuiKey_Escape},      {"Insert", ImGuiKey_Insert},
                    {"Delete", ImGuiKey_Delete},      {"Home", ImGuiKey_Home},
                    {"End", ImGuiKey_End},            {"PageUp", ImGuiKey_PageUp},
                    {"PageDown", ImGuiKey_PageDown},  {"Left", ImGuiKey_LeftArrow},
                    {"Up", ImGuiKey_UpArrow},         {"Right", ImGuiKey_RightArrow},
                    {"Down", ImGuiKey_DownArrow},     {"Print", ImGuiKey_PrintScreen},
                    {"Pause", ImGuiKey_Pause},        {"`", ImGuiKey_GraveAccent},
                    {"-", ImGuiKey_Minus},            {"=", ImGuiKey_Equal},
                    {"[", ImGuiKey_LeftBracket},      {"]", ImGuiKey_RightBracket},
                    {"\\", ImGuiKey_Backslash},       {";", ImGuiKey_Semicolon},
                    {"'", ImGuiKey_Apostrophe},       {",", ImGuiKey_Comma},
                    {".", ImGuiKey_Period},           {"/", ImGuiKey_Slash},
                };
                for (const auto& entry : named) {
                    if (entry.name == name) imgui = entry.key;
                }
            }
            if (imgui != ImGuiKey_None) out.push_back({imgui, key.name});
        }
        return out;
    }();
    return keys;
}

}  // namespace

bool draw_hotkey_editor(const char* id, std::string& value, HotkeyEditorState& state, PlatformServices& platform,
                        UiLanguage language, const UiPalette& palette) {
    bool changed = false;
    ImGui::PushID(id);
    if (state.capturing) {
        const auto& io = ImGui::GetIO();
        for (const auto& key : capture_keys()) {
            if (!ImGui::IsKeyPressed(key.imgui, false)) continue;
            const bool any_modifier = io.KeyCtrl || io.KeyAlt || io.KeyShift || io.KeySuper;
            if (key.imgui == ImGuiKey_Escape && !any_modifier) {
                state.capturing = false;
                break;
            }
            Hotkey captured{.ctrl = io.KeyCtrl, .alt = io.KeyAlt, .shift = io.KeyShift, .super = io.KeySuper,
                            .key = key.name};
            const auto text = format_hotkey(captured);
            changed = text != value;
            value = text;
            state.capturing = false;
            break;
        }
    }
    if (state.probed_text != value) {
        const auto hotkey = parse_hotkey(value);
        state.probed = hotkey ? platform.probe_global_shortcut(*hotkey) : HotkeyAvailability::Unsupported;
        state.probed_text = value;
    }
    if (!state.desktop) state.desktop = platform.desktop_shortcuts();
    const auto model = build_hotkey_editor_model(value, state.probed, language, current_hotkey_platform(), *state.desktop);

    const auto label = state.capturing ? tr(language, UiTextKey::PressShortcut)
                                       : with_icon(icon::kKeyboard, model.display);
    // Clicking toggles capturing, so pop by what was pushed, not by the new state.
    const bool highlighted = state.capturing;
    if (highlighted) ImGui::PushStyleColor(ImGuiCol_Button, palette.accent_soft);
    const auto fallback = std::string(kDefaultHotkey);
    const float reset_width = icon_buttons_width(1);
    const float capture_width = std::max(120.0F, ImGui::GetContentRegionAvail().x - reset_width - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button((label + "###capture").c_str(), ImVec2(capture_width, 0.0F))) {
        state.capturing = !state.capturing;
    }
    if (highlighted) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(language, UiTextKey::ChangeShortcut).c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(value == fallback);
    if (icon_button("reset", icon::kUndo, tr(language, UiTextKey::RestoreDefaults) + ": " + fallback)) {
        value = fallback;
        state.capturing = false;
        changed = true;
    }
    ImGui::EndDisabled();
    for (const auto& note : model.notes) {
        const char* glyph = icon::kInfo;
        ImVec4 color = palette.text_muted;
        switch (note.level) {
        case HotkeyNoteLevel::Ok: glyph = icon::kCircleCheck; color = palette.success; break;
        case HotkeyNoteLevel::Info: break;
        case HotkeyNoteLevel::Warning: glyph = icon::kWarning; color = palette.warning; break;
        case HotkeyNoteLevel::Error: glyph = icon::kCircleX; color = palette.danger; break;
        }
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(color, "%s", with_icon(glyph, note.text).c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::PopID();
    return changed;
}
#endif

}  // namespace pasteit
