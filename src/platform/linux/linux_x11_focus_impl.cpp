#include "platform/linux/linux_x11_focus_impl.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <functional>
#include <sstream>
#include <system_error>

#if defined(PASTIT_HAS_X11)
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <dlfcn.h>
#endif

namespace pastit {
namespace {

std::string hash_text(const std::string& value) {
    std::ostringstream out;
    out << std::hex << std::hash<std::string>{}(value);
    return out.str();
}

#if defined(PASTIT_HAS_X11)

int x11_error_seen = 0;

int record_x11_error(Display*, XErrorEvent* event) {
    x11_error_seen = event == nullptr ? 1 : event->error_code;
    return 0;
}

struct X11ErrorTrap {
    explicit X11ErrorTrap(Display* display_in) : display(display_in), previous(XSetErrorHandler(record_x11_error)) {
        x11_error_seen = 0;
    }

    ~X11ErrorTrap() {
        XSync(display, False);
        XSetErrorHandler(previous);
    }

    Display* display = nullptr;
    int (*previous)(Display*, XErrorEvent*) = nullptr;
};

Atom intern(Display* display, const char* name) {
    return XInternAtom(display, name, False);
}

std::optional<std::string> read_string_property(Display* display, Window window, Atom property, Atom preferred_type) {
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long item_count = 0;
    unsigned long bytes_after = 0;
    unsigned char* raw = nullptr;
    const int status = XGetWindowProperty(display, window, property, 0, 4096, False, preferred_type, &actual_type,
                                          &actual_format, &item_count, &bytes_after, &raw);
    if (status != Success || raw == nullptr || actual_type == None || actual_format != 8) {
        if (raw != nullptr) {
            XFree(raw);
        }
        return std::nullopt;
    }
    std::string value(reinterpret_cast<const char*>(raw), item_count);
    XFree(raw);
    (void)bytes_after;
    return value;
}

std::optional<unsigned long> read_cardinal_property(Display* display, Window window, Atom property) {
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long item_count = 0;
    unsigned long bytes_after = 0;
    unsigned char* raw = nullptr;
    const int status = XGetWindowProperty(display, window, property, 0, 1, False, AnyPropertyType, &actual_type,
                                          &actual_format, &item_count, &bytes_after, &raw);
    if (status != Success || raw == nullptr || actual_type == None || actual_format != 32 || item_count == 0) {
        if (raw != nullptr) {
            XFree(raw);
        }
        return std::nullopt;
    }
    const auto value = *reinterpret_cast<unsigned long*>(raw);
    XFree(raw);
    (void)bytes_after;
    return value;
}

Window active_window(Display* display, Window root) {
    const Atom active_atom = intern(display, "_NET_ACTIVE_WINDOW");
    if (const auto active = read_cardinal_property(display, root, active_atom); active.has_value() && *active != 0) {
        return static_cast<Window>(*active);
    }

    Window focus = 0;
    int revert = 0;
    XGetInputFocus(display, &focus, &revert);
    if (focus == PointerRoot || focus == None) {
        return 0;
    }
    return focus;
}

std::string window_title(Display* display, Window window) {
    const Atom utf8 = intern(display, "UTF8_STRING");
    if (const auto value = read_string_property(display, window, intern(display, "_NET_WM_NAME"), utf8);
        value.has_value()) {
        return *value;
    }
    if (const auto value = read_string_property(display, window, intern(display, "WM_NAME"), AnyPropertyType);
        value.has_value()) {
        return *value;
    }
    return {};
}

std::string window_class(Display* display, Window window) {
    if (const auto value = read_string_property(display, window, intern(display, "WM_CLASS"), XA_STRING);
        value.has_value()) {
        return parse_x11_wm_class(*value);
    }
    return {};
}

std::optional<std::filesystem::path> cwd_for_pid(std::uint32_t pid) {
    if (pid == 0) {
        return std::nullopt;
    }
    std::error_code error;
    auto cwd = std::filesystem::read_symlink(std::filesystem::path{"/proc"} / std::to_string(pid) / "cwd", error);
    if (error) {
        return std::nullopt;
    }
    return cwd;
}

using XTestFakeKeyEventFn = int (*)(Display*, unsigned int, int, unsigned long);

XTestFakeKeyEventFn load_xtest_fake_key_event() {
    static void* handle = [] {
        if (void* first = dlopen("libXtst.so.6", RTLD_LAZY | RTLD_LOCAL); first != nullptr) {
            return first;
        }
        return dlopen("libXtst.so", RTLD_LAZY | RTLD_LOCAL);
    }();
    if (handle == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<XTestFakeKeyEventFn>(dlsym(handle, "XTestFakeKeyEvent"));
}

#endif

X11ContextService& global_context_service() {
    static X11ContextService service;
    return service;
}

}  // namespace

std::string parse_x11_wm_class(std::string_view raw) {
    const auto first_end = raw.find('\0');
    if (first_end == std::string_view::npos) {
        return std::string{raw};
    }
    const auto second_begin = first_end + 1;
    const auto second_end = raw.find('\0', second_begin);
    const auto instance = raw.substr(0, first_end);
    const auto klass = raw.substr(second_begin, second_end == std::string_view::npos ? std::string_view::npos
                                                                                    : second_end - second_begin);
    if (!klass.empty()) {
        return std::string{klass};
    }
    return std::string{instance};
}

std::vector<unsigned int> x11_lock_modifier_variants() {
    constexpr unsigned int lock_mask = 1U << 1;
    constexpr unsigned int mod2_mask = 1U << 4;
    return {0U, lock_mask, mod2_mask, lock_mask | mod2_mask};
}

X11ContextService::X11ContextService() {
#if defined(PASTIT_HAS_X11)
    display_ = XOpenDisplay(nullptr);
    if (display_ != nullptr) {
        root_ = DefaultRootWindow(static_cast<Display*>(display_));
    }
#endif
}

X11ContextService::~X11ContextService() {
#if defined(PASTIT_HAS_X11)
    if (display_ != nullptr) {
        auto* display = static_cast<Display*>(display_);
        if (shortcut_registered_ && shortcut_keycode_ != 0) {
            for (unsigned int variant : x11_lock_modifier_variants()) {
                XUngrabKey(display, static_cast<int>(shortcut_keycode_), ControlMask | Mod1Mask | variant,
                           static_cast<Window>(root_));
            }
        }
        XCloseDisplay(display);
    }
#endif
}

bool X11ContextService::available() const {
    return display_ != nullptr;
}

bool X11ContextService::register_ctrl_alt_f_shortcut() {
#if defined(PASTIT_HAS_X11)
    if (display_ == nullptr) {
        return false;
    }
    auto* display = static_cast<Display*>(display_);
    shortcut_keycode_ = XKeysymToKeycode(display, XK_f);
    if (shortcut_keycode_ == 0) {
        return false;
    }

    bool all_grabbed = false;
    {
        X11ErrorTrap trap(display);
        for (unsigned int variant : x11_lock_modifier_variants()) {
            XGrabKey(display, static_cast<int>(shortcut_keycode_), ControlMask | Mod1Mask | variant,
                     static_cast<Window>(root_), False, GrabModeAsync, GrabModeAsync);
        }
        XSync(display, False);
        all_grabbed = x11_error_seen == 0;
    }
    if (!all_grabbed) {
        for (unsigned int variant : x11_lock_modifier_variants()) {
            XUngrabKey(display, static_cast<int>(shortcut_keycode_), ControlMask | Mod1Mask | variant,
                       static_cast<Window>(root_));
        }
        XFlush(display);
        return false;
    }
    shortcut_registered_ = true;
    return true;
#else
    return false;
#endif
}

bool X11ContextService::poll_ctrl_alt_f_shortcut() {
#if defined(PASTIT_HAS_X11)
    if (display_ == nullptr || !shortcut_registered_) {
        return false;
    }
    auto* display = static_cast<Display*>(display_);
    while (XPending(display) > 0) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type != KeyPress) {
            continue;
        }
        const auto state = static_cast<unsigned int>(event.xkey.state);
        if (event.xkey.keycode == shortcut_keycode_ && (state & ControlMask) != 0 && (state & Mod1Mask) != 0) {
            return true;
        }
    }
    return false;
#else
    return false;
#endif
}

