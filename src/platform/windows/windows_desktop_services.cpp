#include "platform/windows/windows_desktop_services.hpp"

#include "platform/windows/windows_strings.hpp"
#include "storage/clipboard_store.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <functional>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
#include <stb_image.h>
#endif

namespace pasteit {
namespace {

constexpr int kShortcutId = 1;

std::int64_t current_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::vector<std::byte> bytes_from_text(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char ch : text) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return bytes;
}

std::vector<std::byte> bytes_from_global(HGLOBAL global) {
    if (global == nullptr) {
        return {};
    }
    const auto size = GlobalSize(global);
    if (size == 0) {
        return {};
    }
    const void* memory = GlobalLock(global);
    if (memory == nullptr) {
        return {};
    }
    const auto* begin = static_cast<const std::byte*>(memory);
    std::vector<std::byte> bytes(begin, begin + size);
    GlobalUnlock(global);
    return bytes;
}

std::string uri_escape_path(std::string_view value) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (const unsigned char ch : value) {
        const bool ascii_alphanumeric = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                                        (ch >= '0' && ch <= '9');
        const bool safe = ascii_alphanumeric || ch == '-' || ch == '_' || ch == '.' || ch == '~' ||
                          ch == '/' || ch == ':';
        if (safe) {
            out << static_cast<char>(ch);
        } else {
            out << '%' << static_cast<int>(ch / 16) << static_cast<int>(ch % 16);
        }
    }
    return out.str();
}

std::string file_uri_for_path(const std::filesystem::path& path) {
    auto text = path_to_utf8(path);
    std::replace(text.begin(), text.end(), '\\', '/');
    if (text.size() >= 2 && text[1] == ':') {
        return "file:///" + uri_escape_path(text);
    }
    if (text.rfind("//", 0) == 0) {
        return "file:" + uri_escape_path(text);
    }
    return "file:///" + uri_escape_path(text);
}

std::optional<ClipboardCapture> capture_file_drop() {
    auto* drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
    if (drop == nullptr) {
        return std::nullopt;
    }
    const auto count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    if (count == 0) {
        return std::nullopt;
    }

    std::string uri_list;
    for (UINT index = 0; index < count; ++index) {
        const auto length = DragQueryFileW(drop, index, nullptr, 0);
        if (length == 0) {
            continue;
        }
        std::wstring path(static_cast<std::size_t>(length) + 1U, L'\0');
        DragQueryFileW(drop, index, path.data(), length + 1);
        path.resize(length);
        uri_list += file_uri_for_path(std::filesystem::path{path});
        uri_list += "\r\n";
    }
    if (uri_list.empty()) {
        return std::nullopt;
    }
    return ClipboardCapture{.mime_types = {"text/uri-list"},
                            .bytes = bytes_from_text(uri_list),
                            .kind = ContentKind::Path,
                            .source_app = "windows",
                            .captured_at_ms = current_time_ms()};
}

std::optional<ClipboardCapture> capture_unicode_text() {
    auto* global = static_cast<HGLOBAL>(GetClipboardData(CF_UNICODETEXT));
    if (global == nullptr) {
        return std::nullopt;
    }
    const auto* wide = static_cast<const wchar_t*>(GlobalLock(global));
    if (wide == nullptr) {
        return std::nullopt;
    }
    const std::wstring text{wide};
    GlobalUnlock(global);
    const auto utf8 = wide_to_utf8(text);
    if (utf8.empty() && !text.empty()) {
        return std::nullopt;
    }
    return ClipboardCapture{.mime_types = {"text/plain;charset=utf-8"},
                            .bytes = bytes_from_text(utf8),
                            .kind = ContentKind::Text,
                            .source_app = "windows",
                            .captured_at_ms = current_time_ms()};
}

std::optional<ClipboardCapture> capture_clipboard_format(UINT format, std::string mime_type) {
    auto* global = static_cast<HGLOBAL>(GetClipboardData(format));
    auto bytes = bytes_from_global(global);
    if (bytes.empty()) {
        return std::nullopt;
    }
    return ClipboardCapture{.mime_types = {std::move(mime_type)},
                            .bytes = std::move(bytes),
                            .kind = ContentKind::Image,
                            .source_app = "windows",
                            .captured_at_ms = current_time_ms()};
}

