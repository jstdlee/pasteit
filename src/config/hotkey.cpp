#include "config/hotkey.hpp"

#include <algorithm>
#include <cctype>

namespace pasteit {
namespace {

std::string lowercase(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

std::vector<HotkeyKey> build_keys() {
    std::vector<HotkeyKey> keys;
    static const char* letters[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
                                    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
    static const char* letter_syms[] = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
                                        "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"};
    for (int i = 0; i < 26; ++i) keys.push_back({letters[i], letter_syms[i], 'A' + i});
    static const char* digits[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    for (int i = 0; i < 10; ++i) keys.push_back({digits[i], digits[i], '0' + i});
    static const char* functions[] = {"F1",  "F2",  "F3",  "F4",  "F5",  "F6",  "F7",  "F8",
                                      "F9",  "F10", "F11", "F12", "F13", "F14", "F15", "F16",
                                      "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24"};
    for (int i = 0; i < 24; ++i) keys.push_back({functions[i], functions[i], 0x70 + i});
    const HotkeyKey named[] = {
        {"Space", "space", 0x20},      {"Enter", "Return", 0x0D},       {"Tab", "Tab", 0x09},
        {"Backspace", "BackSpace", 0x08}, {"Escape", "Escape", 0x1B},   {"Insert", "Insert", 0x2D},
        {"Delete", "Delete", 0x2E},    {"Home", "Home", 0x24},          {"End", "End", 0x23},
        {"PageUp", "Prior", 0x21},     {"PageDown", "Next", 0x22},      {"Left", "Left", 0x25},
        {"Up", "Up", 0x26},            {"Right", "Right", 0x27},        {"Down", "Down", 0x28},
        {"Print", "Print", 0x2C},      {"Pause", "Pause", 0x13},        {"`", "grave", 0xC0},
        {"-", "minus", 0xBD},          {"=", "equal", 0xBB},            {"[", "bracketleft", 0xDB},
        {"]", "bracketright", 0xDD},   {"\\", "backslash", 0xDC},       {";", "semicolon", 0xBA},
        {"'", "apostrophe", 0xDE},     {",", "comma", 0xBC},            {".", "period", 0xBE},
        {"/", "slash", 0xBF},
    };
    keys.insert(keys.end(), std::begin(named), std::end(named));
    return keys;
}

std::string_view key_alias(std::string_view lowered) {
    struct Alias { std::string_view from, to; };
    static constexpr Alias aliases[] = {
        {"esc", "Escape"},     {"return", "Enter"},      {"pgup", "PageUp"},   {"pgdn", "PageDown"},
        {"pagedn", "PageDown"}, {"prior", "PageUp"},     {"next", "PageDown"}, {"del", "Delete"},
        {"ins", "Insert"},     {"printscreen", "Print"}, {"prtsc", "Print"},   {"grave", "`"},
        {"backquote", "`"},    {"minus", "-"},           {"equal", "="},       {"equals", "="},
        {"bracketleft", "["},  {"bracketright", "]"},    {"backslash", "\\"},  {"semicolon", ";"},
        {"quote", "'"},        {"apostrophe", "'"},      {"comma", ","},       {"period", "."},
        {"slash", "/"},        {"arrowup", "Up"},        {"arrowdown", "Down"}, {"arrowleft", "Left"},
        {"arrowright", "Right"}, {"page_up", "PageUp"},   {"page_down", "PageDown"},
    };
    for (const auto& alias : aliases) {
        if (alias.from == lowered) return alias.to;
    }
    return {};
}

bool is_function_key(std::string_view key) {
    return key.size() >= 2 && key[0] == 'F' && std::isdigit(static_cast<unsigned char>(key[1]));
}

bool is_letter(std::string_view key) {
    return key.size() == 1 && key[0] >= 'A' && key[0] <= 'Z';
}

// Shortcuts that belong to the OS, the desktop or practically every app.
// Parsed on use so the table stays readable.
struct KnownShortcut {
    bool on_linux;
    bool on_windows;
    std::string_view combo;
    HotkeyConflictLevel level;
    std::string_view owner;
    std::string_view action_en;
    std::string_view action_zh;
};

constexpr auto B = HotkeyConflictLevel::Blocked;
constexpr auto W = HotkeyConflictLevel::Warning;

constexpr KnownShortcut kKnownShortcuts[] = {
    // Reserved by the system: an application can never receive these.
    {true, true, "Ctrl+Alt+Delete", B, "System", "Security screen / log out", "安全选项 / 注销"},
    {true, true, "Alt+Tab", B, "System", "Switch windows", "切换窗口"},
    {true, true, "Alt+Shift+Tab", B, "System", "Switch windows backwards", "反向切换窗口"},
    {true, true, "Alt+F4", B, "System", "Close window", "关闭窗口"},
    {false, true, "Ctrl+Shift+Escape", B, "Windows", "Task Manager", "任务管理器"},
    {false, true, "Ctrl+Escape", B, "Windows", "Start menu", "开始菜单"},
    {false, true, "Super+L", B, "Windows", "Lock the PC", "锁定电脑"},
    {false, true, "Super+Tab", B, "Windows", "Task view", "任务视图"},
    {true, false, "Ctrl+Alt+Backspace", B, "X server", "Kill the X session", "强制结束 X 会话"},
    // Common desktop and OS bindings: grabbable only if the desktop lets go.
    {false, true, "Super+D", W, "Windows", "Show desktop", "显示桌面"},
    {false, true, "Super+E", W, "Windows", "File Explorer", "文件资源管理器"},
    {false, true, "Super+R", W, "Windows", "Run dialog", "运行对话框"},
    {false, true, "Super+V", W, "Windows", "Clipboard history", "剪贴板历史"},
    {false, true, "Super+Shift+S", W, "Windows", "Snipping Tool", "截图工具"},
    {false, true, "Super+I", W, "Windows", "Settings", "设置"},
    {false, true, "Super+A", W, "Windows", "Quick settings", "快速设置"},
    {false, true, "Super+S", W, "Windows", "Search", "搜索"},
    {false, true, "Super+X", W, "Windows", "Quick link menu", "快速链接菜单"},
    {false, true, "Super+.", W, "Windows", "Emoji panel", "表情面板"},
    {false, true, "Super+Space", W, "Windows", "Switch input language", "切换输入法"},
    {false, true, "Super+Up", W, "Windows", "Maximize window", "最大化窗口"},
    {false, true, "Super+Down", W, "Windows", "Minimize window", "最小化窗口"},
    {false, true, "Super+Left", W, "Windows", "Snap window left", "窗口贴靠左侧"},
    {false, true, "Super+Right", W, "Windows", "Snap window right", "窗口贴靠右侧"},
    {false, true, "Ctrl+Alt+Up", W, "Graphics driver", "Rotate screen (some drivers)", "旋转屏幕（部分显卡驱动）"},
    {false, true, "Ctrl+Alt+Down", W, "Graphics driver", "Rotate screen (some drivers)", "旋转屏幕（部分显卡驱动）"},
    {true, true, "Alt+Space", W, "Desktop", "Window menu", "窗口菜单"},
    {true, false, "Ctrl+Alt+T", W, "Desktop", "Open terminal (GNOME, Ubuntu)", "打开终端（GNOME、Ubuntu）"},
    {true, false, "Ctrl+Alt+L", W, "Desktop", "Lock screen (KDE, Xfce)", "锁屏（KDE、Xfce）"},
    {true, false, "Super+L", W, "Desktop", "Lock screen (GNOME)", "锁屏（GNOME）"},
    {true, false, "Alt+F2", W, "Desktop", "Run command", "运行命令"},
    {true, false, "Super+A", W, "Desktop", "Show applications (GNOME)", "显示应用（GNOME）"},
    {true, false, "Super+V", W, "Desktop", "Notification list (GNOME)", "通知列表（GNOME）"},
    {true, false, "Super+Space", W, "Desktop", "Switch input source", "切换输入源"},
    {true, false, "Super+Tab", W, "Desktop", "Switch applications", "切换应用"},
    {true, false, "Super+D", W, "Desktop", "Show desktop", "显示桌面"},
    {true, false, "Super+E", W, "Desktop", "File manager (KDE)", "文件管理器（KDE）"},
    {true, false, "Ctrl+Alt+Left", W, "Desktop", "Previous workspace", "上一个工作区"},
    {true, false, "Ctrl+Alt+Right", W, "Desktop", "Next workspace", "下一个工作区"},
    {true, false, "Ctrl+Alt+Up", W, "Desktop", "Workspace overview", "工作区概览"},
    {true, false, "Ctrl+Alt+Down", W, "Desktop", "Workspace below", "下方工作区"},
    {true, false, "Ctrl+Alt+D", W, "Desktop", "Show desktop (some desktops)", "显示桌面（部分桌面）"},
    {true, true, "Print", W, "Desktop", "Screenshot", "截图"},
    {true, true, "Shift+Print", W, "Desktop", "Area screenshot", "区域截图"},
    // Editing shortcuts every application relies on.
    {true, true, "Ctrl+A", W, "Every application", "Select all", "全选"},
    {true, true, "Ctrl+C", W, "Every application", "Copy", "复制"},
    {true, true, "Ctrl+V", W, "Every application", "Paste", "粘贴"},
    {true, true, "Ctrl+X", W, "Every application", "Cut", "剪切"},
    {true, true, "Ctrl+Z", W, "Every application", "Undo", "撤销"},
    {true, true, "Ctrl+Y", W, "Every application", "Redo", "重做"},
    {true, true, "Ctrl+S", W, "Every application", "Save", "保存"},
    {true, true, "Ctrl+F", W, "Every application", "Find", "查找"},
    {true, true, "Ctrl+Shift+V", W, "Every application", "Paste as plain text / terminal paste",
     "粘贴为纯文本 / 终端粘贴"},
    {true, true, "Ctrl+Shift+C", W, "Every application", "Terminal copy / developer tools", "终端复制 / 开发者工具"},
    {true, true, "Ctrl+Enter", W, "PasteIt", "Apply edited preview text", "应用编辑后的预览文本"},
};

}  // namespace

const std::vector<HotkeyKey>& hotkey_keys() {
    static const std::vector<HotkeyKey> keys = build_keys();
    return keys;
}

const HotkeyKey* find_hotkey_key(std::string_view name) {
    const auto lowered = lowercase(trim(name));
    if (lowered.empty()) return nullptr;
    const auto alias = key_alias(lowered);
    for (const auto& key : hotkey_keys()) {
        if (!alias.empty() ? alias == key.name : lowercase(key.name) == lowered) return &key;
    }
    return nullptr;
}

Hotkey default_hotkey() {
    return *parse_hotkey(kDefaultHotkey);
}

std::optional<Hotkey> parse_hotkey(std::string_view text) {
    Hotkey hotkey;
    text = trim(text);
    if (text.empty()) return std::nullopt;
    std::size_t start = 0;
    while (start <= text.size()) {
        auto end = text.find('+', start);
        // A trailing "+" can only be the key itself, which we do not support.
        if (end == std::string_view::npos) end = text.size();
        const auto part = trim(text.substr(start, end - start));
        const bool last = end == text.size();
        if (part.empty()) return std::nullopt;
        const auto lowered = lowercase(part);
        if (!last) {
            if (lowered == "ctrl" || lowered == "control") hotkey.ctrl = true;
            else if (lowered == "alt" || lowered == "option") hotkey.alt = true;
            else if (lowered == "shift") hotkey.shift = true;
            else if (lowered == "super" || lowered == "win" || lowered == "meta" || lowered == "cmd") hotkey.super = true;
            else return std::nullopt;
        } else {
            const auto* key = find_hotkey_key(part);
            if (key == nullptr) return std::nullopt;
            hotkey.key = key->name;
        }
        start = end + 1;
    }
    return hotkey.key.empty() ? std::nullopt : std::optional<Hotkey>{hotkey};
}

std::string format_hotkey(const Hotkey& hotkey) {
    std::string out;
    if (hotkey.ctrl) out += "Ctrl+";
    if (hotkey.alt) out += "Alt+";
    if (hotkey.shift) out += "Shift+";
    if (hotkey.super) out += "Super+";
    return out + hotkey.key;
}

HotkeyPlatform current_hotkey_platform() {
#if defined(_WIN32)
    return HotkeyPlatform::Windows;
#else
    return HotkeyPlatform::Linux;
#endif
}

std::optional<Hotkey> parse_gtk_accelerator(std::string_view accelerator) {
    Hotkey hotkey;
    accelerator = trim(accelerator);
    while (!accelerator.empty() && accelerator.front() == '<') {
        const auto close = accelerator.find('>');
        if (close == std::string_view::npos) return std::nullopt;
        const auto modifier = lowercase(accelerator.substr(1, close - 1));
        if (modifier == "primary" || modifier == "control" || modifier == "ctrl" || modifier == "ctl") hotkey.ctrl = true;
        else if (modifier == "alt" || modifier == "mod1") hotkey.alt = true;
        else if (modifier == "shift") hotkey.shift = true;
        else if (modifier == "super" || modifier == "mod4" || modifier == "meta") hotkey.super = true;
        else return std::nullopt;  // Hyper, Release, ...
        accelerator.remove_prefix(close + 1);
    }
    if (accelerator.empty()) return std::nullopt;
    const auto lowered = lowercase(accelerator);
    for (const auto& key : hotkey_keys()) {
        if (lowercase(key.x11_keysym) == lowered) {
            hotkey.key = key.name;
            return hotkey;
        }
    }
    const auto* key = find_hotkey_key(accelerator);
    if (key == nullptr) return std::nullopt;
    hotkey.key = key->name;
    return hotkey;
}

std::vector<DesktopShortcut> parse_gsettings_keybindings(std::string_view listing) {
    std::vector<DesktopShortcut> result;
    while (!listing.empty()) {
        const auto end = listing.find('\n');
        auto line = trim(listing.substr(0, end));
        listing = end == std::string_view::npos ? std::string_view{} : listing.substr(end + 1);
        const auto first = line.find(' ');
        const auto second = first == std::string_view::npos ? first : line.find(' ', first + 1);
        if (second == std::string_view::npos) continue;
        std::string action(line.substr(first + 1, second - first - 1));
        std::replace(action.begin(), action.end(), '-', ' ');
        // Values are GVariant string arrays ['<Super>l', ''] or a single 'string'.
        auto value = line.substr(second + 1);
        std::size_t at = 0;
        while ((at = value.find('\'', at)) != std::string_view::npos) {
            const auto close = value.find('\'', at + 1);
            if (close == std::string_view::npos) break;
            if (const auto hotkey = parse_gtk_accelerator(value.substr(at + 1, close - at - 1))) {
                result.push_back({*hotkey, "GNOME", action});
            }
            at = close + 1;
        }
    }
    return result;
}

std::vector<DesktopShortcut> parse_kde_global_shortcuts(std::string_view config) {
    std::vector<DesktopShortcut> result;
    while (!config.empty()) {
        const auto end = config.find('\n');
        auto line = trim(config.substr(0, end));
        config = end == std::string_view::npos ? std::string_view{} : config.substr(end + 1);
        if (line.empty() || line.front() == '[' || line.front() == '#') continue;
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        // active,default,description; several active keys are tab separated.
        const auto value = line.substr(equals + 1);
        const auto comma = value.find(',');
        const auto active = value.substr(0, comma);
        std::string action(line.substr(0, equals));
        if (const auto last_comma = value.rfind(','); last_comma != std::string_view::npos && last_comma != comma) {
            action = std::string(value.substr(last_comma + 1));
        }
        std::size_t start = 0;
        while (start <= active.size()) {
            auto stop = active.find('\t', start);
            if (stop == std::string_view::npos) stop = active.size();
            if (const auto hotkey = parse_hotkey(active.substr(start, stop - start))) {
                result.push_back({*hotkey, "KDE", action});
            }
            start = stop + 1;
        }
    }
    return result;
}

std::vector<HotkeyConflict> find_hotkey_conflicts(const Hotkey& hotkey, HotkeyPlatform platform,
                                                  const std::vector<DesktopShortcut>& desktop) {
    std::vector<HotkeyConflict> conflicts;
    const auto add = [&](HotkeyConflictLevel level, std::string_view owner, std::string_view en, std::string_view zh) {
        conflicts.push_back({level, std::string(owner), std::string(en), std::string(zh)});
    };
    const bool strong_modifier = hotkey.ctrl || hotkey.alt || hotkey.super;
    const bool standalone_key = is_function_key(hotkey.key) || hotkey.key == "Print" || hotkey.key == "Pause";
    if (!strong_modifier && !standalone_key) {
        add(HotkeyConflictLevel::Blocked, "Typing",
            "Needs Ctrl, Alt or Super, otherwise the key could no longer be typed",
            "需要 Ctrl、Alt 或 Super，否则该键将无法正常输入");
    }
    if (!hotkey.ctrl && !hotkey.super && hotkey.alt && !hotkey.shift && (is_letter(hotkey.key) || hotkey.key.size() == 1)) {
        add(HotkeyConflictLevel::Warning, "Every application", "Alt+key opens menus (access keys)",
            "Alt+键 用于打开菜单（访问键）");
    }
    if (standalone_key && !strong_modifier && is_function_key(hotkey.key) && hotkey.key.size() <= 3 &&
        std::stoi(hotkey.key.substr(1)) <= 12) {
        add(HotkeyConflictLevel::Warning, "Every application", "Function keys are used by many applications (F1 help, F5 refresh…)",
            "功能键被许多应用使用（F1 帮助、F5 刷新…）");
    }
    if (platform == HotkeyPlatform::Linux && hotkey.ctrl && hotkey.alt && !hotkey.super && is_function_key(hotkey.key) &&
        hotkey.key.size() <= 3 && std::stoi(hotkey.key.substr(1)) <= 12) {
        add(HotkeyConflictLevel::Blocked, "System", "Switch to a virtual console", "切换到虚拟控制台");
    }
    // On Windows Ctrl+Alt is AltGr, which types characters on many layouts
    // (German Ctrl+Alt+Q = @, Ctrl+Alt+E = €, Polish letters…).
    if (platform == HotkeyPlatform::Windows && hotkey.ctrl && hotkey.alt && !hotkey.super &&
        (hotkey.key.size() == 1 && std::string_view("EQM234567890-=[]\\;'`,./ACLNOSZ").find(hotkey.key[0]) != std::string_view::npos)) {
        add(HotkeyConflictLevel::Warning, "Keyboard layout", "Ctrl+Alt acts as AltGr and types a character on some layouts",
            "Ctrl+Alt 相当于 AltGr，在部分键盘布局下会输入字符");
    }
    for (const auto& known : kKnownShortcuts) {
        if (platform == HotkeyPlatform::Linux ? !known.on_linux : !known.on_windows) continue;
        // The desktop's real bindings are more precise than the generic table.
        if (!desktop.empty() && known.owner == "Desktop") continue;
        const auto parsed = parse_hotkey(known.combo);
        if (parsed && *parsed == hotkey) add(known.level, known.owner, known.action_en, known.action_zh);
    }
    for (const auto& bound : desktop) {
        if (bound.hotkey == hotkey) {
            add(HotkeyConflictLevel::Warning, bound.owner, "Already bound to \"" + bound.action + "\"",
                "已绑定到“" + bound.action + "”");
        }
    }
    // Any other Ctrl+letter still shadows that letter's shortcut in every app.
    const bool plain_ctrl = hotkey.ctrl && !hotkey.alt && !hotkey.super;
    if (plain_ctrl && is_letter(hotkey.key) && std::none_of(conflicts.begin(), conflicts.end(), [](const auto& conflict) {
            return conflict.owner == "Every application";
        })) {
        const auto combo = format_hotkey(hotkey);
        add(HotkeyConflictLevel::Warning, "Every application", combo + " stops working in other applications",
            combo + " 在其他应用中将失效");
    }
    return conflicts;
}

bool has_blocking_conflict(const std::vector<HotkeyConflict>& conflicts) {
    return std::any_of(conflicts.begin(), conflicts.end(),
                       [](const auto& conflict) { return conflict.level == HotkeyConflictLevel::Blocked; });
}

}  // namespace pasteit
