#include "platform/linux/linux_x11_focus_impl.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#if defined(PASTIT_HAS_X11)
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <dlfcn.h>
#endif

namespace {

int skip(const char* reason) {
    std::cout << "SKIP: " << reason << '\n';
    return 77;
}

}  // namespace

int main(int argc, char** argv) {
#if defined(PASTIT_HAS_X11)
    const bool send_only = argc == 2 && std::string_view{argv[1]} == "--send-only";
    const char* display_name = std::getenv("DISPLAY");
    if (display_name == nullptr || std::string{display_name}.empty()) {
        return skip("DISPLAY is unavailable");
    }

    pastit::X11ContextService receiver;
    if (!receiver.available()) {
        return skip("X11 display could not be opened");
    }
    if (!send_only && !receiver.register_ctrl_alt_f_shortcut()) {
        return skip("Ctrl+Alt+F is already grabbed by another client");
    }

    void* library = dlopen("libXtst.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (library == nullptr) {
        return skip("libXtst runtime is unavailable");
    }
    using FakeKey = int (*)(Display*, unsigned int, int, unsigned long);
    auto fake_key = reinterpret_cast<FakeKey>(dlsym(library, "XTestFakeKeyEvent"));
    if (fake_key == nullptr) {
        dlclose(library);
        return skip("XTestFakeKeyEvent is unavailable");
    }

    Display* sender = XOpenDisplay(nullptr);
    if (sender == nullptr) {
        dlclose(library);
        return skip("sender display could not be opened");
    }
    const int keyboard_grab = XGrabKeyboard(sender, DefaultRootWindow(sender), False, GrabModeAsync, GrabModeAsync,
                                            CurrentTime);
    if (keyboard_grab != GrabSuccess) {
        XCloseDisplay(sender);
        dlclose(library);
        return skip("another client owns the active keyboard grab (for example the lock screen)");
    }
    XUngrabKeyboard(sender, CurrentTime);
    XSync(sender, False);
    const KeyCode control = XKeysymToKeycode(sender, XK_Control_L);
    const KeyCode alt = XKeysymToKeycode(sender, XK_Alt_L);
    const KeyCode f = XKeysymToKeycode(sender, XK_f);
    const auto inject = [&](KeyCode key, bool pressed) {
        const int accepted = fake_key(sender, key, pressed ? True : False, CurrentTime);
        XSync(sender, False);
        std::this_thread::sleep_for(std::chrono::milliseconds{15});
        return accepted != 0;
    };
    const int injected = inject(control, true) && inject(alt, true) && inject(f, true) && inject(f, false) &&
                         inject(alt, false) && inject(control, false);
    if (injected == 0) {
        XCloseDisplay(sender);
        dlclose(library);
        return skip("XTest server rejected synthetic key events");
    }

    bool received = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
    while (!received && std::chrono::steady_clock::now() < deadline) {
        received = receiver.poll_ctrl_alt_f_shortcut();
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    XCloseDisplay(sender);
    dlclose(library);
    if (send_only) {
        std::cout << "Ctrl+Alt+F sent to the current global shortcut owner\n";
        return 0;
    }
    if (!received) {
        std::cerr << "Ctrl+Alt+F was injected but not received by the global grab\n";
        return 1;
    }
    return 0;
#else
    return skip("X11 support was not compiled");
#endif
}
