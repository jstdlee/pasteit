#include "platform/linux/linux_desktop_services.hpp"
#include "platform/linux/linux_recent_paths.hpp"

#include <algorithm>
#include <cmath>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <pwd.h>
#include <spawn.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#if defined(PASTEIT_HAS_X11)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif

extern char** environ;

namespace pasteit {
namespace {

enum class DirectoryChooserStatus {
    Selected,
    Unavailable,
    NoSelection,
};

struct DirectoryChooserResult {
    DirectoryChooserStatus status = DirectoryChooserStatus::NoSelection;
    std::filesystem::path path;
};

bool open_with_xdg(std::string value) {
    std::vector<char*> argv{const_cast<char*>("xdg-open"), value.data(), nullptr};
    pid_t pid = 0;
    if (posix_spawnp(&pid, argv.front(), nullptr, nullptr, argv.data(), environ) != 0) {
        return false;
    }
    std::thread([pid] {
        int status = 0;
        (void)waitpid(pid, &status, 0);
    }).detach();
    return true;
}

FocusContext to_x11_focus(const PlatformFocusContext& source) {
    return FocusContext{.window_id = source.window_id,
                        .app_name = source.app_name,
                        .window_title = source.window_title,
                        .focused_target_hash = source.focused_target_hash,
                        .pid = source.pid,
                        .current_directory = source.current_directory};
}

std::string read_all(int fd) {
    std::string output;
    char buffer[256];
    while (true) {
        const auto count = read(fd, buffer, sizeof(buffer));
        if (count > 0) {
            output.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count == -1 && errno == EINTR) {
            continue;
        }
        break;
    }
    return output;
}

void trim_one_trailing_newline(std::string& value) {
    if (!value.empty() && value.back() == '\n') {
        value.pop_back();
        if (!value.empty() && value.back() == '\r') {
            value.pop_back();
        }
    }
}

std::filesystem::path normalized_existing_directory(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_directory(path, error)) {
        return {};
    }
    auto canonical = std::filesystem::weakly_canonical(path, error);
    if (!error && !canonical.empty()) {
        return canonical.lexically_normal();
    }
    return path.lexically_normal();
}

std::filesystem::path chooser_initial_directory(const std::filesystem::path& initial_directory) {
    if (const auto normalized = normalized_existing_directory(initial_directory); !normalized.empty()) {
        return normalized;
    }
    std::error_code error;
    auto temporary = std::filesystem::temp_directory_path(error);
    if (!error) {
        if (const auto normalized = normalized_existing_directory(temporary); !normalized.empty()) {
            return normalized;
        }
    }
    return initial_directory;
}

DirectoryChooserResult run_directory_chooser(const std::vector<std::string>& args) {
    int pipe_fds[2] = {-1, -1};
    if (pipe(pipe_fds) != 0) {
        return {.status = DirectoryChooserStatus::NoSelection, .path = {}};
    }

    posix_spawn_file_actions_t actions{};
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return {.status = DirectoryChooserStatus::NoSelection, .path = {}};
    }
    posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int spawn_error = posix_spawnp(&pid, argv.front(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipe_fds[1]);

    if (spawn_error != 0) {
        close(pipe_fds[0]);
        if (spawn_error == ENOENT || spawn_error == ENOTDIR) {
            return {.status = DirectoryChooserStatus::Unavailable, .path = {}};
        }
        return {.status = DirectoryChooserStatus::NoSelection, .path = {}};
    }

    auto output = read_all(pipe_fds[0]);
    close(pipe_fds[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return {.status = DirectoryChooserStatus::NoSelection, .path = {}};
    }

    trim_one_trailing_newline(output);
    if (output.empty()) {
        return {.status = DirectoryChooserStatus::NoSelection, .path = {}};
    }
    const auto selected = normalized_existing_directory(std::filesystem::path{output});
    if (selected.empty()) {
        return {.status = DirectoryChooserStatus::NoSelection, .path = {}};
    }
    return {.status = DirectoryChooserStatus::Selected, .path = selected};
}

DirectoryChooserResult run_zenity_directory_chooser(const std::filesystem::path& initial_directory) {
    auto initial = initial_directory.string();
    if (!initial.empty() && initial.back() != '/') {
        initial.push_back('/');
    }
    return run_directory_chooser({"zenity", "--file-selection", "--directory", "--filename=" + initial});
}

DirectoryChooserResult run_kdialog_directory_chooser(const std::filesystem::path& initial_directory) {
    return run_directory_chooser({"kdialog", "--getexistingdirectory", initial_directory.string()});
}

std::filesystem::path user_home_directory() {
    std::vector<char> buffer(16384);
    passwd entry{};
    passwd* result = nullptr;
    if (getpwuid_r(getuid(), &entry, buffer.data(), buffer.size(), &result) != 0 ||
        result == nullptr || result->pw_dir == nullptr) {
        return {};
    }
    return result->pw_dir;
}

}  // namespace

std::optional<std::filesystem::path> browser_open_path(const std::filesystem::path& path,
                                                      const std::filesystem::path& home_directory) {
    const auto filename = path.filename().string();
    std::error_code error;
    if (path.extension() != ".html" || !filename.starts_with("pasteit-render-") ||
        !std::filesystem::equivalent(path.parent_path(), std::filesystem::temp_directory_path(), error)) {
        return path;
    }
    if (error || home_directory.empty() ||
        std::filesystem::symlink_status(path, error).type() != std::filesystem::file_type::regular) {
        return std::nullopt;
    }
    const auto directory = home_directory / "PasteIt Previews";
    if (mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
        return std::nullopt;
    }
    struct stat directory_info{};
    if (lstat(directory.c_str(), &directory_info) != 0 || !S_ISDIR(directory_info.st_mode) ||
        directory_info.st_uid != geteuid() || (directory_info.st_mode & 077) != 0) {
        return std::nullopt;
    }
    const auto expired_before = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error) break;
        const auto name = entry.path().filename().string();
        if (!name.starts_with("pasteit-render-") || entry.path().extension() != ".html") continue;
        std::error_code entry_error;
        if (entry.symlink_status(entry_error).type() != std::filesystem::file_type::regular || entry_error) continue;
        if (entry.last_write_time(entry_error) < expired_before && !entry_error) {
            std::filesystem::remove(entry.path(), entry_error);
        }
    }
    error.clear();
    const auto staged = directory / filename;
    if (!std::filesystem::copy_file(path, staged, std::filesystem::copy_options::none, error)) {
        struct stat staged_info{};
        if (error == std::errc::file_exists && lstat(staged.c_str(), &staged_info) == 0 &&
            S_ISREG(staged_info.st_mode) && staged_info.st_uid == geteuid() &&
            (staged_info.st_mode & 077) == 0) {
            return staged;
        }
        return std::nullopt;
    }
    std::filesystem::permissions(staged, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(staged, ignored);
        return std::nullopt;
    }
    return staged;
}