std::optional<ClipboardCapture> capture_dib(UINT format) {
    auto* global = static_cast<HGLOBAL>(GetClipboardData(format));
    auto dib = bytes_from_global(global);
    if (dib.size() < sizeof(BITMAPINFOHEADER)) {
        return std::nullopt;
    }

    BITMAPINFOHEADER header{};
    std::memcpy(&header, dib.data(), sizeof(header));
    if (header.biSize < sizeof(BITMAPINFOHEADER) || header.biSize > dib.size() ||
        header.biPlanes != 1 || header.biWidth <= 0 || header.biHeight == 0) {
        return std::nullopt;
    }

    std::size_t color_bytes = 0;
    if (header.biBitCount <= 8) {
        const auto count = header.biClrUsed != 0 ? header.biClrUsed : (1U << header.biBitCount);
        color_bytes = static_cast<std::size_t>(count) * sizeof(RGBQUAD);
    } else if (header.biClrUsed != 0) {
        color_bytes = static_cast<std::size_t>(header.biClrUsed) * sizeof(RGBQUAD);
    }
    std::size_t mask_bytes = 0;
    if (header.biSize == sizeof(BITMAPINFOHEADER) && header.biCompression == BI_BITFIELDS) {
        mask_bytes = 3U * sizeof(DWORD);
    }
    const auto pixel_offset = static_cast<std::size_t>(header.biSize) + mask_bytes + color_bytes;
    if (pixel_offset >= dib.size() || dib.size() > UINT32_MAX - sizeof(BITMAPFILEHEADER)) {
        return std::nullopt;
    }

    BITMAPFILEHEADER file_header{};
    file_header.bfType = 0x4D42;
    file_header.bfSize = static_cast<DWORD>(sizeof(file_header) + dib.size());
    file_header.bfOffBits = static_cast<DWORD>(sizeof(file_header) + pixel_offset);
    std::vector<std::byte> bmp(sizeof(file_header) + dib.size());
    std::memcpy(bmp.data(), &file_header, sizeof(file_header));
    std::memcpy(bmp.data() + sizeof(file_header), dib.data(), dib.size());
    return ClipboardCapture{.mime_types = {"image/bmp"},
                            .bytes = std::move(bmp),
                            .kind = ContentKind::Image,
                            .source_app = "windows",
                            .captured_at_ms = current_time_ms()};
}

HGLOBAL global_copy(const void* data, std::size_t size) {
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, size);
    if (global == nullptr) {
        return nullptr;
    }
    void* target = GlobalLock(global);
    if (target == nullptr) {
        GlobalFree(global);
        return nullptr;
    }
    std::memcpy(target, data, size);
    GlobalUnlock(global);
    return global;
}

std::vector<std::byte> dib_from_encoded_image(const std::vector<std::byte>& image) {
#if defined(PASTEIT_HAS_DESKTOP_DEPS)
    if (image.size() > static_cast<std::size_t>(INT_MAX)) {
        return {};
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(image.data()),
                               static_cast<int>(image.size()), &width, &height, &channels) ||
        width <= 0 || height <= 0 ||
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > 16U * 1024U * 1024U) {
        return {};
    }
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(image.data()),
                                             static_cast<int>(image.size()), &width, &height, &channels, 4);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        return {};
    }
    const auto row_stride = (static_cast<std::size_t>(width) * 3U + 3U) & ~std::size_t{3};
    const auto pixel_bytes = row_stride * static_cast<std::size_t>(height);
    if (row_stride > 64U * 1024U * 1024U || pixel_bytes > 64U * 1024U * 1024U) {
        stbi_image_free(pixels);
        return {};
    }

    BITMAPINFOHEADER header{};
    header.biSize = sizeof(header);
    header.biWidth = width;
    header.biHeight = height;
    header.biPlanes = 1;
    header.biBitCount = 24;
    header.biCompression = BI_RGB;
    header.biSizeImage = static_cast<DWORD>(pixel_bytes);
    std::vector<std::byte> dib(sizeof(header) + pixel_bytes);
    std::memcpy(dib.data(), &header, sizeof(header));
    for (int y = 0; y < height; ++y) {
        auto* row = reinterpret_cast<unsigned char*>(dib.data() + sizeof(header) +
                                                     static_cast<std::size_t>(height - 1 - y) * row_stride);
        for (int x = 0; x < width; ++x) {
            const auto* source = pixels + (static_cast<std::size_t>(y) * width + x) * 4U;
            const auto alpha = static_cast<unsigned int>(source[3]);
            for (int channel = 0; channel < 3; ++channel) {
                const int rgb_channel = 2 - channel;
                row[static_cast<std::size_t>(x) * 3U + channel] = static_cast<unsigned char>(
                    (static_cast<unsigned int>(source[rgb_channel]) * alpha + 255U * (255U - alpha)) / 255U);
            }
        }
    }
    stbi_image_free(pixels);
    return dib;
