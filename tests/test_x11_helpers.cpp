#include "platform/linux/linux_x11_clipboard_impl.hpp"
#include "platform/linux/linux_x11_focus_impl.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (unsigned char ch : text) {
        out.push_back(static_cast<std::byte>(ch));
    }
    return out;
}

bool contains_modifier(const std::vector<unsigned int>& values, unsigned int wanted) {
    return std::find(values.begin(), values.end(), wanted) != values.end();
}

}  // namespace

int main() {
    using namespace pasteit;

    const auto paths = parse_x11_uri_list(
        "# copied from a file manager\r\n"
        "file:///tmp/PasteIt%20Demo.txt\r\n"
        "file://localhost/home/user/notes.md\n"
        "/var/tmp/raw-path.json\n"
        "https://example.com/download.bin\n");
    assert(paths.size() == 3);
    assert(paths[0] == std::filesystem::path{"/tmp/PasteIt Demo.txt"});
    assert(paths[1] == std::filesystem::path{"/home/user/notes.md"});
    assert(paths[2] == std::filesystem::path{"/var/tmp/raw-path.json"});

    const auto image = classify_x11_clipboard_payload({"image/png"}, bytes("\x89PNG\r\n"));
    assert(image == ContentKind::Image);

    const auto uri_list = classify_x11_clipboard_payload({"text/uri-list"}, bytes("file:///tmp/a.txt\n"));
    assert(uri_list == ContentKind::Path);

    const auto plain_path = classify_x11_clipboard_payload({"UTF8_STRING"}, bytes("/tmp/a.txt\n"));
    assert(plain_path == ContentKind::Path);

    const auto plain_text = classify_x11_clipboard_payload({"UTF8_STRING"}, bytes("hello world"));
    assert(plain_text == ContentKind::Text);

    const auto json_with_email = classify_x11_clipboard_payload(
        {"UTF8_STRING"}, bytes(R"({"name":"PasteIt demo","email":"demo@example.com"})"));
    assert(json_with_email == ContentKind::Json);

    const std::string wm_class{"Navigator\0firefox\0", 18};
    assert(parse_x11_wm_class(wm_class) == "firefox");
    assert(parse_x11_wm_class(std::string{"Alacritty\0", 10}) == "Alacritty");

    const auto modifiers = x11_lock_modifier_variants();
    assert(contains_modifier(modifiers, 0U));
    assert(contains_modifier(modifiers, 2U));
    assert(contains_modifier(modifiers, 16U));
    assert(contains_modifier(modifiers, 18U));
}