void LinuxDesktopServices::apply_settings(const AppSettings& settings) {
    LinuxFastActionServices::Options options;
    options.terminal_command = settings.terminal.command;
    fast_actions_.configure(std::move(options));
}

std::optional<ClipboardCapture> LinuxDesktopServices::poll_clipboard() { return clipboard_.poll(); }
void LinuxDesktopServices::process_events() { clipboard_.process_events(); }
bool LinuxDesktopServices::publish_text(std::string_view text) { return clipboard_.set_text(text); }
bool LinuxDesktopServices::publish_image(const std::vector<std::byte>& bytes, std::string_view mime_type) {
    return clipboard_.set_image(bytes, mime_type);
}

PlatformFocusContext LinuxDesktopServices::focused_context() {
    const auto source = focus_.collect_focus_context();
    return PlatformFocusContext{.window_id = source.window_id,
                                .app_name = source.app_name,
                                .window_title = source.window_title,
                                .focused_target_hash = source.focused_target_hash,
                                .pid = source.pid,
                                .current_directory = source.current_directory};
}

std::vector<PlatformRecentPath> LinuxDesktopServices::recent_paths() {
    LinuxRecentPathCollector collector;
    auto values = collector.scan_file_manager_services();
    auto shells = collector.scan_shell_history();
    values.insert(values.end(), shells.begin(), shells.end());
    return values;
}
bool LinuxDesktopServices::register_global_shortcut() { return focus_.register_ctrl_alt_f_shortcut(); }
bool LinuxDesktopServices::global_shortcut_activated() { return focus_.poll_ctrl_alt_f_shortcut(); }
bool LinuxDesktopServices::restore_focus_and_paste(const PlatformFocusContext& context) {
    return focus_.focus_and_paste(to_x11_focus(context));
}
bool LinuxDesktopServices::open_path(const std::filesystem::path& path) {
    const auto browser_path = browser_open_path(path, user_home_directory());
    return browser_path.has_value() && open_with_xdg(browser_path->string());
}
bool LinuxDesktopServices::open_uri(std::string_view uri) { return open_with_xdg(std::string{uri}); }
bool LinuxDesktopServices::copy_text(std::string_view text) { return publish_text(text); }
std::optional<std::string> LinuxDesktopServices::owned_clipboard_text() const {
    return clipboard_.owned_text_if_current();
}
bool LinuxDesktopServices::move_popup_by(int delta_x, int delta_y) {
#if defined(PASTEIT_HAS_X11)
    if (popup_window_id_ == 0) return false;
    Display* display=XOpenDisplay(nullptr);if(!display)return false;XWindowAttributes attributes{};
    const bool ok=XGetWindowAttributes(display,static_cast<Window>(popup_window_id_),&attributes)!=0;
    if(ok){XMoveWindow(display,static_cast<Window>(popup_window_id_),attributes.x+delta_x,attributes.y+delta_y);XFlush(display);}XCloseDisplay(display);return ok;
#else
    (void)delta_x;(void)delta_y;return false;
#endif
}
void LinuxDesktopServices::keep_above_popup(std::uint64_t window_id) {
#if defined(PASTEIT_HAS_X11)
    if (popup_window_id_ == 0 || window_id == 0 || window_id == popup_window_id_) return;
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) return;
    // Mutter and other window managers stack transients above their parent,
    // even when the (floating) parent is clicked again.
    XSetTransientForHint(display, static_cast<Window>(window_id), static_cast<Window>(popup_window_id_));
    XRaiseWindow(display, static_cast<Window>(window_id));
    XFlush(display);
    XCloseDisplay(display);
