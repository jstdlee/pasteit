#include "platform/platform_services.hpp"
#if !defined(_WIN32)
#include "platform/linux/linux_desktop_services.hpp"
#endif

#include <cassert>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>

namespace {

class FakePlatformServices final : public pastit::PlatformServices {
public:
    void apply_settings(const pastit::AppSettings&) override {}
    std::optional<pastit::ClipboardCapture> poll_clipboard() override { return capture; }
    void process_events() override {}
    bool publish_text(std::string_view) override { return true; }
    bool publish_image(const std::vector<std::byte>&, std::string_view) override { return true; }
    pastit::PlatformFocusContext focused_context() override { return focus; }
    std::vector<pastit::PlatformRecentPath> recent_paths() override { return paths; }
    bool register_global_shortcut() override { return true; }
    bool global_shortcut_activated() override { return false; }
    bool restore_focus_and_paste(const pastit::PlatformFocusContext&) override { return true; }
    bool open_path(const std::filesystem::path&) override { return true; }
    bool open_uri(std::string_view) override { return true; }
    bool copy_text(std::string_view) override { return true; }
    bool move_popup_by(int, int) override { return true; }
    bool set_popup_opacity(float) override { return true; }
    std::vector<std::filesystem::path> preferred_ui_fonts() override { return {"/tmp/font.ttc"}; }
    std::optional<std::filesystem::path> choose_directory(const std::filesystem::path& initial_directory) override {
        return chosen_directory.value_or(initial_directory);
    }

    std::optional<pastit::ClipboardCapture> capture;
    pastit::PlatformFocusContext focus;
    std::vector<pastit::PlatformRecentPath> paths;
    std::optional<std::filesystem::path> chosen_directory;
};

class MissingDirectoryChooserServices : public pastit::PlatformServices {
public:
    std::optional<pastit::ClipboardCapture> poll_clipboard() override { return std::nullopt; }
    void process_events() override {}
    bool publish_text(std::string_view) override { return true; }
    bool publish_image(const std::vector<std::byte>&, std::string_view) override { return true; }
    pastit::PlatformFocusContext focused_context() override { return {}; }
    std::vector<pastit::PlatformRecentPath> recent_paths() override { return {}; }
    bool register_global_shortcut() override { return true; }
    bool global_shortcut_activated() override { return false; }
    bool restore_focus_and_paste(const pastit::PlatformFocusContext&) override { return true; }
    bool open_path(const std::filesystem::path&) override { return true; }
    bool open_uri(std::string_view) override { return true; }
    bool copy_text(std::string_view) override { return true; }
    bool move_popup_by(int, int) override { return true; }
    bool set_popup_opacity(float) override { return true; }
    std::vector<std::filesystem::path> preferred_ui_fonts() override { return {}; }
};

}  // namespace

int main() {
    static_assert(std::is_abstract_v<pastit::PlatformServices>);
    static_assert(std::is_abstract_v<MissingDirectoryChooserServices>);
    FakePlatformServices fake;
    fake.paths.push_back({.path = "/tmp/example", .kind = pastit::PathKind::Directory, .source = "test"});
    fake.focus.app_name = "editor";
    assert(fake.recent_paths().front().path == "/tmp/example");
    assert(fake.focused_context().app_name == "editor");
    assert(fake.set_popup_opacity(0.72F));
    assert(fake.preferred_ui_fonts().front() == "/tmp/font.ttc");
    fake.chosen_directory = "/tmp/chosen";
    const auto chosen = fake.choose_directory("/tmp/initial");
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(chosen)>, std::optional<std::filesystem::path>>);
    assert(chosen == "/tmp/chosen");
#if !defined(_WIN32)
    const auto root = std::filesystem::temp_directory_path() /
        ("pastit-browser-preview-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto home = root / "home";
    std::filesystem::create_directories(home);
    const auto previews = home / "PasteIt Previews";
    std::filesystem::create_directory(previews);
    std::filesystem::permissions(previews, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace);
    const auto old_preview = previews / "pastit-render-expired.html";
    const auto unrelated_preview = previews / "keep.html";
    { std::ofstream(old_preview) << "old"; }
    { std::ofstream(unrelated_preview) << "keep"; }
    std::filesystem::last_write_time(old_preview, std::filesystem::file_time_type::clock::now() -
                                     std::chrono::hours(48));
    const auto rendered = std::filesystem::temp_directory_path() /
        ("pastit-render-platform-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".html");
    {
        std::ofstream out(rendered);
        out << "<!doctype html><html><pre class=\"mermaid\">graph TD</pre></html>";
    }
    const auto staged = pastit::browser_open_path(rendered, home);
    assert(staged.has_value());
    assert(staged->parent_path() == home / "PasteIt Previews");
    assert(std::filesystem::is_regular_file(*staged));
    assert(!std::filesystem::exists(old_preview));
    assert(std::filesystem::is_regular_file(unrelated_preview));
    std::ifstream staged_file(*staged);
    assert(std::string((std::istreambuf_iterator<char>(staged_file)), {}) ==
           "<!doctype html><html><pre class=\"mermaid\">graph TD</pre></html>");
    const auto permissions = std::filesystem::status(staged->parent_path()).permissions();
    assert((permissions & std::filesystem::perms::others_all) == std::filesystem::perms::none);
    assert(pastit::browser_open_path(rendered, home) == staged);
    const auto unrelated = root / "ordinary.html";
    assert(pastit::browser_open_path(unrelated, home) == unrelated);
    std::filesystem::remove(rendered);
    std::filesystem::remove_all(root);
#endif
}