#else
    (void)image;
    return {};
#endif
}

bool set_clipboard_data(UINT format, HGLOBAL global) {
    if (global == nullptr) {
        return false;
    }
    if (SetClipboardData(format, global) == nullptr) {
        GlobalFree(global);
        return false;
    }
    return true;
}

std::string window_title(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return {};
    }
    std::wstring title(static_cast<std::size_t>(length) + 1U, L'\0');
    GetWindowTextW(window, title.data(), length + 1);
    title.resize(length);
    return wide_to_utf8(title);
}

std::string process_name(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return {};
    }
    std::vector<wchar_t> path(1024);
    DWORD size = static_cast<DWORD>(path.size());
    std::string result;
    if (QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
        result = path_to_utf8(std::filesystem::path{std::wstring{path.data(), size}}.filename());
    }
    CloseHandle(process);
    return result;
}

std::string focus_hash(HWND window, DWORD pid, std::string_view title) {
    std::ostringstream out;
    out << std::hex << reinterpret_cast<std::uintptr_t>(window) << ':' << pid << ':'
        << std::hash<std::string_view>{}(title);
    return out.str();
}

std::optional<std::filesystem::path> known_folder(const KNOWNFOLDERID& id) {
    PWSTR raw = nullptr;
    if (SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw) != S_OK || raw == nullptr) {
        return std::nullopt;
    }
    std::filesystem::path path{raw};
    CoTaskMemFree(raw);
    std::error_code error;
    if (!std::filesystem::is_directory(path, error)) {
        return std::nullopt;
    }
    return path;
}

std::optional<std::filesystem::path> recent_shortcut_target(const std::filesystem::path& shortcut) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link)))) {
        return std::nullopt;
    }
    IPersistFile* persist = nullptr;
    std::optional<std::filesystem::path> result;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&persist)))) {
        const auto filename = path_to_wide(shortcut);
        if (SUCCEEDED(persist->Load(filename.c_str(), STGM_READ))) {
            std::vector<wchar_t> target(32768, L'\0');
            if (SUCCEEDED(link->GetPath(target.data(), static_cast<int>(target.size()), nullptr, SLGP_RAWPATH)) &&
                target[0] != L'\0') {
                result = std::filesystem::path{target.data()};
            }
        }
        persist->Release();
    }
    link->Release();
    return result;
}

}  // namespace

WindowsDesktopServices::WindowsDesktopServices() {
    png_format_ = RegisterClipboardFormatW(L"PNG");
    jpeg_format_ = RegisterClipboardFormatW(L"JFIF");
    clipboard_sequence_ = GetClipboardSequenceNumber();
    if (ensure_message_window()) {
        AddClipboardFormatListener(message_window_);
    }
}

WindowsDesktopServices::~WindowsDesktopServices() {
    if (shortcut_registered_ && message_window_ != nullptr) {
        UnregisterHotKey(message_window_, kShortcutId);
    }
    if (message_window_ != nullptr) {
        RemoveClipboardFormatListener(message_window_);
        DestroyWindow(message_window_);
    }
}

void WindowsDesktopServices::attach_popup_window(std::uint64_t window_id) {
    popup_window_ = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window_id));
}

void WindowsDesktopServices::apply_settings(const AppSettings& settings) {
    WindowsFastActionServices::Options options;
    options.terminal_command = settings.terminal.command;
    fast_actions_.configure(std::move(options));
}