#else
    (void)window_id;
#endif
}

unsigned long x11_opacity_cardinal(float opacity) {
    // Compute in double and clamp: 1.0F*4294967295.0F rounds to 2^32 in float,
    // which X truncates to 0 (a fully transparent window).
    const double scaled = std::round(std::clamp(static_cast<double>(opacity), 0.0, 1.0) * 4294967295.0);
    return static_cast<unsigned long>(std::min(4294967295.0, scaled));
}

bool LinuxDesktopServices::set_popup_opacity(float opacity) {
#if defined(PASTEIT_HAS_X11)
    if (popup_window_id_ == 0 || opacity < 0.0F || opacity > 1.0F) return false;
    Display* display=XOpenDisplay(nullptr);if(!display)return false;const Atom property=XInternAtom(display,"_NET_WM_WINDOW_OPACITY",False);
    const unsigned long value=x11_opacity_cardinal(opacity);XChangeProperty(display,static_cast<Window>(popup_window_id_),property,XA_CARDINAL,32,PropModeReplace,reinterpret_cast<const unsigned char*>(&value),1);XFlush(display);XCloseDisplay(display);return true;
#else
    (void)opacity;return false;
#endif
}

std::vector<std::filesystem::path> LinuxDesktopServices::preferred_ui_fonts() {
    const std::vector<std::filesystem::path> candidates{
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
    };
    std::vector<std::filesystem::path> found;
    std::copy_if(candidates.begin(), candidates.end(), std::back_inserter(found), [](const auto& path) {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error);
    });
    return found;
}

std::optional<std::filesystem::path> LinuxDesktopServices::choose_directory(
    const std::filesystem::path& initial_directory) {
    const auto initial = chooser_initial_directory(initial_directory);
    const auto zenity = run_zenity_directory_chooser(initial);
    if (zenity.status == DirectoryChooserStatus::Selected) {
        return zenity.path;
    }
    if (zenity.status != DirectoryChooserStatus::Unavailable) {
        return std::nullopt;
    }

    const auto kdialog = run_kdialog_directory_chooser(initial);
    if (kdialog.status == DirectoryChooserStatus::Selected) {
        return kdialog.path;
    }
    return std::nullopt;
}

FastActionServices& LinuxDesktopServices::fast_actions() { return fast_actions_; }

}  // namespace pasteit
