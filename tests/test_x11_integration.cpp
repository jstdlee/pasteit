#include "platform/linux/linux_x11_clipboard_impl.hpp"
#include "platform/linux/linux_x11_focus_impl.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstddef>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

int skip(const char* reason) {
    std::cout << "SKIP: " << reason << '\n';
    return 77;
}

std::string text_from(const pastit::ClipboardData& data) {
    return std::string(reinterpret_cast<const char*>(data.bytes.data()), data.bytes.size());
}

std::optional<pastit::ClipboardData> capture_from(pastit::X11ClipboardWatcher& owner,
                                                  pastit::X11ClipboardWatcher& reader) {
    std::atomic<bool> serving{true};
    std::thread owner_thread([&] {
        while (serving.load()) {
            owner.process_events();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    std::optional<pastit::ClipboardData> captured;
    for (int attempt = 0; attempt < 40 && !captured.has_value(); ++attempt) {
        captured = reader.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    serving.store(false);
    owner_thread.join();
    return captured;
}

}  // namespace

int main() {
    using namespace pastit;

    const char* display_name = std::getenv("DISPLAY");
    if (display_name == nullptr || std::string{display_name}.empty()) {
        return skip("DISPLAY is unavailable");
    }

    X11ContextService context;
    if (!context.available()) {
        return skip("X11 support was not compiled in or the display could not be opened");
    }

    const auto focus = context.collect_focus_context();
    assert(!focus.focused_target_hash.empty());

    if (context.register_ctrl_alt_f_shortcut()) {
        assert(!context.poll_ctrl_alt_f_shortcut());
    } else {
        std::cout << "SKIP: Ctrl+Alt+F is already grabbed by another client\n";
    }

    X11ClipboardWatcher writer;
    X11ClipboardWatcher reader;
    if (!writer.available() || !reader.available()) {
        return skip("clipboard display connection unavailable");
    }

    const std::string expected = "pasteit-x11-selection-roundtrip";
    assert(writer.set_text(expected));
    assert(writer.owned_text_if_current() == expected);

    auto captured = capture_from(writer, reader);
    assert(captured.has_value());
    assert(captured->kind == ContentKind::Text);
    assert(text_from(*captured) == expected);

    const std::vector<std::byte> png{
        std::byte{0x89}, std::byte{'P'}, std::byte{'N'}, std::byte{'G'},
        std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a},
    };
    assert(writer.set_image(png, "image/png"));
    assert(writer.owned_text_if_current() == std::string{});

    captured = capture_from(writer, reader);
    assert(captured.has_value());
    assert(captured->kind == ContentKind::Image);
    assert(captured->mime_types.front() == "image/png");
    assert(captured->bytes == png);

    std::vector<std::byte> large_png(2U * 1024U * 1024U, std::byte{0x5a});
    large_png[0] = std::byte{0x89};
    large_png[1] = std::byte{'P'};
    large_png[2] = std::byte{'N'};
    large_png[3] = std::byte{'G'};
    assert(writer.set_image(large_png, "image/png"));

    captured = capture_from(writer, reader);
    assert(captured.has_value());
    assert(captured->kind == ContentKind::Image);
    assert(captured->bytes == large_png);
}