bool WindowsDesktopServices::ensure_message_window() {
    if (message_window_ != nullptr) {
        return true;
    }
    const wchar_t* class_name = L"PasteItMessageWindow";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = &WindowsDesktopServices::window_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = class_name;
    if (RegisterClassW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    message_window_ = CreateWindowExW(0, class_name, L"PasteIt", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                      GetModuleHandleW(nullptr), this);
    return message_window_ != nullptr;
}

void WindowsDesktopServices::mark_clipboard_dirty() {
    clipboard_dirty_ = true;
}

void WindowsDesktopServices::mark_shortcut_activated() {
    shortcut_activated_ = true;
}

LRESULT CALLBACK WindowsDesktopServices::window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<WindowsDesktopServices*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self != nullptr) {
        if (message == WM_CLIPBOARDUPDATE) {
            self->mark_clipboard_dirty();
            return 0;
        }
        if (message == WM_HOTKEY && static_cast<int>(wparam) == kShortcutId) {
            self->mark_shortcut_activated();
            return 0;
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

std::optional<ClipboardCapture> WindowsDesktopServices::poll_clipboard() {
    process_events();
    const DWORD sequence = GetClipboardSequenceNumber();
    if (!clipboard_dirty_ && sequence == clipboard_sequence_) {
        return std::nullopt;
    }
    if (!OpenClipboard(message_window_)) {
        clipboard_dirty_ = true;
        return std::nullopt;
    }
    clipboard_dirty_ = false;
    clipboard_sequence_ = sequence;
    std::optional<ClipboardCapture> captured;
    if (IsClipboardFormatAvailable(CF_HDROP)) {
        captured = capture_file_drop();
    }
    if (!captured && png_format_ != 0 && IsClipboardFormatAvailable(png_format_)) {
        captured = capture_clipboard_format(png_format_, "image/png");
    }
    if (!captured && jpeg_format_ != 0 && IsClipboardFormatAvailable(jpeg_format_)) {
        captured = capture_clipboard_format(jpeg_format_, "image/jpeg");
    }
    if (!captured && IsClipboardFormatAvailable(CF_DIBV5)) {
        captured = capture_dib(CF_DIBV5);
    }
    if (!captured && IsClipboardFormatAvailable(CF_DIB)) {
        captured = capture_dib(CF_DIB);
    }
    if (!captured && IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        captured = capture_unicode_text();
    }
    if (captured) {
        const HWND owner = GetClipboardOwner();
        DWORD pid = 0;
        if (owner != nullptr) {
            GetWindowThreadProcessId(owner, &pid);
        }
        if (pid != 0) {
            const auto app = process_name(pid);
            if (!app.empty()) {
                captured->source_app = app;
            }
        }
    }
    CloseClipboard();
    return captured;
}

void WindowsDesktopServices::process_events() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

bool WindowsDesktopServices::publish_text(std::string_view text) {
    const auto wide = utf8_to_wide(text);
    if (wide.empty() && !text.empty()) {
        return false;
    }
    const auto bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL global = global_copy(wide.c_str(), bytes);
    if (global == nullptr) {
        return false;
    }
    if (!OpenClipboard(message_window_)) {
        GlobalFree(global);
        return false;
    }
    if (!EmptyClipboard()) {
        GlobalFree(global);
        CloseClipboard();
        return false;
    }
    const bool ok = set_clipboard_data(CF_UNICODETEXT, global);
    CloseClipboard();
    clipboard_sequence_ = GetClipboardSequenceNumber();
    clipboard_dirty_ = false;
    return ok;
}

bool WindowsDesktopServices::publish_image(const std::vector<std::byte>& bytes, std::string_view mime_type) {
    if (bytes.empty()) {
        return false;
    }
    UINT format = 0;
    const void* image_data = bytes.data();
    std::size_t image_size = bytes.size();
    std::vector<std::byte> dib;
    if (mime_type == "image/png") {
        format = png_format_;
        dib = dib_from_encoded_image(bytes);
    } else if (mime_type == "image/jpeg" || mime_type == "image/jpg") {
        format = jpeg_format_;
        dib = dib_from_encoded_image(bytes);
    } else if (mime_type == "image/dib") {
        format = CF_DIB;
    } else if (mime_type == "image/bmp" && bytes.size() > sizeof(BITMAPFILEHEADER)) {
        BITMAPFILEHEADER header{};
        std::memcpy(&header, bytes.data(), sizeof(header));
        if (header.bfType == 0x4D42) {
            format = CF_DIB;
            image_data = bytes.data() + sizeof(header);
            image_size = bytes.size() - sizeof(header);
        }
    }
    if ((format == 0 && dib.empty()) || !OpenClipboard(message_window_)) {
        return false;
    }
    if (!EmptyClipboard()) {
        CloseClipboard();
        return false;
    }
    bool ok = false;
    if (format != 0) {
        ok = set_clipboard_data(format, global_copy(image_data, image_size));
    }
    if (!dib.empty()) {
        ok = set_clipboard_data(CF_DIB, global_copy(dib.data(), dib.size())) || ok;
    }
    CloseClipboard();
    clipboard_sequence_ = GetClipboardSequenceNumber();
    clipboard_dirty_ = false;
    return ok;
}

PlatformFocusContext WindowsDesktopServices::focused_context() {
    HWND window = GetForegroundWindow();
    DWORD pid = 0;
    if (window != nullptr) {
        GetWindowThreadProcessId(window, &pid);
    }
    auto title = window == nullptr ? std::string{} : window_title(window);
    auto app = pid == 0 ? std::string{} : process_name(pid);
    return PlatformFocusContext{.window_id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(window)),
                                .app_name = app,
                                .window_title = title,
                                .focused_target_hash = focus_hash(window, pid, title),
                                .pid = pid,
                                .current_directory = std::nullopt};
}

std::vector<PlatformRecentPath> WindowsDesktopServices::recent_paths() {
    std::vector<PlatformRecentPath> result;
    const auto now = current_time_ms();
    std::set<std::filesystem::path> seen;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (SUCCEEDED(init) || init == RPC_E_CHANGED_MODE) {
        if (const auto recent_dir = known_folder(FOLDERID_Recent)) {
            std::vector<std::filesystem::directory_entry> shortcuts;
            std::error_code error;
            for (std::filesystem::directory_iterator it(*recent_dir, error), end; !error && it != end;
                 it.increment(error)) {
                if (it->is_regular_file(error) && it->path().extension() == L".lnk") {
                    shortcuts.push_back(*it);
                }
            }
            std::sort(shortcuts.begin(), shortcuts.end(), [](const auto& left, const auto& right) {
                std::error_code left_error;
                std::error_code right_error;
                return left.last_write_time(left_error) > right.last_write_time(right_error);
            });
            for (const auto& shortcut : shortcuts) {
                if (result.size() >= 20) {
                    break;
                }
                const auto target = recent_shortcut_target(shortcut.path());
                if (!target || seen.find(*target) != seen.end()) {
                    continue;
                }
                std::error_code kind_error;
                const bool is_directory = std::filesystem::is_directory(*target, kind_error);
                const bool is_file = !is_directory && std::filesystem::is_regular_file(*target, kind_error);
                if (!is_directory && !is_file) {
                    continue;
                }
                seen.insert(*target);
                std::error_code time_error;
                const auto file_time = shortcut.last_write_time(time_error);
                const auto observed_ms = time_error ? now :
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        (std::chrono::system_clock::now() +
                         (file_time - std::filesystem::file_time_type::clock::now())).time_since_epoch()).count();
                result.push_back(PlatformRecentPath{.path = *target,
                                                    .kind = is_directory ? PathKind::Directory : PathKind::File,
                                                    .source = "windows-recent",
                                                    .observed_at_ms = observed_ms});
            }
        }
    }
    if (SUCCEEDED(init)) {
        CoUninitialize();
    }
    for (const auto& id : {FOLDERID_Downloads, FOLDERID_Documents, FOLDERID_Desktop, FOLDERID_Pictures}) {
        if (const auto folder = known_folder(id)) {
            if (seen.insert(*folder).second) {
                result.push_back(PlatformRecentPath{.path = *folder,
                                                    .kind = PathKind::Directory,
                                                    .source = "known-folder",
                                                    .observed_at_ms = now});
            }
        }
    }
    return result;
}