FocusContext X11ContextService::collect_focus_context() const {
    FocusContext context;
#if defined(PASTIT_HAS_X11)
    if (display_ == nullptr) {
        context.focused_target_hash = hash_text("x11-unavailable");
        return context;
    }
    auto* display = static_cast<Display*>(display_);
    const Window window = active_window(display, static_cast<Window>(root_));
    context.window_id = static_cast<std::uint64_t>(window);
    context.window_title = window == 0 ? "" : window_title(display, window);
    context.app_name = window == 0 ? "" : window_class(display, window);
    if (context.app_name.empty()) {
        context.app_name = "x11";
    }
    if (window != 0) {
        if (const auto pid = read_cardinal_property(display, window, intern(display, "_NET_WM_PID"));
            pid.has_value()) {
            context.pid = static_cast<std::uint32_t>(*pid);
            context.current_directory = cwd_for_pid(context.pid);
        }
    }
    std::ostringstream seed;
    seed << context.window_id << '|' << context.app_name << '|' << context.window_title << '|' << context.pid << '|';
    if (context.current_directory.has_value()) {
        seed << context.current_directory->string();
    }
    context.focused_target_hash = hash_text(seed.str());
#else
    context.focused_target_hash = hash_text("x11-disabled");
#endif
    return context;
}

bool X11ContextService::focus_and_paste(const FocusContext& context) const {
#if defined(PASTIT_HAS_X11)
    if (display_ == nullptr || context.window_id == 0) {
        return false;
    }
    auto* display = static_cast<Display*>(display_);
    const auto fake_key = load_xtest_fake_key_event();
    if (fake_key == nullptr) {
        return false;
    }
    const auto window = static_cast<Window>(context.window_id);
    {
        X11ErrorTrap trap(display);
        XRaiseWindow(display, window);
        XSetInputFocus(display, window, RevertToParent, CurrentTime);
        XSync(display, False);
        if (x11_error_seen != 0) {
            return false;
        }
    }

    const KeyCode control = XKeysymToKeycode(display, XK_Control_L);
    const KeyCode v = XKeysymToKeycode(display, XK_v);
    if (control == 0 || v == 0) {
        return false;
    }
    fake_key(display, control, True, CurrentTime);
    fake_key(display, v, True, CurrentTime);
    fake_key(display, v, False, CurrentTime);
    fake_key(display, control, False, CurrentTime);
    XFlush(display);
    return true;
#else
    (void)context;
    return false;
#endif
}

FocusContext collect_x11_context() {
    return global_context_service().collect_focus_context();
}

bool register_ctrl_alt_f_shortcut() {
    return global_context_service().register_ctrl_alt_f_shortcut();
}

bool poll_ctrl_alt_f_shortcut() {
    return global_context_service().poll_ctrl_alt_f_shortcut();
}

bool focus_x11_target_and_paste(const FocusContext& context) {
    return global_context_service().focus_and_paste(context);
}

}  // namespace pastit