bool WindowsDesktopServices::register_global_shortcut() {
    if (!ensure_message_window()) {
        return false;
    }
    shortcut_registered_ = RegisterHotKey(message_window_, kShortcutId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'F') != 0;
    return shortcut_registered_;
}

bool WindowsDesktopServices::global_shortcut_activated() {
    process_events();
    const bool activated = shortcut_activated_;
    shortcut_activated_ = false;
    return activated;
}

bool WindowsDesktopServices::restore_focus_and_paste(const PlatformFocusContext& context) {
    HWND window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(context.window_id));
    if (window == nullptr || !IsWindow(window)) {
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == 0 || pid != context.pid) {
        return false;
    }
    if (IsIconic(window)) {
        ShowWindow(window, SW_RESTORE);
    }
    if (!SetForegroundWindow(window)) {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{35});
    if (GetForegroundWindow() != window) {
        return false;
    }

    std::array<INPUT, 4> inputs{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) == inputs.size();
}

bool WindowsDesktopServices::open_path(const std::filesystem::path& path) {
    const auto wide = path_to_wide(path);
    return reinterpret_cast<std::intptr_t>(ShellExecuteW(popup_window_, L"open", wide.c_str(), nullptr, nullptr,
                                                         SW_SHOWNORMAL)) > 32;
}

bool WindowsDesktopServices::open_uri(std::string_view uri) {
    const auto wide = utf8_to_wide(uri);
    return reinterpret_cast<std::intptr_t>(ShellExecuteW(popup_window_, L"open", wide.c_str(), nullptr, nullptr,
                                                         SW_SHOWNORMAL)) > 32;
}

bool WindowsDesktopServices::copy_text(std::string_view text) {
    return publish_text(text);
}

bool WindowsDesktopServices::move_popup_by(int delta_x, int delta_y) {
    if (popup_window_ == nullptr) {
        return false;
    }
    RECT rect{};
    if (!GetWindowRect(popup_window_, &rect)) {
        return false;
    }
    return SetWindowPos(popup_window_, nullptr, rect.left + delta_x, rect.top + delta_y, 0, 0,
                        SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER) != 0;
}

void WindowsDesktopServices::keep_above_popup(std::uint64_t window_id) {
    const auto child = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window_id));
    if (popup_window_ == nullptr || child == nullptr || child == popup_window_) return;
    // An owned window always stays above its owner.
    SetWindowLongPtrW(child, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(popup_window_));
    SetWindowPos(child, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool WindowsDesktopServices::set_popup_opacity(float opacity) {
    if (popup_window_ == nullptr || opacity < 0.0F || opacity > 1.0F) {
        return false;
    }
    const auto ex_style = GetWindowLongPtrW(popup_window_, GWL_EXSTYLE);
    SetWindowLongPtrW(popup_window_, GWL_EXSTYLE, ex_style | WS_EX_LAYERED);
    const auto alpha = static_cast<BYTE>(std::lround(std::clamp(opacity, 0.0F, 1.0F) * 255.0F));
    return SetLayeredWindowAttributes(popup_window_, 0, alpha, LWA_ALPHA) != 0;
}

std::vector<std::filesystem::path> WindowsDesktopServices::preferred_ui_fonts() {
    wchar_t windows_dir[MAX_PATH]{};
    if (GetWindowsDirectoryW(windows_dir, MAX_PATH) == 0) {
        return {};
    }
    const auto fonts = std::filesystem::path{windows_dir} / "Fonts";
    const std::vector<std::filesystem::path> candidates{
        fonts / "msyh.ttc",
        fonts / "simsun.ttc",
        fonts / "seguiemj.ttf",
        fonts / "arial.ttf",
    };
    std::vector<std::filesystem::path> found;
    std::copy_if(candidates.begin(), candidates.end(), std::back_inserter(found), [](const auto& path) {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error);
    });
    return found;
}

std::optional<std::filesystem::path> WindowsDesktopServices::choose_directory(
    const std::filesystem::path& initial_directory) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninitialize = SUCCEEDED(init);

    IFileOpenDialog* dialog = nullptr;
    if (CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)) != S_OK) {
        if (uninitialize) {
            CoUninitialize();
        }
        return std::nullopt;
    }
    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }

    std::error_code error;
    if (!initial_directory.empty() && std::filesystem::is_directory(initial_directory, error)) {
        IShellItem* folder = nullptr;
        const auto wide = path_to_wide(initial_directory);
        if (SHCreateItemFromParsingName(wide.c_str(), nullptr, IID_PPV_ARGS(&folder)) == S_OK) {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }

    std::optional<std::filesystem::path> selected;
    if (dialog->Show(popup_window_) == S_OK) {
        IShellItem* item = nullptr;
        if (dialog->GetResult(&item) == S_OK) {
            PWSTR raw = nullptr;
            if (item->GetDisplayName(SIGDN_FILESYSPATH, &raw) == S_OK && raw != nullptr) {
                selected = std::filesystem::path{raw};
                CoTaskMemFree(raw);
            }
            item->Release();
        }
    }
    dialog->Release();
    if (uninitialize) {
        CoUninitialize();
    }
    return selected;
}

FastActionServices& WindowsDesktopServices::fast_actions() {
    return fast_actions_;
}

}  // namespace pasteit
