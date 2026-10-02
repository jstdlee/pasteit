#include "ui/desktop_runtime.hpp"

#include "detect/content_detector.hpp"
#include "ai/mermaid_prompt.hpp"
#include "ai/model_catalog.hpp"
#include "ai/openai_compatible_client.hpp"
#include "ai/prompt_expander.hpp"
#include "app/ai_result_state.hpp"
#include "app/download_job.hpp"
#include "app/file_operation_confirmation.hpp"
#include "config/prompt_template_service.hpp"
#include "config/settings_store.hpp"
#include "decision/action_preference.hpp"
#include "djev/djev_client.hpp"
#include "executor/action_executor.hpp"
#include "executor/fast_action_executor.hpp"
#include "graph/graph_data.hpp"
#include "graph/graph_renderer.hpp"
#include "history/path_history_store.hpp"
#include "history/clipboard_history_store.hpp"
#include "history/path_target_resolver.hpp"
#include "platform/app_paths.hpp"
#if defined(_WIN32)
#include "platform/windows/windows_desktop_services.hpp"
#include "platform/windows/windows_single_instance.hpp"
#include "platform/windows/windows_strings.hpp"
#else
#include "platform/linux/linux_desktop_services.hpp"
#include "platform/linux/linux_single_instance.hpp"
#endif
#include "storage/clipboard_store.hpp"
#include "storage/path_history.hpp"
#include "util/path_utf8.hpp"
#include "history/choice_memory.hpp"
#include "history/usage_store.hpp"
#include "ui/desktop_flow.hpp"
#include "ui/decision_future_slot.hpp"
#include "ui/ai_result_panel.hpp"
#include "transform/text_transforms.hpp"
#include "ui/icons.hpp"
#include "util/utf8.hpp"
#include "ui/data_views.hpp"
#include "ui/diff_view.hpp"
#include "ui/command_palette.hpp"
#include "ui/ui_state.hpp"
#include "executor/utility_executor.hpp"
#include "ui/pipeline_view.hpp"
#include "ui/privacy_view.hpp"
#include "ui/prompt_parameters.hpp"
#include "net/provider_health.hpp"
#include "ai/prompt_optimizer.hpp"
#include "privacy/anonymizer.hpp"
#include "net/page_text.hpp"
#include "net/http_client.hpp"
#include "ui/theme.hpp"
#include "ui/image_preview_panel.hpp"
#include "ui/clipboard_history_model.hpp"
#include "ui/clipboard_history_panel.hpp"
#include "ui/contact_result_panel.hpp"
#include "ui/download_progress_panel.hpp"
#include "ui/file_operation_confirmation_panel.hpp"
#include "ui/hash_result_panel.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/image_annotation_panel.hpp"
#include "ui/imgui_widgets.hpp"
#include "ui/localization.hpp"
#include "ui/main_popup_panel.hpp"
#include "ui/mermaid_preview_panel.hpp"
#include "ui/network_report_panel.hpp"
#include "ui/prompt_templates_panel.hpp"
#include "ui/qr_preview_panel.hpp"
#include "ui/recent_paths_panel.hpp"
#include "ui/settings_model.hpp"
#include "ui/popup.hpp"
#include "ui/provider_test_request.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
#include "ui/multi_viewport.hpp"
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#else
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#ifdef None
#undef None
#endif
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>
#include <stb_image.h>

#if !defined(_WIN32)
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif
#endif


// Windows ships OpenGL 1.1 headers, which predate GL_CLAMP_TO_EDGE (1.2).
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace pasteit {
namespace {

#if defined(PASTEIT_HAS_DESKTOP_DEPS)

std::int64_t current_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string join_argv(const std::vector<std::string>& values) {
    std::ostringstream out;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) out << ' ';
        const bool needs_quotes = values[index].find_first_of(" \t\"'\\") != std::string::npos;
        if (!needs_quotes) {
            out << values[index];
            continue;
        }
        out << '"';
        for (const char ch : values[index]) {
            if (ch == '"' || ch == '\\') out << '\\';
            out << ch;
        }
        out << '"';
    }
    return out.str();
}

std::vector<std::string> split_argv_field(std::string_view value) {
    std::vector<std::string> out;
    std::string current;
    bool in_quotes = false;
    char quote_char = '\0';
    bool escaping = false;
    for (const char ch : value) {
        if (escaping) {
            current.push_back(ch);
            escaping = false;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            continue;
        }
        if (in_quotes) {
            if (ch == quote_char) {
                in_quotes = false;
            } else {
                current.push_back(ch);
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            in_quotes = true;
            quote_char = ch;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            if (!current.empty()) {
                out.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (escaping) current.push_back('\\');
    if (!current.empty()) out.push_back(std::move(current));
    return out;
}

void load_dotenv_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return;
    }
    std::string line;
    while (std::getline(input, line)) {
        line = trim(std::move(line));
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (line.rfind("export ", 0) == 0) {
            line = trim(line.substr(7));
        }
        const auto equal = line.find('=');
        if (equal == std::string::npos || equal == 0) {
            continue;
        }
        const auto key = trim(line.substr(0, equal));
        auto value = trim(line.substr(equal + 1));
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                  (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        if (!key.empty() && std::getenv(key.c_str()) == nullptr) {
#if defined(_WIN32)
            _putenv_s(key.c_str(), value.c_str());
#else
            setenv(key.c_str(), value.c_str(), 0);
#endif
        }
    }
}

void load_desktop_environment() {
    if (const char* explicit_path = std::getenv("PASTEIT_ENV_FILE")) {
        load_dotenv_file(explicit_path);
        return;
    }
    load_dotenv_file(".env");
    if (std::getenv("DJEV_API_KEY") == nullptr && std::getenv("API_KEY") == nullptr &&
        std::getenv("TYPESAFE_API_KEY") == nullptr) {
        load_dotenv_file("../.env");
    }
}

std::filesystem::path path_history_file() {
    return app_data_dir() / "paths.json";
}

std::filesystem::path usage_history_file() {
    return app_data_dir() / "usage.json";
}

std::filesystem::path choice_memory_file() {
    return app_data_dir() / "choices.json";
}

struct ImageTexture {
    unsigned int id = 0;
    int width = 0;
    int height = 0;

    void clear() {
        if (id != 0) {
            glDeleteTextures(1, &id);
        }
        id = 0;
        width = 0;
        height = 0;
    }

    ~ImageTexture() { clear(); }
};

struct GenerationJob {
    enum class Kind { TextPrompt, MermaidDiagram };
    Kind kind = Kind::TextPrompt;
    AiResultState state;
    AiResultPanelState panel;
    ActionInstance action;
    std::string source_text;
    std::optional<std::future<TextGenerationResult>> pending;
    // Filled by the worker thread while the answer streams in.
    struct Stream {
        std::mutex mutex;
        TextStreamUpdate latest;
        bool changed = false;
    };
    std::shared_ptr<Stream> stream;
    std::size_t reasoning_chars = 0;
    bool focus_pending = true;
    bool restore_placeholders = false;  // request text was anonymized
};

struct PromptParameterDialog {
    bool open = false;
    bool focus_pending = true;
    std::string action_id;
    std::string template_id;
    std::string template_name;
    std::vector<std::string> names;
    std::map<std::string, std::string> values;
};

struct CustomPromptDialog {
    bool open = false;
    bool focus_pending = true;
    ActionInstance action;
    std::string prompt;
    std::string template_id;  // empty = custom prompt
    bool thinking = false;    // per run; follows the selected template
};

struct AuxiliaryPanelWindow {
    bool open = false;
    bool focus_pending = false;

    void bring_to_front() {
        open = true;
        focus_pending = true;
    }
};

bool load_image_texture(const std::vector<std::byte>& bytes, ImageTexture& texture) {
    texture.clear();
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                            static_cast<int>(bytes.size()), &texture.width, &texture.height,
                                            &channels, 4);
    if (pixels == nullptr || texture.width <= 0 || texture.height <= 0) {
        if (pixels != nullptr) {
            stbi_image_free(pixels);
        }
        texture.width = 0;
        texture.height = 0;
        return false;
    }
    glGenTextures(1, &texture.id);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture.width, texture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    return true;
}

bool load_image_texture_file(const std::filesystem::path& path, ImageTexture& texture) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return false;
    const auto size = input.tellg();
    if (size <= 0 || size > 64 * 1024 * 1024) return false;
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    return input && load_image_texture(bytes, texture);
}

std::string bytes_as_text(const std::vector<std::byte>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool mime_is_image(const ClipboardItem& item) {
    return item.kind == ContentKind::Image ||
           std::any_of(item.mime_types.begin(), item.mime_types.end(), [](const std::string& mime) {
               return mime.rfind("image/", 0) == 0;
           });
}

std::string image_extension_for(const ClipboardItem& item) {
    for (const auto& mime : item.mime_types) {
        if (mime == "image/png") {
            return ".png";
        }
        if (mime == "image/jpeg" || mime == "image/jpg") {
            return ".jpg";
        }
        if (mime == "image/webp") {
            return ".webp";
        }
        if (mime == "image/dib") {
            return ".dib";
        }
    }
    return ".png";
}

bool is_direct_paste(ActionKind kind) {
    return kind == ActionKind::PasteText || kind == ActionKind::PasteImage || kind == ActionKind::PasteUrl ||
           kind == ActionKind::PasteEmail;
}

bool spawn_detached(const std::vector<std::string>& arguments) {
    if (arguments.empty()) {
        return false;
    }
#if defined(_WIN32)
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    auto command = windows_command_line(arguments);
    const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr,
                                        &startup, &process);
    if (!created) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawnp(&pid, argv.front(), nullptr, nullptr, argv.data(), environ) != 0) {
        return false;
    }
    std::thread([pid] {
        int status = 0;
        (void)waitpid(pid, &status, 0);
    }).detach();
    return true;
#endif
}

void position_popup(GLFWwindow* window) {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = monitor == nullptr ? nullptr : glfwGetVideoMode(monitor);
    if (monitor == nullptr || mode == nullptr) {
        return;
    }
    int monitor_x = 0;
    int monitor_y = 0;
    int width = 0;
    int height = 0;
    glfwGetMonitorPos(monitor, &monitor_x, &monitor_y);
    glfwGetWindowSize(window, &width, &height);
    glfwSetWindowPos(window, monitor_x + (mode->width - width) / 2,
                     monitor_y + (mode->height - height) / 2);
}

void glfw_error_callback(int error, const char* description) {
    std::cerr << "GLFW error " << error << ": " << (description == nullptr ? "unknown" : description) << '\n';
}

enum class DecisionState { Idle, Pending, Ranked, Fallback };
enum class SettingsPage { Home, General, Ranking, Llm, Prompts, FastActions, Pipelines, Privacy, Usage };

std::string content_kind_label(ContentKind kind) {
    switch (kind) {
        case ContentKind::Text: return "TEXT";
        case ContentKind::Url: return "URL";
        case ContentKind::Email: return "EMAIL";
        case ContentKind::Image: return "IMAGE";
        case ContentKind::Path: return "PATH";
        case ContentKind::Json: return "JSON";
        case ContentKind::DateTime: return "DATE";
        case ContentKind::Unknown: break;
    }
    return "DATA";
}

std::string human_size(std::uint64_t bytes) {
    std::ostringstream out;
    if (bytes < 1024) out << bytes << " B";
    else if (bytes < 1024 * 1024) out << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / 1024.0 << " KB";
    else out << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / (1024.0 * 1024.0) << " MB";
    return out.str();
}

// First non-empty lines of the clipboard text with runs of spaces collapsed.
std::string preview_excerpt(std::string_view text, std::size_t max_lines) {
    std::string out;
    std::size_t lines = 0;
    std::size_t start = 0;
    while (start < text.size() && lines < max_lines) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string line;
        bool space = false;
        for (const char ch : text.substr(start, end - start)) {
            if (ch == ' ' || ch == '\t' || ch == '\r') {
                space = !line.empty();
                continue;
            }
            if (space) line += ' ';
            space = false;
            line += ch;
        }
        if (!line.empty()) {
            if (!out.empty()) out += '\n';
            out += line.substr(0, 400);
            ++lines;
        }
        start = end + 1;
    }
    return out;
}

#endif

}  // namespace

int run_desktop_runtime() {
#if defined(PASTEIT_HAS_DESKTOP_DEPS)
    load_desktop_environment();
#if defined(_WIN32)
    WindowsSingleInstance single_instance;
#else
    LinuxSingleInstance single_instance;
#endif
    if (!single_instance.acquired()) {
        if (single_instance.request_show_existing_instance()) return 0;
        std::cerr << "PasteIt is already running.\n";
        return 2;
    }
    SettingsStore settings_store;
    auto settings_load = settings_store.load();
    AppSettings settings = std::move(settings_load.settings);
    if (!settings_load.djev_endpoint_saved || settings.djev.endpoint.empty()) settings.djev.endpoint = DjevClient::endpoint_from_env();
    if (!settings_load.djev_model_saved || settings.djev.model_id.empty()) settings.djev.model_id = DjevClient::model_from_env();
    if (!settings_load.djev_api_key_saved || settings.djev.api_key.empty()) {
        if (const char* key = std::getenv("DJEV_API_KEY")) settings.djev.api_key = key;
        else if (const char* key = std::getenv("API_KEY")) settings.djev.api_key = key;
    }
#if defined(_WIN32)
    std::unique_ptr<PlatformServices> platform = std::make_unique<WindowsDesktopServices>();
#else
    install_nonfatal_x11_error_handler(std::getenv("PASTEIT_DIAGNOSTICS") != nullptr);
    std::unique_ptr<PlatformServices> platform = std::make_unique<LinuxDesktopServices>();
#endif
    DownloadManager download_manager({}, {.keep_part_files_on_cancel = settings.downloads.keep_part_files});
    FastActionExecutor fast_action_executor(platform->fast_actions(), settings.renderers);
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    // No title bar or window buttons: the tab row moves the window and Esc
    // (or the global shortcut) hides it.
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    UiLayoutState ui_layout = load_ui_layout(app_data_dir() / "ui-state.json");
    GLFWwindow* window = glfwCreateWindow(ui_layout.popup_width, 520, "PasteIt", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        std::cerr << "Failed to create PasteIt popup window\n";
        return 1;
    }
    glfwMakeContextCurrent(window);
#if defined(_WIN32)
    const auto popup_window_id = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(glfwGetWin32Window(window)));
    if (auto* windows_platform = dynamic_cast<WindowsDesktopServices*>(platform.get())) {
        windows_platform->attach_popup_window(popup_window_id);
    }
#else
    const auto popup_window_id = static_cast<std::uint64_t>(glfwGetX11Window(window));
    if (auto* linux_platform = dynamic_cast<LinuxDesktopServices*>(platform.get())) {
        linux_platform->attach_popup_window(popup_window_id);
    }
#endif
    platform->apply_settings(settings);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // Dear ImGui 1.92 rasterizes glyphs on demand, so CJK text needs no
    // pre-baked glyph ranges and any font size can be pushed per widget.
    load_ui_fonts(platform->preferred_ui_fonts(), executable_directory() / "fa-solid-900.ttf");
    float dpi_scale = 1.0F;
    {
        float scale_x = 1.0F;
        float scale_y = 1.0F;
        glfwGetWindowContentScale(window, &scale_x, &scale_y);
        dpi_scale = std::clamp(scale_y, 1.0F, 3.0F);
    }
    UiTheme applied_theme = settings.theme;
    const auto apply_ui_theme = [&](UiTheme theme) {
        apply_theme(theme, dpi_scale);
        configure_independent_viewports(ImGui::GetIO(), ImGui::GetStyle());
        applied_theme = theme;
    };
    apply_ui_theme(settings.theme);
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true) || !ImGui_ImplOpenGL3_Init("#version 130")) {
        std::cerr << "Failed to initialize Dear ImGui backends\n";
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    install_imgui_clipboard_bridge(*platform);

    ClipboardStore clipboard_store;
    ClipboardHistoryStore clipboard_history_store(ClipboardStore::default_data_dir());
    const auto clipboard_history_load = clipboard_history_store.load();
    try {
        clipboard_store.restore(clipboard_history_load.items, clipboard_history_load.next_ref);
    } catch (const std::exception& error) {
        std::cerr << "PasteIt clipboard history restore failed: " << error.what() << '\n';
    }
    if (!clipboard_history_load.items.empty()) {
        const auto normalized_history = clipboard_history_store.save(
            clipboard_store.items_newest_first(50), clipboard_store.next_ref());
        if (normalized_history.success) {
            clipboard_store.restore(normalized_history.retained_items, normalized_history.next_ref);
        }
    }
    PathHistoryStore path_history_store(path_history_file());
    auto path_history_load = path_history_store.load();
    PathHistory path_history = std::move(path_history_load.history);
    UsageStore usage_store(usage_history_file());
    auto usage_load = usage_store.load();
    UsageModel usage_model = std::move(usage_load.model);
    if (!usage_load.warning.empty()) std::cerr << "PasteIt: " << usage_load.warning << '\n';
    // Startup never fails on a taken shortcut: Settings > General shows the
    // conflict and lets the user pick another combination.
    bool global_shortcut_ok = platform->register_global_shortcut(
        parse_hotkey(settings.global_hotkey).value_or(default_hotkey()));
    if (!global_shortcut_ok) {
        std::cerr << "Could not register global " << settings.global_hotkey
                  << "; another application holds it. Change it in Settings > General,"
                     " or set PASTEIT_SHOW_ON_START=1 to open manually.\n";
    }

    DjevClient djev_client(settings.djev.endpoint, settings.djev.model_id, DjevClient::kDefaultTimeout, {},
                           settings.djev.api_key);
    OpenAiCompatibleClient llm_client;
    DecisionFutureSlot pending_decision;
    std::vector<std::unique_ptr<GenerationJob>> generation_jobs;
    // Runs a text job with a streamed answer; the UI picks up partial text
    // from job.stream each frame.
    const auto launch_streaming_job = [&llm_client](GenerationJob& job, TextGenerationRequest request) {
        job.stream = std::make_shared<GenerationJob::Stream>();
        job.reasoning_chars = 0;
        job.pending.emplace(std::async(std::launch::async, [&llm_client, request = std::move(request), stream = job.stream] {
            return llm_client.generate(request, [&stream](const TextStreamUpdate& update) {
                std::lock_guard lock(stream->mutex);
                stream->latest = update;
                stream->changed = true;
            });
        }));
    };
    PromptParameterDialog prompt_parameter_dialog;
    CustomPromptDialog custom_prompt_dialog;
    ChartViewState chart_view;
    TableViewState table_view;
    MarkdownViewState markdown_view;
    // Text actions previewed in the input, and the side-by-side comparison.
    DiffViewState diff_view;
    CommandPaletteState command_palette;
    bool hide_popup_requested = false;
    int popup_resize_edge = 0;  // -1 left, 1 right while the popup's side is dragged
    std::string diff_action_id;
    std::string previewing_action_id;
    PipelineViewState pipeline_view;
    AnonymizeViewState anonymize_view;
    // Pending first-use consent for Summarize page.
    std::optional<ActionInstance> page_consent;
    bool page_consent_focus = false;
    // Placeholder <-> original for this session only (never written to disk).
    PlaceholderVault privacy_vault;
    const auto anonymize_options = [&] {
        AnonymizeOptions options;
        options.style = replacement_style_from_name(settings.privacy.replacement_style);
        for (const auto category : all_pii_categories()) {
            const auto name = pii_category_name(category);
            if (std::find(settings.privacy.disabled_categories.begin(), settings.privacy.disabled_categories.end(), name) !=
                settings.privacy.disabled_categories.end()) {
                options.disabled.insert(category);
            }
        }
        options.always_hide = settings.privacy.always_hide;
        options.never_hide = settings.privacy.never_hide;
        return options;
    };
    // Text sent to the general LLM; placeholders when the privacy option is on.
    const auto protect_for_llm = [&](const std::string& text) -> std::pair<std::string, bool> {
        if (!settings.privacy.anonymize_before_llm) return {text, false};
        auto options = anonymize_options();
        options.style = ReplacementStyle::Placeholder;
        auto result = anonymize_text(text, options, &privacy_vault);
        return {std::move(result.text), !result.findings.empty()};
    };
    const auto default_chart_path = [&] {
        const auto directory = settings.default_image_directory.empty()
            ? std::filesystem::current_path() : settings.default_image_directory;
        return path_to_utf8_string(directory / "chart.png");
    };
    const DataViewHost data_view_host{
        .copy_text = [&](std::string_view text) { platform->copy_text(text); },
        .copy_png = [&](const std::vector<std::byte>& png) { return platform->publish_image(png, "image/png"); },
        .open_uri = [&](std::string_view uri) { platform->open_uri(uri); },
        .save_file = [&](const std::string& path_text, const std::vector<std::byte>& bytes) -> std::string {
            const auto path = path_from_utf8_string(trim(path_text));
            if (path.empty()) return "Choose a file path first";
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            output.close();
            if (!output) return "Could not write " + path_to_utf8_string(path);
            (void)path_history.observe_path(path, PathKind::File, "chart", current_time_ms());
            path_history.record_use(path.parent_path(), current_time_ms());
            return {};
        },
    };
    // Last-used action options (prompt placeholders such as the target
    // language, the Ask LLM template, the chart type), kept across restarts.
    const ChoiceMemoryStore choice_memory_store(choice_memory_file());
    ChoiceMemory choice_memory = choice_memory_store.load();
    auto& prompt_parameter_memory = choice_memory.prompt_parameters;
    const auto save_choice_memory = [&] {
        std::string error;
        if (!choice_memory_store.save(choice_memory, error)) {
            std::cerr << "PasteIt remembered choices save failed: " << error << '\n';
        }
    };
    const auto remember_choice = [&](const std::string& key, std::string value) {
        auto& stored = choice_memory.choices[key];
        if (stored == value) return;
        stored = std::move(value);
        save_choice_memory();
    };
    custom_prompt_dialog.template_id = choice_memory.choice("ask_llm.template");
    if (const auto kind = choice_memory.choice("chart.kind"); !kind.empty()) {
        chart_view.kind = std::clamp(std::atoi(kind.c_str()), 0, static_cast<int>(ChartKind::Histogram));
    }
    std::optional<std::future<std::string>> pending_provider_test;
    // Jev / general LLM reachability, re-checked every minute (and right after
    // settings change) and shown as dots beside the Settings tab.
    ProviderHealth jev_health;
    ProviderHealth llm_health;
    std::optional<std::future<ProviderHealth>> pending_jev_health;
    std::optional<std::future<ProviderHealth>> pending_llm_health;
    auto next_health_check = std::chrono::steady_clock::now();
    bool health_recheck_requested = false;
    // Prompt editor "Optimize with LLM": the request, the template it belongs
    // to, and the draft it replaced (for Revert).
    std::optional<std::future<OptimizedPrompt>> pending_prompt_optimize;
    std::string prompt_optimize_template_id;
    std::string prompt_optimize_original;
    std::string prompt_optimize_status;
    bool pending_provider_test_is_djev = false;
    std::string djev_test_status;
    std::string llm_test_status;
    std::optional<std::future<ModelListResult>> pending_model_list;
    std::vector<std::string> general_llm_models;
    std::string model_list_endpoint;
    std::string model_list_status;
    ImagePreviewState image_preview;
    AuxiliaryPanelWindow contact_panel;
    AuxiliaryPanelWindow network_panel;
    AuxiliaryPanelWindow mermaid_panel;
    AuxiliaryPanelWindow qr_panel;
    RendererPreviewPanelState mermaid_preview_state;
    ImageTexture mermaid_result_texture;
    std::filesystem::path mermaid_result_texture_path;
    RendererPreviewPanelState qr_preview_state;
    ImageTexture qr_result_texture;
    std::filesystem::path qr_result_texture_path;
    AuxiliaryPanelWindow download_panel;
    AuxiliaryPanelWindow hash_panel;
    ImageAnnotationPanelState annotation_panel;
    annotation_panel.export_directory = settings.annotation.save_directory;
    annotation_panel.export_format = settings.annotation.export_format;
    annotation_panel.open_path = [&platform](const std::filesystem::path& path) { return platform->open_path(path); };
    annotation_panel.copy_text = [&platform](std::string_view text) { (void)platform->copy_text(text); };
    std::string active_download_panel_job_id;
    DesktopDecisionBatch active_batch;
    PopupModel popup_model;
    PlatformFocusContext target_context;
    ImageTexture preview_texture;
    ImageTexture history_texture;
    std::string history_texture_ref;
    ClipboardHistoryState clipboard_history_state;
    RecentPathsState recent_paths_state;
    std::optional<FileOperationConfirmationViewState> file_confirmation;
    std::optional<std::filesystem::path> manual_destination;
    PromptTemplatesPanelModel prompt_panel_model = build_prompt_templates_panel_model(settings.prompt_templates);
    std::string decision_status;
    std::string clipboard_preview_text;
    // Editable copy of the newest text item shown in the preview card; an
    // applied edit becomes a new clipboard item and the actions re-rank.
    std::string preview_edit_text;
    std::string preview_edit_source;
    bool preview_editable = false;
    bool preview_edit_apply_requested = false;
    DecisionState decision_state = DecisionState::Idle;
    SettingsPage settings_page = SettingsPage::Home;
    enum class MainTab { Smart, Paths, History, Settings };
    int main_tab = static_cast<int>(MainTab::Smart);
    HotkeyEditorState hotkey_editor_state;
    bool fit_popup_pending = false;
    bool popup_drag_armed = false;
    std::set<std::uint64_t> owned_sub_windows;
    std::string fitted_status;
    const auto fit_popup_to = [&](int desired_height) {
        fit_popup_pending = false;
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = monitor == nullptr ? nullptr : glfwGetVideoMode(monitor);
        const int maximum = mode == nullptr ? 900 : static_cast<int>(mode->height * 0.85);
        const int height = std::clamp(desired_height, 240, std::max(240, maximum));
        int width = 0;
        int current = 0;
        glfwGetWindowSize(window, &width, &current);
        if (std::abs(current - height) > 2) glfwSetWindowSize(window, width, height);
    };
    std::string execution_status;
    std::optional<ExecutionResult> last_execution;
    int selected_row = 0;
    std::uint64_t request_counter = 0;
    bool popup_visible = false;
    AppSettings settings_draft = settings;
    const auto draw_pipeline_settings = [&](UiLanguage language) {
        const auto& pal = palette();
        section_heading(icon::kPipeline, tr(language, UiTextKey::Pipeline), tr(language, UiTextKey::PipelinesHelp));
        if (begin_form("settings-pipelines", 250.0F)) {
            form_row(tr(language, UiTextKey::AllowedTools), tr(language, UiTextKey::AllowedToolsHelp));
            std::string tools;
            for (const auto& tool : settings_draft.pipelines.allowed_tools) tools += (tools.empty() ? "" : ", ") + tool;
            if (input_text_string("##allowed-tools", tools)) {
                settings_draft.pipelines.allowed_tools.clear();
                std::istringstream parts(tools);
                for (std::string part; std::getline(parts, part, ',');) {
                    part = trim(part);
                    // Bare program names only: no paths, no shell syntax.
                    if (!part.empty() && part.find_first_of("/\\ ;|&$`<>") == std::string::npos) {
                        settings_draft.pipelines.allowed_tools.push_back(part);
                    }
                }
            }
            form_row(tr(language, UiTextKey::AllowAnyProgram), tr(language, UiTextKey::AllowAnyProgramHelp));
            toggle_switch("##allow-any", &settings_draft.pipelines.allow_any_program);
            end_form();
        }
        separator_heading(tr(language, UiTextKey::CustomCommands));
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(pal.text_muted, "%s", tr(language, UiTextKey::CustomCommandsHelp).c_str());
        ImGui::PopTextWrapPos();
        std::optional<std::size_t> remove_command;
        begin_group("settings-custom-commands-group");
        if (ImGui::BeginTable("settings-custom-commands", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(tr(language, UiTextKey::Name).c_str(), ImGuiTableColumnFlags_WidthStretch, 0.8F);
            ImGui::TableSetupColumn(tr(language, UiTextKey::Command).c_str(), ImGuiTableColumnFlags_WidthStretch, 3.0F);
            ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            ImGui::TableHeadersRow();
            for (std::size_t index = 0; index < settings_draft.pipelines.custom_commands.size(); ++index) {
                auto& custom = settings_draft.pipelines.custom_commands[index];
                ImGui::PushID(static_cast<int>(index) + 10000);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                input_text_string("##name", custom.name);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                input_text_string("##command", custom.command);
                ImGui::TableNextColumn();
                if (icon_button("remove", icon::kTrash, tr(language, UiTextKey::Delete))) remove_command = index;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        end_group();
        if (remove_command) {
            settings_draft.pipelines.custom_commands.erase(settings_draft.pipelines.custom_commands.begin() +
                                                           static_cast<std::ptrdiff_t>(*remove_command));
        }
        if (ImGui::Button(with_icon(icon::kPlus, tr(language, UiTextKey::CustomCommands)).c_str())) {
            settings_draft.pipelines.custom_commands.push_back({"errors", "grep -i 'error|fail'"});
        }
        separator_heading(tr(language, UiTextKey::Recipes));
        std::optional<std::size_t> remove;
        begin_group("settings-recipes-group");
        if (ImGui::BeginTable("settings-recipes", 5, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(tr(language, UiTextKey::Enabled).c_str(), ImGuiTableColumnFlags_WidthFixed, 56.0F);
            ImGui::TableSetupColumn(tr(language, UiTextKey::Name).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.4F);
            ImGui::TableSetupColumn(tr(language, UiTextKey::AppliesTo).c_str(), ImGuiTableColumnFlags_WidthStretch, 0.8F);
            ImGui::TableSetupColumn(tr(language, UiTextKey::Command).c_str(), ImGuiTableColumnFlags_WidthStretch, 2.6F);
            ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            ImGui::TableHeadersRow();
            for (std::size_t index = 0; index < settings_draft.pipelines.recipes.size(); ++index) {
                auto& recipe = settings_draft.pipelines.recipes[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                toggle_switch("##enabled", &recipe.enabled);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                input_text_string("##name", recipe.name);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                input_text_string("##applies", recipe.applies_to);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                input_text_string("##command", recipe.command);
                ImGui::TableNextColumn();
                if (icon_button("remove", icon::kTrash, tr(language, UiTextKey::Delete))) remove = index;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        end_group();
        if (remove) settings_draft.pipelines.recipes.erase(settings_draft.pipelines.recipes.begin() + static_cast<std::ptrdiff_t>(*remove));
        if (ImGui::Button(with_icon(icon::kPlus, tr(language, UiTextKey::NewRecipe)).c_str())) {
            settings_draft.pipelines.recipes.push_back({.id = "custom-" + std::to_string(current_time_ms()), .name = "New recipe",
                                                        .command = "sort | uniq -c", .applies_to = "lines"});
        }
        ImGui::SameLine();
        if (ImGui::Button(with_icon(icon::kUndo, tr(language, UiTextKey::RestoreDefaults)).c_str())) {
            settings_draft.pipelines.recipes = default_pipeline_recipes();
        }
        (void)pal;
    };
    const auto lines_field = [&](const char* id, std::vector<std::string>& values) {
        std::string text;
        for (const auto& value : values) text += (text.empty() ? "" : "\n") + value;
        if (input_text_string(id, text, true, 0, ImGui::GetTextLineHeightWithSpacing() * 4.0F)) {
            values.clear();
            std::istringstream lines(text);
            for (std::string line; std::getline(lines, line);) {
                line = trim(line);
                if (!line.empty()) values.push_back(line);
            }
        }
    };
    const auto draw_privacy_settings = [&](UiLanguage language) {
        section_heading(icon::kShield, tr(language, UiTextKey::Privacy), tr(language, UiTextKey::PrivacyHelp));
        auto& privacy = settings_draft.privacy;
        if (begin_form("settings-privacy", 230.0F)) {
            form_row(tr(language, UiTextKey::ReplacementStyle));
            static constexpr const char* style_names[] = {"placeholder", "mask", "fake", "redact"};
            int style = static_cast<int>(replacement_style_from_name(privacy.replacement_style));
            const std::string labels[] = {tr(language, UiTextKey::StylePlaceholder), tr(language, UiTextKey::StyleMask),
                                          tr(language, UiTextKey::StyleFake), tr(language, UiTextKey::StyleRedact)};
            if (choice_control("##style", labels, style)) privacy.replacement_style = style_names[style];
            form_row(tr(language, UiTextKey::AnonymizeBeforeLlm), tr(language, UiTextKey::AnonymizeBeforeLlmHelp));
            toggle_switch("##before-llm", &privacy.anonymize_before_llm);
            form_row(tr(language, UiTextKey::AllowPageFetch));
            toggle_switch("##page-fetch", &privacy.allow_page_fetch);
            form_row(tr(language, UiTextKey::AlwaysHide));
            lines_field("##always-hide", privacy.always_hide);
            form_row(tr(language, UiTextKey::NeverHide));
            lines_field("##never-hide", privacy.never_hide);
            end_form();
        }
        separator_heading(tr(language, UiTextKey::Detect));
        begin_group("privacy-categories-group");
        if (ImGui::BeginTable("privacy-categories", 2, ImGuiTableFlags_SizingStretchSame)) {
            for (const auto category : all_pii_categories()) {
                ImGui::TableNextColumn();
                const auto name = pii_category_name(category);
                auto& disabled = privacy.disabled_categories;
                const auto found = std::find(disabled.begin(), disabled.end(), name);
                bool enabled = found == disabled.end();
                if (toggle_switch(pii_category_label(category).c_str(), &enabled)) {
                    if (enabled) disabled.erase(found);
                    else disabled.push_back(name);
                }
            }
            ImGui::EndTable();
        }
        end_group();
    };
    std::string settings_status = settings_load.warning;
    if (!clipboard_history_load.warning.empty()) {
        if (!settings_status.empty()) settings_status += "\n";
        settings_status += clipboard_history_load.warning;
    }
    const bool diagnostics = std::getenv("PASTEIT_DIAGNOSTICS") != nullptr;
    auto last_clipboard_poll = std::chrono::steady_clock::time_point{};
    if(!platform->set_popup_opacity(settings.window_opacity))glfwSetWindowOpacity(window, settings.window_opacity);

    const auto has_fast_action_result_panel = [&] {
        return contact_panel.open || network_panel.open || mermaid_panel.open || qr_panel.open ||
               download_panel.open || hash_panel.open || annotation_panel.open;
    };

    const auto has_any_auxiliary_window = [&] {
        return image_preview.open || file_confirmation.has_value() ||
               prompt_parameter_dialog.open || custom_prompt_dialog.open || chart_view.open || table_view.open ||
               markdown_view.open || pipeline_view.open || anonymize_view.open || page_consent.has_value() ||
               has_fast_action_result_panel() ||
               std::any_of(generation_jobs.begin(), generation_jobs.end(), [](const auto& job) {
                   return job->panel.open;
               });
    };

    // How many dialogs and result windows are open; a drop means one closed.
    const auto open_auxiliary_window_count = [&] {
        const bool flags[] = {image_preview.open, file_confirmation.has_value(), prompt_parameter_dialog.open,
                              custom_prompt_dialog.open, chart_view.open, table_view.open, markdown_view.open,
                              pipeline_view.open, anonymize_view.open, page_consent.has_value(), contact_panel.open,
                              network_panel.open, mermaid_panel.open, qr_panel.open, download_panel.open,
                              hash_panel.open, annotation_panel.open};
        return static_cast<std::size_t>(std::count(std::begin(flags), std::end(flags), true)) +
               static_cast<std::size_t>(std::count_if(generation_jobs.begin(), generation_jobs.end(),
                                                      [](const auto& job) { return job->panel.open; }));
    };
    std::size_t last_auxiliary_window_count = 0;
    bool refocus_popup = false;

    const auto persist_clipboard_history = [&] {
        const auto saved = clipboard_history_store.save(clipboard_store.items_newest_first(50), clipboard_store.next_ref());
        if (saved.success) {
            std::string cleanup_error;
            (void)clipboard_history_store.cleanup_orphan_blobs(saved, cleanup_error);
            clipboard_store.restore(saved.retained_items, saved.next_ref);
        } else if (diagnostics) {
            std::cerr << "PasteIt clipboard history save failed: " << saved.error << '\n';
        }
    };

    const auto capture_clipboard = [&](bool force) {
        platform->process_events();
        const auto now = std::chrono::steady_clock::now();
        if (!force && now - last_clipboard_poll < std::chrono::milliseconds{350}) {
            return;
        }
        last_clipboard_poll = now;
        auto captured = platform->poll_clipboard();
        if (!captured.has_value()) {
            return;
        }
        if (captured->captured_at_ms == 0) {
            captured->captured_at_ms = current_time_ms();
        }
        if (captured->source_app.empty() || captured->source_app == "x11") {
            captured->source_app = platform->focused_context().app_name;
        }
        if (captured->kind != ContentKind::Image) {
            captured->kind = detect_content(captured->mime_types, bytes_as_text(captured->bytes)).kind;
        }
        const auto item = clipboard_store.put(*captured);
        if (item.kind == ContentKind::Path) {
            const auto text = clipboard_store.read_text(item.ref);
            const auto paths = paths_from_uri_list(text);
            if (paths.empty()) {
                (void)path_history.observe(text, item.source_app, item.captured_at_ms);
            } else {
                for (const auto& path : paths) {
                    (void)path_history.observe(path_to_utf8_string(path), item.source_app, item.captured_at_ms);
                }
            }
        }
        persist_clipboard_history();
    };

    const auto request_model_list = [&] {
        if (pending_model_list.has_value() || settings_draft.general_llm.endpoint.empty()) return;
        const auto provider = settings_draft.general_llm;
        model_list_endpoint = provider.endpoint;
        pending_model_list.emplace(std::async(std::launch::async, [provider] {
            return OpenAiCompatibleModelClient{}.list_models({.endpoint=provider.endpoint, .api_key=provider.api_key});
        }));
    };

    const auto rewrite_history = [&](std::size_t keep, UiLanguage language) {
        path_history.retain_latest(keep);
        std::string path_error;
        const bool path_saved = path_history_store.save(path_history, path_error);
        clipboard_store.retain_latest(keep);
        const auto clipboard_saved = clipboard_history_store.save(
            clipboard_store.items_newest_first(50), clipboard_store.next_ref());
        std::string clipboard_error = clipboard_saved.error;
        bool clipboard_ok = clipboard_saved.success;
        if (clipboard_ok) {
            std::string cleanup_error;
            clipboard_ok = clipboard_history_store.cleanup_orphan_blobs(clipboard_saved, cleanup_error);
            if (!clipboard_ok) clipboard_error = cleanup_error;
            clipboard_store.restore(clipboard_saved.retained_items, clipboard_saved.next_ref);
        }
        recent_paths_state.detail_ref.clear();
        clipboard_history_state.detail_ref.clear();
        history_texture.clear();
        history_texture_ref.clear();
        if (!path_saved) settings_status = "Path history update failed: " + path_error;
        else if (!clipboard_ok) settings_status = "Clipboard history update failed: " + clipboard_error;
        else settings_status = keep == 0 ? tr(language, UiTextKey::HistoryDeleted)
                                         : tr(language, UiTextKey::HistoryPruned);
    };

    const auto make_batch = [&](const PlatformFocusContext& focus, std::string request_id) {
        DesktopDecisionInput input;
        input.request_id = std::move(request_id);
        input.captured_at_ms = current_time_ms();
        input.clipboard_items = clipboard_store.items_newest_first(1);
        input.recent_paths = path_history.recent(6);
        input.focused_target_hash = focus.focused_target_hash;
        input.focused_app = focus.app_name;
        input.focused_window_title = focus.window_title;
        input.direct_send_available = std::getenv("PASTEIT_SENDMAIL") != nullptr;
        input.prompt_templates = settings.prompt_templates;
        input.general_llm = settings.general_llm;
        input.default_image_directory = settings.default_image_directory;
        input.default_text_directory = settings.default_text_directory;
        input.downloads = settings.downloads;
        input.hash = settings.hash;
        input.date_time = settings.date_time;
        input.usage = &usage_model;
        input.pipeline_recipes = settings.pipelines.recipes;
        if (focus.current_directory.has_value()) {
            input.focused_current_directory = *focus.current_directory;
        }
        return build_desktop_decision(input);
    };

    const auto open_popup = [&](bool refresh_only = false) {
        if (!refresh_only) capture_clipboard(true);
        target_context = platform->focused_context();
        if (!settings.default_text_directory.empty()) {
            std::error_code error;
            if (std::filesystem::is_directory(settings.default_text_directory, error))
                (void)path_history.observe_path(settings.default_text_directory, PathKind::Directory, "configured-text", current_time_ms());
        }
        if (!settings.default_image_directory.empty()) {
            std::error_code error;
            if (std::filesystem::is_directory(settings.default_image_directory, error))
                (void)path_history.observe_path(settings.default_image_directory, PathKind::Directory, "configured-image", current_time_ms());
        }
        for (const auto& observed : platform->recent_paths()) {
            (void)path_history.observe_path(observed.path, observed.kind, observed.source,
                                            observed.observed_at_ms == 0 ? current_time_ms() : observed.observed_at_ms);
        }
        if (target_context.current_directory.has_value()) {
            (void)path_history.observe_path(*target_context.current_directory, PathKind::Directory,
                                            target_context.app_name, current_time_ms());
        }
        std::string path_save_error;
        if (!path_history_store.save(path_history, path_save_error) && diagnostics) {
            std::cerr << "PasteIt path history save failed: " << path_save_error << '\n';
        }
        const auto request_id = "req_" + std::to_string(current_time_ms()) + "_" + std::to_string(++request_counter);
        active_batch = make_batch(target_context, request_id);
        if (diagnostics) {
            std::cerr << "PasteIt snapshot request=" << request_id
                      << " clipboard_items=" << active_batch.request.snapshot.clipboard_items.size()
                      << " actions=" << active_batch.catalog.actions.size()
                      << " target_app=" << active_batch.request.snapshot.focused_app << '\n';
        }
        popup_model = {};
        selected_row = 0;
        clipboard_preview_text.clear();
        if (!active_batch.request.snapshot.clipboard_items.empty() &&
            active_batch.request.snapshot.clipboard_items.front().kind != ContentKind::Image) {
            // The stored preview is flattened to one line; show real rows.
            auto full_text = clipboard_store.read_text(active_batch.request.snapshot.clipboard_items.front().ref);
            clipboard_preview_text = utf8_prefix_bytes(full_text, 4096);
            constexpr std::size_t kMaxEditableBytes = 256 * 1024;
            preview_editable = full_text.size() <= kMaxEditableBytes;
            preview_edit_text = preview_editable ? std::move(full_text) : std::string{};
        } else {
            preview_editable = false;
            preview_edit_text.clear();
        }
        preview_edit_source = preview_edit_text;
        previewing_action_id.clear();
        prompt_parameter_dialog = {};
        execution_status.clear();
        last_execution.reset();
        preview_texture.clear();
        if (!active_batch.request.snapshot.clipboard_items.empty()) {
            const auto& newest = active_batch.request.snapshot.clipboard_items.front();
            if (mime_is_image(newest)) {
                (void)load_image_texture(clipboard_store.read(newest.ref), preview_texture);
                if (diagnostics) {
                    std::cerr << "PasteIt image thumbnail=" << preview_texture.width << 'x' << preview_texture.height
                              << " texture=" << (preview_texture.id != 0 ? "ready" : "failed") << '\n';
                }
            }
        }
        if (active_batch.catalog.actions.empty()) {
            decision_status.clear();
            decision_state = DecisionState::Idle;
            pending_decision.clear();
        } else {
            decision_status.clear();
            decision_state = DecisionState::Pending;
            const auto request = active_batch.request;
            pending_decision.replace(std::async(std::launch::async, [client = djev_client, request] {
                return client.decide(request);
            }));
        }
        fit_popup_pending = true;
        if (refresh_only) return;
        // Normal stacking: the popup is raised and focused when shown but does
        // not stay above other applications.
        glfwRestoreWindow(window);
        glfwShowWindow(window);
        glfwPollEvents();
        position_popup(window);
        glfwFocusWindow(window);
        popup_visible = true;
    };

    const auto apply_preview_edit = [&]() -> bool {
        if (!preview_editable || preview_edit_text == preview_edit_source) return false;
        ClipboardData edited;
        edited.mime_types = {"text/plain;charset=utf-8", "text/plain"};
        edited.bytes.resize(preview_edit_text.size());
        std::transform(preview_edit_text.begin(), preview_edit_text.end(), edited.bytes.begin(),
                       [](char ch) { return static_cast<std::byte>(ch); });
        edited.kind = detect_content(edited.mime_types, preview_edit_text).kind;
        edited.source_app = "PasteIt";
        edited.captured_at_ms = current_time_ms();
        (void)clipboard_store.put(edited);
        persist_clipboard_history();
        (void)platform->publish_text(preview_edit_text);
        open_popup(true);
        return true;
    };

    const auto publish_clipboard_result = [&](const ExecutionResult& result) {
        if (!result.output_clipboard_ref.has_value()) {
            return true;
        }
        const auto item = clipboard_store.item(*result.output_clipboard_ref);
        if (!item.has_value()) {
            return false;
        }
        if (mime_is_image(*item)) {
            const auto mime = item->mime_types.empty() ? std::string{"image/png"} : item->mime_types.front();
            return platform->publish_image(clipboard_store.read(item->ref), mime);
        }
        return platform->publish_text(clipboard_store.read_text(item->ref));
    };

    const auto write_temporary_annotation_image = [&](std::string_view source_ref) -> std::optional<std::filesystem::path> {
        const auto item = clipboard_store.item(std::string{source_ref});
        if (!item.has_value() || !mime_is_image(*item)) {
            return std::nullopt;
        }
        const auto output = std::filesystem::temp_directory_path() /
            ("pasteit-annotation-" + item->ref + image_extension_for(*item));
        std::ofstream stream(output, std::ios::binary | std::ios::trunc);
        const auto bytes = clipboard_store.read(item->ref);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        if (!stream) {
            return std::nullopt;
        }
        (void)path_history.observe_path(output, PathKind::File, "annotation", current_time_ms());
        return output;
    };

    const auto open_result_panel_for = [&](const ExecutionResult& result) {
        if (result.download_job_id.has_value()) {
            active_download_panel_job_id = *result.download_job_id;
            download_panel.bring_to_front();
        }
        if (fast_action_executor.contact_result().has_value() &&
            fast_action_executor.contact_result()->active().action_id == result.action_id) {
            contact_panel.bring_to_front();
        }
        if (fast_action_executor.network_report().has_value() &&
            fast_action_executor.network_report()->active().action_id == result.action_id) {
            network_panel.bring_to_front();
        }
        if (fast_action_executor.hash_result().has_value() &&
            fast_action_executor.hash_result()->active().action_id == result.action_id) {
            hash_panel.bring_to_front();
        }
        if (!fast_action_executor.renderer_result().has_value() ||
            fast_action_executor.renderer_result()->active().action_id != result.action_id) {
            return;
        }
        const auto& renderer = fast_action_executor.renderer_result()->active();
        switch (renderer.kind) {
            case RendererResultKind::Mermaid:
                mermaid_panel.bring_to_front();
                mermaid_preview_state.mode = RendererPreviewPanelState::Mode::Source;
                // Save where the user can find it (not the temp directory), as
                // the offline HTML page when it rendered, else the source.
                {
                    const auto directory = settings.default_text_directory.empty()
                        ? std::filesystem::temp_directory_path() : settings.default_text_directory;
                    const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    const bool html = renderer.output_path && renderer.output_path->extension() == ".html";
                    mermaid_preview_state.destination = path_to_utf8_string(
                        directory / ("mermaid-" + std::to_string(stamp) + (html ? ".html" : ".mmd")));
                    // No inline preview for the HTML page: show it in the default browser right away.
                    if (html && renderer.status == RendererResultStatus::Ready) {
                        mermaid_preview_state.mode = RendererPreviewPanelState::Mode::Preview;
                        mermaid_preview_state.open_preview(*renderer.output_path,
                            [platform = platform.get()](const std::filesystem::path& path) { return platform->open_path(path); });
                    }
                }
                break;
            case RendererResultKind::Qr:
                qr_panel.bring_to_front();
                qr_preview_state.mode = RendererPreviewPanelState::Mode::Source;
                qr_preview_state.destination = path_to_utf8_string(std::filesystem::temp_directory_path() /
                    ("pasteit-" + renderer.action_id + ".txt"));
                break;
            case RendererResultKind::Annotation: {
                annotation_panel.open = true;
                annotation_panel.focus_pending = true;
                annotation_panel.status_text.clear();
                annotation_panel.export_directory = settings.annotation.save_directory;
                annotation_panel.export_format = settings.annotation.export_format;
                const auto source_ref = renderer.payload.empty() ? renderer.source : renderer.payload;
                annotation_panel.output_path.clear();
                annotation_panel.last_export_path.clear();
                annotation_panel.text_editing = false;
                if (const auto original = write_temporary_annotation_image(source_ref)) {
                    annotation_panel.document = AnnotationDocument{*original};
                } else {
                    annotation_panel.document = AnnotationDocument{};
                    annotation_panel.status_text = "Annotation export unavailable: original image bytes could not be written";
                }
                break;
            }
            default:
                break;
        }
    };

    const auto make_execution_context = [&](std::string request_id) {
        ExecutionContext context{
            .clipboard_store = clipboard_store,
            .path_history = path_history,
            .request_id = std::move(request_id),
            .direct_send_configured = std::getenv("PASTEIT_SENDMAIL") != nullptr,
            .now_ms = current_time_ms(),
            .open_uri = {},
            .send_email = {},
            .fast_action_services = &platform->fast_actions(),
            .fast_action_executor = &fast_action_executor,
            .download_manager = &download_manager,
        };
        context.open_uri = [&platform](std::string_view uri) {
            return platform->open_uri(uri);
        };
        context.send_email = [](std::string_view email) {
            const char* adapter = std::getenv("PASTEIT_SENDMAIL");
            return adapter != nullptr && spawn_detached({adapter, std::string{email}});
        };
        return context;
    };

    const auto record_execution_result = [&](const ExecutionResult& result) {
        last_execution = result;
        execution_status = result.message;
        if (result.output_path.has_value()) execution_status += ": " + result.output_path->string();
        if (result.download_job_id.has_value()) execution_status += " [" + *result.download_job_id + "]";
        open_result_panel_for(result);
        if (result.status != ExecutionStatus::Completed) {
            return;
        }
        // The folder a result landed in is a used destination.
        if (result.output_path.has_value() && !result.output_path->parent_path().empty()) {
            path_history.record_use(result.output_path->parent_path(), current_time_ms());
        }
        std::string path_save_error;
        (void)path_history_store.save(path_history, path_save_error);
        if (!publish_clipboard_result(result)) {
            execution_status = "Action completed, but publishing the clipboard result failed";
        }
    };

    // Starts a general-LLM text job whose result opens in an AI result
    // window; restore maps placeholders in the answer back to real values.
    const auto start_text_job = [&](ActionInstance action, std::string system_message, std::string user_message,
                                    double temperature, bool restore, bool thinking) {
        if (active_batch.general_llm.endpoint.empty() || active_batch.general_llm.model_id.empty()) {
            execution_status = "General LLM provider is not configured";
            return;
        }
        TextGenerationRequest request{
            .request_id = active_batch.request.request_id + "_llm_" + std::to_string(++request_counter),
            .endpoint = active_batch.general_llm.endpoint,
            .api_key = active_batch.general_llm.api_key,
            .model_id = active_batch.general_llm.model_id,
            .system_message = std::move(system_message),
            .user_message = user_message,
            .temperature = temperature,
            .thinking = thinking,
        };
        auto job = std::make_unique<GenerationJob>();
        job->kind = GenerationJob::Kind::TextPrompt;
        job->action = std::move(action);
        job->source_text = user_message;
        job->restore_placeholders = restore;
        job->state.start(job->action.id, user_message, request);
        job->panel = {.open = true, .running = true, .request_id = request.request_id, .editable_text = {}, .error = {}};
        launch_streaming_job(*job, request);
        generation_jobs.push_back(std::move(job));
    };
    struct PageFetchResult {
        bool ok = false;
        std::string url;
        std::string title;
        std::string text;
        std::string error;
        bool truncated = false;
    };
    std::optional<std::future<PageFetchResult>> pending_page;
    const auto start_page_summary = [&](const ActionInstance& action) {
        const auto url = action.parameters.contains("url") ? action.parameters.at("url") : std::string{};
        execution_status = tr(UiTextKey::FetchingPage);
        pending_page.emplace(std::async(std::launch::async, [url] {
            PageFetchResult page;
            page.url = url;
            HttpRequest request{.url = url, .body = {},
                                .headers = {{"Accept", "text/html,text/plain;q=0.9,*/*;q=0.1"}, {"User-Agent", "PasteIt/1.0"}},
                                .timeout = std::chrono::milliseconds{5000}, .follow_redirects = true,
                                .max_body_bytes = 2 * 1024 * 1024};
            const auto response = make_default_http_transport()->get_json(request);
            if (!response.transport_error.empty()) {
                page.error = response.transport_error;
            } else if (response.status < 200 || response.status >= 300) {
                page.error = "The page answered HTTP " + std::to_string(response.status);
            } else if (response.body.find('\0') != std::string::npos) {
                page.error = "The page is not text";
            } else {
                const auto extracted = extract_page_text(response.body, response.content_type);
                page.ok = !extracted.text.empty();
                page.title = extracted.title;
                page.text = extracted.text;
                page.truncated = extracted.truncated || response.truncated;
                if (!page.ok) page.error = "No readable text on the page";
            }
            return page;
        }));
    };
    const auto execute_selected = [&](const std::string& action_id,
                                      std::optional<PromptVariables> supplied_prompt_variables = std::nullopt) {
        const auto action = active_batch.catalog.find(action_id);
        if (!action.has_value() || !action->enabled) {
            execution_status = "Action is unavailable";
            return;
        }
        // A root-list click is an explicit preference signal. File-confirmation
        // and prompt-parameter submissions re-enter this lambda for the same
        // choice, so they must not reward it a second time.
        const bool follow_up = action->parameters.contains("confirmation_complete") ||
                               supplied_prompt_variables.has_value();
        if (!follow_up) {
            // Usage keys are semantic (kind, destination path, template), so
            // regenerated action ids keep accumulating the same habit.
            record_batch_usage(usage_model, active_batch, *action);
            std::string usage_save_error;
            if (!usage_store.save(usage_model, usage_save_error) && diagnostics) {
                std::cerr << "PasteIt usage history save failed: " << usage_save_error << '\n';
            }
        }
        if (action->kind == ActionKind::Graph) {
            open_chart_from_series(chart_view, clipboard_store.read_text(action->source_ref), default_chart_path());
            return;
        }
        if (action->kind == ActionKind::ViewTable) {
            const auto text = clipboard_store.read_text(action->source_ref);
            const auto format = action->parameters.contains("format") ? action->parameters.at("format") : "delimited";
            std::optional<TableData> table;
            if (format == "json") {
                table = parse_json_table(text);
            } else {
                const auto delimiter = action->parameters.contains("delimiter") && !action->parameters.at("delimiter").empty()
                    ? action->parameters.at("delimiter").front() : '\0';
                const bool header = !action->parameters.contains("header") || action->parameters.at("header") == "1";
                table = parse_delimited_table(text, delimiter, header);
            }
            if (!table) {
                execution_status = "Could not read this clipboard as a table";
                return;
            }
            open_table_view(table_view, std::move(*table), tr(UiTextKey::ViewTable));
            return;
        }
        if (action->kind == ActionKind::SummarizePage) {
            if (!settings.privacy.allow_page_fetch) {
                page_consent = *action;
                page_consent_focus = true;
                return;
            }
            start_page_summary(*action);
            return;
        }
        if (action->kind == ActionKind::AnonymizeText) {
            open_anonymize_view(anonymize_view, clipboard_store.read_text(action->source_ref), anonymize_options());
            // Its Ask LLM picker lists the enabled templates; start on the last one used.
            int template_index = 0;
            for (const auto& prompt : settings.prompt_templates) {
                if (!prompt.enabled) continue;
                if (prompt.id == choice_memory.choice("anonymize.ask_template")) anonymize_view.prompt_choice = template_index;
                ++template_index;
            }
            return;
        }
        if (action->kind == ActionKind::RestorePlaceholders) {
            const auto text = clipboard_store.read_text(action->source_ref);
            const auto count = privacy_vault.restorable_count(text);
            if (count == 0) {
                execution_status = tr(UiTextKey::NoKnownPlaceholders);
                return;
            }
            platform->publish_text(privacy_vault.restore(text));
            execution_status = tr(UiTextKey::RestoredPlaceholders) + " " + std::to_string(count);
            return;
        }
        if (action->kind == ActionKind::RunPipeline) {
            const auto command = action->parameters.contains("command") ? action->parameters.at("command") : std::string{};
            open_pipeline_view(pipeline_view, clipboard_store.read_text(action->source_ref), command);
            return;
        }
        if (action->kind == ActionKind::PreviewMarkdown) {
            open_markdown_view(markdown_view, clipboard_store.read_text(action->source_ref));
            return;
        }
        if (action->kind == ActionKind::CustomPrompt) {
            custom_prompt_dialog.open = true;
            custom_prompt_dialog.focus_pending = true;
            custom_prompt_dialog.action = *action;
            custom_prompt_dialog.prompt.clear();
            custom_prompt_dialog.thinking = false;
            if (const auto selected = std::find_if(settings.prompt_templates.begin(), settings.prompt_templates.end(),
                    [&](const PromptTemplate& prompt) { return prompt.id == custom_prompt_dialog.template_id; });
                selected != settings.prompt_templates.end()) {
                custom_prompt_dialog.thinking = selected->thinking;
            }
            return;
        }
        if (needs_file_confirmation(action->kind) && !action->parameters.contains("confirmation_complete")) {
            const auto item = clipboard_store.item(action->source_ref);
            if (!item) {
                execution_status = "Clipboard source is no longer available";
                return;
            }
            const auto source_text = mime_is_image(*item) ? std::string{} : clipboard_store.read_text(item->ref);
            const auto configured_default =
                (action->kind == ActionKind::DownloadUrl || action->kind == ActionKind::SaveUrlFile)
                    ? settings.downloads.resume_directory
                    : (item->kind == ContentKind::Image
                           ? settings.default_image_directory
                           : settings.default_text_directory);
            const auto resolved = resolve_file_targets(
                *item, source_text, manual_destination, target_context.current_directory,
                configured_default.empty() ? std::nullopt
                                           : std::optional<std::filesystem::path>{configured_default},
                path_history.recent(100));
            std::vector<PathLocation> destinations;
            destinations.reserve(resolved.size());
            for (const auto& candidate : resolved) {
                std::string source = "recent";
                switch (candidate.role) {
                    case DestinationRole::Manual: source = "manual"; break;
                    case DestinationRole::SourceParent: source = "source-parent"; break;
                    case DestinationRole::FocusedDirectory: source = "focused"; break;
                    case DestinationRole::ConfiguredDefault: source = "configured"; break;
                    case DestinationRole::Recent: source = "recent"; break;
                    case DestinationRole::Temporary: source = "temporary"; break;
                }
                destinations.push_back(path_history.observe_path(
                    candidate.path, PathKind::Directory, source, current_time_ms()));
            }
            auto draft = make_file_operation_draft(*action, *item, std::move(destinations),
                                                   std::chrono::system_clock::now());
            const bool valid = validate_file_operation_draft(draft, clipboard_store);
            file_confirmation = FileOperationConfirmationViewState{
                .draft = std::move(draft),
                .source_preview = item->preview,
                .can_confirm = valid,
            };
            return;
        }
        if (mermaid_action_requires_generation(*action)) {
            if (active_batch.general_llm.endpoint.empty() || active_batch.general_llm.model_id.empty()) {
                execution_status = "General LLM provider is not configured";
                return;
            }
            const auto source_text = clipboard_store.read_text(action->source_ref);
            const auto request = build_mermaid_generation_request(
                active_batch.request.request_id + "_mermaid_" + std::to_string(++request_counter),
                active_batch.general_llm, source_text);
            auto job = std::make_unique<GenerationJob>();
            job->kind = GenerationJob::Kind::MermaidDiagram;
            job->action = *action;
            job->source_text = source_text;
            job->pending.emplace(std::async(std::launch::async, [&llm_client, request] {
                return llm_client.generate(request);
            }));
            generation_jobs.push_back(std::move(job));
            execution_status = "Mermaid source generation is running...";
            return;
        }
        if (action->kind == ActionKind::TransformText) {
            if (active_batch.general_llm.endpoint.empty() || active_batch.general_llm.model_id.empty()) {
                execution_status = "General LLM provider is not configured";
                return;
            }
            PromptTemplate prompt;
            prompt.id = action->parameters.at("template_id");
            prompt.name = action->parameters.at("template_name");
            prompt.system_prompt = action->parameters.at("system_prompt");
            prompt.temperature = std::stod(action->parameters.at("temperature"));
            if (const auto thinking = action->parameters.find("thinking"); thinking != action->parameters.end()) {
                prompt.thinking = thinking->second == "true";
            }
            const auto source_text = clipboard_store.read_text(action->source_ref);
            PromptVariables prompt_variables;
            const auto variables = prompt_variable_names(prompt.system_prompt);
            if (supplied_prompt_variables.has_value()) {
                prompt_variables = *supplied_prompt_variables;
            } else if (!variables.empty()) {
                prompt_parameter_dialog = {};
                prompt_parameter_dialog.open = true;
                prompt_parameter_dialog.action_id = action->id;
                prompt_parameter_dialog.template_id = prompt.id;
                prompt_parameter_dialog.template_name = prompt.name;
                prompt_parameter_dialog.names = variables;
                if (const auto remembered = prompt_parameter_memory.find(prompt.id); remembered != prompt_parameter_memory.end()) {
                    prompt_parameter_dialog.values = remembered->second;
                }
                fill_prompt_parameter_defaults(variables, prompt_parameter_dialog.values);
                return;
            }
            const auto [llm_text, protected_text] = protect_for_llm(source_text);
            const auto expanded = expand_prompt(prompt, llm_text, prompt_variables);
            TextGenerationRequest request{
                .request_id = active_batch.request.request_id + "_ai_" + std::to_string(++request_counter),
                .endpoint = action->parameters.at("llm_endpoint"),
                .api_key = active_batch.general_llm.api_key,
                .model_id = action->parameters.at("llm_model_id"),
                .system_message = expanded.system_message,
                .user_message = expanded.user_message,
                .temperature = prompt.temperature,
                .thinking = prompt.thinking,
            };
            auto job = std::make_unique<GenerationJob>();
            job->kind = GenerationJob::Kind::TextPrompt;
            job->action = *action;
            job->source_text = source_text;
            job->state.start(action->id, source_text, request);
            job->restore_placeholders = protected_text;
            job->panel = {
                .open = true,
                .running = true,
                .request_id = request.request_id,
                .editable_text = {},
                .error = {},
            };
            launch_streaming_job(*job, request);
            generation_jobs.push_back(std::move(job));
            execution_status = "Text transformation is running…";
            return;
        }
        auto context = make_execution_context(active_batch.request.request_id);
        const auto result = execute_action(*action, context);
        record_execution_result(result);
        if (result.status != ExecutionStatus::Completed) {
            return;
        }
        if (is_direct_paste(action->kind)) {
            popup_visible = false;
            if (!has_any_auxiliary_window()) {
                glfwSetWindowAttrib(window, GLFW_FLOATING, GLFW_FALSE);
                glfwHideWindow(window);
            }
            platform->process_events();
            if (!platform->restore_focus_and_paste(target_context)) {
                std::cerr << "Action completed, but PasteIt could not restore the target and send Ctrl+V.\n";
            }
        }
    };

    bool show_on_start = std::getenv("PASTEIT_SHOW_ON_START") != nullptr;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        platform->process_events();
        capture_clipboard(false);
        {
            const auto now = std::chrono::steady_clock::now();
            if ((now >= next_health_check || health_recheck_requested) && !pending_jev_health && !pending_llm_health) {
                health_recheck_requested = false;
                next_health_check = now + kProviderHealthInterval;
                pending_jev_health.emplace(std::async(std::launch::async, [provider = settings.djev] {
                    return check_jev_health(provider);
                }));
                pending_llm_health.emplace(std::async(std::launch::async, [provider = settings.general_llm] {
                    return check_llm_health(provider);
                }));
            }
            const auto take = [](std::optional<std::future<ProviderHealth>>& pending, ProviderHealth& target) {
                if (!pending || pending->wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) return;
                try { target = pending->get(); }
                catch (const std::exception& error) { target = {.state = ProviderHealthState::Down, .detail = error.what(), .checked_at_ms = current_time_ms()}; }
                pending.reset();
            };
            take(pending_jev_health, jev_health);
            take(pending_llm_health, llm_health);
        }
        const bool show_requested = single_instance.take_show_request();
        if (show_on_start || show_requested || platform->global_shortcut_activated()) {
            show_on_start = false;
            open_popup();
        }

        pending_decision.reap();
        if (auto ready = pending_decision.take_ready()) {
            const auto response = std::move(*ready);
            if (diagnostics) {
                std::cerr << "PasteIt Djev response request=" << response.request_id
                          << " valid=" << (response.valid ? "true" : "false")
                          << " choice=" << response.choice
                          << " probabilities=" << response.probabilities.size();
                if (!response.valid) {
                    std::cerr << " error=" << response.error;
                }
                std::cerr << '\n';
            }
            const auto active_focus = platform->focused_context();
            const auto& current_target = active_focus.window_id == popup_window_id ? target_context : active_focus;
            const auto current = make_batch(current_target, active_batch.request.request_id);
            const auto prepared = prepare_popup_decision(active_batch.request, response, active_batch.catalog,
                                                         current.request.snapshot,
                                                         active_batch.ranking_context);
            if (prepared.status == DecisionSessionStatus::Ready) {
                popup_model = build_popup_model(active_batch.request.snapshot, prepared.ranked);
                fit_popup_pending = popup_visible;
                const bool ranked = prepared.message == "ranked";
                decision_state = ranked ? DecisionState::Ranked : DecisionState::Fallback;
                decision_status = ranked ? std::string{} : prepared.message;
            } else {
                decision_state = DecisionState::Idle;
                decision_status = prepared.message.empty() ? "Djev did not return executable actions" : prepared.message;
            }
        }

        if (pending_page && pending_page->wait_for(std::chrono::milliseconds{0}) == std::future_status::ready) {
            const auto page = pending_page->get();
            pending_page.reset();
            if (!page.ok) {
                execution_status = "Summarize page failed: " + page.error;
            } else {
                const auto document = "Title: " + page.title + "\nURL: " + page.url + (page.truncated ? "\n(Text truncated)" : "") +
                                      "\n\n" + page.text;
                const auto [llm_text, protected_text] = protect_for_llm(document);
                ActionInstance action{};
                action.id = "summarize_page_" + std::to_string(++request_counter);
                action.kind = ActionKind::SummarizePage;
                start_text_job(action,
                               "Summarize this web page for a busy reader: one line with the gist, then up to 7 short "
                               "bullet points with the key facts. Keep names, numbers and dates. Answer in the page's language.",
                               llm_text, 0.2, protected_text, false);
                execution_status.clear();
            }
        }
        for (auto& job : generation_jobs) {
            // Show whatever has streamed in so far.
            if (job->pending.has_value() && job->stream) {
                std::lock_guard lock(job->stream->mutex);
                if (job->stream->changed) {
                    job->stream->changed = false;
                    job->reasoning_chars = job->stream->latest.reasoning_chars;
                    job->panel.editable_text = job->restore_placeholders ? privacy_vault.restore(job->stream->latest.text)
                                                                         : job->stream->latest.text;
                }
            }
            if (!job->pending.has_value() ||
                job->pending->wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) continue;
            const auto result = job->pending->get();
            job->pending.reset();
            if (job->kind == GenerationJob::Kind::MermaidDiagram) {
                MermaidNormalizationResult normalized;
                if (result.ok) {
                    normalized = normalize_mermaid_response(result.content);
                } else {
                    normalized.raw_source = result.content;
                    normalized.error = "General LLM request failed: " + result.error;
                }
                auto context = make_execution_context(active_batch.request.request_id.empty() ? std::string{"runtime"}
                                                                                              : active_batch.request.request_id);
                record_execution_result(fast_action_executor.render_generated_mermaid(
                    job->action, context, job->action.source_ref, job->source_text, normalized));
                continue;
            }
            job->panel.running = false;
            if (result.ok) {
                // Answers mention placeholders; show the real values again.
                const auto content = job->restore_placeholders ? privacy_vault.restore(result.content) : result.content;
                (void)job->state.complete(result.request_id, content);
                job->panel.editable_text = content;
                job->panel.error.clear();
            } else {
                (void)job->state.fail(result.request_id, result.error);
                job->panel.error = result.error;
            }
            job->panel.open = true;
        }
        std::erase_if(generation_jobs, [](const auto& job) {
            return job->kind == GenerationJob::Kind::MermaidDiagram && !job->pending.has_value();
        });
        {
            auto context = make_execution_context(active_batch.request.request_id.empty() ? std::string{"runtime"}
                                                                                          : active_batch.request.request_id);
            for (const auto& result : fast_action_executor.poll(context)) {
                record_execution_result(result);
            }
        }
        if (pending_provider_test.has_value() &&
            pending_provider_test->wait_for(std::chrono::milliseconds{0}) == std::future_status::ready) {
            auto& result_status = pending_provider_test_is_djev ? djev_test_status : llm_test_status;
            try { result_status = pending_provider_test->get(); }
            catch (const std::exception& error) { result_status = error.what(); }
            pending_provider_test.reset();
        }
        if (pending_prompt_optimize.has_value() &&
            pending_prompt_optimize->wait_for(std::chrono::milliseconds{0}) == std::future_status::ready) {
            OptimizedPrompt optimized;
            try { optimized = pending_prompt_optimize->get(); }
            catch (const std::exception& error) { optimized.error = error.what(); }
            pending_prompt_optimize.reset();
            auto& draft = prompt_panel_model.draft;
            if (!optimized.error.empty()) {
                prompt_optimize_status = optimized.error;
            } else if (prompt_panel_model.modal == PromptTemplateModal::Edit && draft && draft->id == prompt_optimize_template_id) {
                prompt_optimize_original = draft->system_prompt;
                draft->system_prompt = optimized.prompt;
                prompt_optimize_status = tr(UiTextKey::PromptOptimized);
                if (!optimized.restored_placeholders.empty()) {
                    prompt_optimize_status += "  (re-added:";
                    for (const auto& name : optimized.restored_placeholders) prompt_optimize_status += " {" + name + "}";
                    prompt_optimize_status += ")";
                }
            }
        }
        if (pending_model_list.has_value() &&
            pending_model_list->wait_for(std::chrono::milliseconds{0}) == std::future_status::ready) {
            const auto result = pending_model_list->get();
            pending_model_list.reset();
            if (result.ok) {
                general_llm_models = result.model_ids;
                model_list_status.clear();
            } else {
                general_llm_models.clear();
                model_list_status = result.error;
            }
        }

        const bool has_auxiliary_window = has_any_auxiliary_window();
        if (!popup_visible && !has_auxiliary_window) {
            std::this_thread::sleep_for(std::chrono::milliseconds{8});
            continue;
        }

        if (settings_draft.theme != applied_theme) apply_ui_theme(settings_draft.theme);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        const char* locale_value=std::getenv("LANG");
        const auto ui_language=resolve_language(settings.language,locale_value==nullptr?std::string_view{}:std::string_view{locale_value});
        // An applied preview edit rebuilds active_batch; do it before any UI
        // holds pointers into the batch.
        if (preview_edit_apply_requested) {
            preview_edit_apply_requested = false;
            if (apply_preview_edit()) execution_status = tr(ui_language, UiTextKey::PreviewEditApplied);
        }
        set_active_language(ui_language);
        std::optional<std::string> activated_action;
        std::optional<PromptVariables> activated_prompt_variables;
        const auto auxiliary_window_class = independent_window_class();
        if (popup_visible) {
        ImGui::SetNextWindowBgAlpha(settings_draft.window_opacity);
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(main_viewport->Pos);
        ImGui::SetNextWindowSize(main_viewport->Size);
        ImGui::SetNextWindowViewport(main_viewport->ID);
        const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoSavedSettings;
        if (refocus_popup) ImGui::SetNextWindowFocus();
        ImGui::Begin("PasteItPopup", nullptr, flags);
        // A pending confirmation or parameter dialog owns input until closed.
        const bool modal_open = file_confirmation.has_value() || prompt_parameter_dialog.open || custom_prompt_dialog.open ||
                                page_consent.has_value();
        if (modal_open) ImGui::BeginDisabled();
        const auto& theme_palette = palette();
        // The popup's left and right edges resize it (its height follows its
        // content). The width is remembered.
        {
            const float edge = 6.0F * ImGui::GetStyle().FontScaleDpi;
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const bool inside_y = mouse.y >= main_viewport->Pos.y && mouse.y <= main_viewport->Pos.y + main_viewport->Size.y;
            const float left = main_viewport->Pos.x;
            const float right = main_viewport->Pos.x + main_viewport->Size.x;
            const int near_edge = !inside_y ? 0 : mouse.x >= left && mouse.x < left + edge ? -1 : mouse.x <= right && mouse.x > right - edge ? 1 : 0;
            const bool over_popup = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            if (near_edge != 0 && over_popup && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) popup_resize_edge = near_edge;
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && popup_resize_edge != 0) {
                popup_resize_edge = 0;
                int width = 0;
                int height = 0;
                glfwGetWindowSize(window, &width, &height);
                ui_layout.popup_width = width;
                (void)save_ui_layout(app_data_dir() / "ui-state.json", ui_layout);
            }
            if ((near_edge != 0 && over_popup) || popup_resize_edge != 0) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (popup_resize_edge != 0 && ImGui::GetIO().MouseDelta.x != 0.0F) {
                int x = 0;
                int y = 0;
                int width = 0;
                int height = 0;
                glfwGetWindowPos(window, &x, &y);
                glfwGetWindowSize(window, &width, &height);
                const int delta = static_cast<int>(ImGui::GetIO().MouseDelta.x) * popup_resize_edge;
                const int resized = std::clamp(width + delta, 480, 1600);
                if (popup_resize_edge < 0) glfwSetWindowPos(window, x + (width - resized), y);
                glfwSetWindowSize(window, resized, height);
            }
        }
        // Dragging anywhere on the tab row moves the undecorated window. Mouse
        // positions are desktop coordinates with viewports on, so moving the
        // window does not feed back into the delta.
        {
            const float row_bottom = main_viewport->Pos.y + ImGui::GetStyle().WindowPadding.y + ImGui::GetFrameHeight() + 4.0F;
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const bool in_row = mouse.y >= main_viewport->Pos.y && mouse.y <= row_bottom &&
                                mouse.x >= main_viewport->Pos.x && mouse.x <= main_viewport->Pos.x + main_viewport->Size.x;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && in_row && popup_resize_edge == 0 &&
                ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
                popup_drag_armed = true;
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) popup_drag_armed = false;
            if (popup_drag_armed && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0F)) {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                if (delta.x != 0.0F || delta.y != 0.0F) {
                    int x = 0;
                    int y = 0;
                    glfwGetWindowPos(window, &x, &y);
                    glfwSetWindowPos(window, x + static_cast<int>(delta.x), y + static_cast<int>(delta.y));
                }
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            } else if (in_row && !ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            }
        }
        // Theme and language from the palette or a shortcut apply and save at
        // once, without touching other unsaved settings.
        const auto save_appearance = [&](std::optional<UiTheme> theme, std::optional<UiLanguage> language) {
            if (theme) settings.theme = settings_draft.theme = *theme;
            if (language) settings.language = settings_draft.language = *language;
            std::string error;
            if (!settings_store.save(settings, error)) settings_status = error;
        };
        static constexpr UiTheme kThemeCycle[] = {UiTheme::System, UiTheme::Light, UiTheme::Dark, UiTheme::TokyoNight};
        const auto next_theme = [&] {
            const auto at = std::find(std::begin(kThemeCycle), std::end(kThemeCycle), settings_draft.theme);
            return at == std::end(kThemeCycle) || at + 1 == std::end(kThemeCycle) ? kThemeCycle[0] : *(at + 1);
        };
        // App shortcuts (the global hotkey only opens the popup).
        if (!modal_open && !hotkey_editor_state.capturing) {
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_K) ||
                ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P)) {
                open_command_palette(command_palette);
            }
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) main_tab = static_cast<int>(MainTab::Settings);
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_T)) save_appearance(next_theme(), std::nullopt);
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_L)) {
                const auto ask = std::find_if(active_batch.catalog.actions.begin(), active_batch.catalog.actions.end(),
                    [](const ActionInstance& action) { return action.kind == ActionKind::CustomPrompt && action.enabled; });
                if (ask != active_batch.catalog.actions.end()) activated_action = ask->id;
            }
        }
        // Command registry: the palette lists it; ids name what run_command does.
        if (command_palette.open) {
            std::vector<PaletteCommand> commands;
            const auto add = [&](std::string id, UiTextKey label, const char* glyph, std::string keys, UiTextKey group,
                                 std::string detail = {}) {
                commands.push_back({.id = std::move(id), .label = tr(ui_language, label), .english = tr(UiLanguage::English, label),
                                    .group = tr(ui_language, group), .glyph = glyph, .keys = std::move(keys), .detail = std::move(detail)});
            };
            add("go.smart", UiTextKey::SmartActions, icon::kZap, "", UiTextKey::GroupGoTo);
            add("go.paths", UiTextKey::RecentPaths, icon::kFolder, "", UiTextKey::GroupGoTo);
            add("go.history", UiTextKey::ClipboardHistory, icon::kHistory, "", UiTextKey::GroupGoTo);
            add("go.settings", UiTextKey::Settings, icon::kSettings, "Ctrl+,", UiTextKey::GroupGoTo);
            struct SettingsPageEntry { SettingsPage page; const char* glyph; UiTextKey label; };
            static constexpr SettingsPageEntry pages[] = {
                {SettingsPage::Home, icon::kBook, UiTextKey::Home}, {SettingsPage::General, icon::kSliders, UiTextKey::General},
                {SettingsPage::Ranking, icon::kGauge, UiTextKey::Djev}, {SettingsPage::Llm, icon::kAi, UiTextKey::GeneralLlm},
                {SettingsPage::Prompts, icon::kFileText, UiTextKey::PromptTemplates}, {SettingsPage::FastActions, icon::kZap, UiTextKey::FastActions},
                {SettingsPage::Pipelines, icon::kPipeline, UiTextKey::Pipeline}, {SettingsPage::Privacy, icon::kShield, UiTextKey::Privacy},
                {SettingsPage::Usage, icon::kChart, UiTextKey::UsageInsights}};
            for (const auto& page : pages) {
                commands.push_back({.id = "settings." + std::to_string(static_cast<int>(page.page)),
                                    .label = tr(ui_language, UiTextKey::Settings) + " \xE2\x80\xBA " + tr(ui_language, page.label),
                                    .english = "Settings " + tr(UiLanguage::English, page.label),
                                    .group = tr(ui_language, UiTextKey::GroupGoTo), .glyph = page.glyph});
            }
            // Everything the current clipboard offers, not only the top cards.
            for (const auto& action : active_batch.catalog.actions) {
                if (!action.enabled) continue;
                commands.push_back({.id = "action." + action.id, .label = action.label, .english = action.label,
                                    .group = tr(ui_language, UiTextKey::GroupActions), .glyph = action_icon(action.kind),
                                    .detail = action.description});
            }
            add("theme.cycle", UiTextKey::SwitchTheme, icon::kPalette, "Ctrl+Shift+T", UiTextKey::GroupAppearance);
            const std::pair<UiTheme, std::string> themes[] = {
                {UiTheme::System, tr(ui_language, UiTextKey::ThemeSystem)}, {UiTheme::Light, tr(ui_language, UiTextKey::ThemeLight)},
                {UiTheme::Dark, tr(ui_language, UiTextKey::ThemeDark)}, {UiTheme::TokyoNight, "Tokyo Night"}};
            for (const auto& [theme, name] : themes) {
                commands.push_back({.id = "theme." + std::to_string(static_cast<int>(theme)),
                                    .label = tr(ui_language, UiTextKey::Theme) + ": " + name, .english = "Theme " + name,
                                    .group = tr(ui_language, UiTextKey::GroupAppearance), .glyph = icon::kPalette,
                                    .enabled = settings_draft.theme != theme});
            }
            const std::pair<UiLanguage, const char*> languages[] = {
                {UiLanguage::System, "System"}, {UiLanguage::English, "English"}, {UiLanguage::SimplifiedChinese, "简体中文"}};
            for (const auto& [language, name] : languages) {
                commands.push_back({.id = "language." + std::to_string(static_cast<int>(language)),
                                    .label = tr(ui_language, UiTextKey::Language) + ": " + name, .english = std::string{"Language "} + name,
                                    .group = tr(ui_language, UiTextKey::GroupAppearance), .glyph = icon::kLanguage,
                                    .enabled = settings_draft.language != language});
            }
            for (const auto& item : clipboard_store.items_newest_first(15)) {
                auto line = item.preview.substr(0, item.preview.find('\n'));
                if (line.size() > 90) line = utf8_prefix_bytes(line, 90) + "\xE2\x80\xA6";
                commands.push_back({.id = "history." + item.ref, .label = line, .english = line,
                                    .group = tr(ui_language, UiTextKey::GroupHistory), .glyph = icon::kHistory,
                                    .detail = human_size(item.size_bytes)});
            }
            add("app.check", UiTextKey::CheckProvidersNow, icon::kRefresh, "", UiTextKey::GroupApp);
            add("app.hide", UiTextKey::HidePopup, icon::kClose, "Esc", UiTextKey::GroupApp);
            if (const auto chosen = draw_command_palette(command_palette, commands, ui_language); !chosen.empty()) {
                const auto suffix = [&](std::string_view prefix) { return chosen.substr(prefix.size()); };
                if (chosen == "go.smart") main_tab = static_cast<int>(MainTab::Smart);
                else if (chosen == "go.paths") main_tab = static_cast<int>(MainTab::Paths);
                else if (chosen == "go.history") main_tab = static_cast<int>(MainTab::History);
                else if (chosen == "go.settings") main_tab = static_cast<int>(MainTab::Settings);
                else if (chosen.starts_with("settings.")) {
                    main_tab = static_cast<int>(MainTab::Settings);
                    settings_page = static_cast<SettingsPage>(std::stoi(suffix("settings.")));
                } else if (chosen.starts_with("action.")) {
                    main_tab = static_cast<int>(MainTab::Smart);
                    activated_action = suffix("action.");
                } else if (chosen == "theme.cycle") save_appearance(next_theme(), std::nullopt);
                else if (chosen.starts_with("theme.")) save_appearance(static_cast<UiTheme>(std::stoi(suffix("theme."))), std::nullopt);
                else if (chosen.starts_with("language.")) save_appearance(std::nullopt, static_cast<UiLanguage>(std::stoi(suffix("language."))));
                else if (chosen.starts_with("history.")) {
                    main_tab = static_cast<int>(MainTab::History);
                    (void)view_clipboard_history_item(clipboard_history_state, suffix("history."));
                } else if (chosen == "app.check") health_recheck_requested = true;
                else if (chosen == "app.hide") hide_popup_requested = true;
            }
        }
        const auto& theme_fonts = ui_fonts();
        const ImVec2 tab_row_pos = ImGui::GetCursorScreenPos();
        const float tab_row_right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        // Provider status in the window's top-right corner, on the tab row, drawn
        // before the tab bar so it stays the row's next item:
        // green reachable, yellow down, grey not configured / not checked yet.
        // Hover for details; click re-checks both.
        {
            const auto& pal = palette();
            struct Indicator { const char* id; const char* name; const ProviderHealth* health; bool checking; };
            const Indicator indicators[] = {
                {"health-jev", "Jev", &jev_health, pending_jev_health.has_value()},
                {"health-llm", "LLM", &llm_health, pending_llm_health.has_value()},
            };
            const float dot_radius = std::round(ImGui::GetFontSize() * 0.22F);
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            float total = 0.0F;
            for (const auto& indicator : indicators) {
                total += dot_radius * 2.0F + 5.0F + ImGui::CalcTextSize(indicator.name).x + gap * 1.5F;
            }
            // Utility cluster (top right, same place in every view): search, then settings.
            const float scale = ImGui::GetStyle().FontScaleDpi;
            const float icon_gap = 4.0F * scale;
            const float cluster_width = icon_buttons_width(2) - ImGui::GetStyle().ItemSpacing.x + icon_gap;
            const float cluster_x = tab_row_right - cluster_width;
            float x = cluster_x - 10.0F * scale - total + gap * 1.5F;
            const float row_height = ImGui::GetFrameHeight() + 4.0F * scale;
            ImGui::SetCursorScreenPos(ImVec2(cluster_x, tab_row_pos.y + (row_height - ImGui::GetFrameHeight()) * 0.5F));
            if (icon_button("cluster-search", icon::kSearch, tr(ui_language, UiTextKey::SearchCommand), command_palette.open, "Ctrl+P")) {
                open_command_palette(command_palette);
            }
            ImGui::SameLine(0.0F, icon_gap);
            const bool settings_open = main_tab == static_cast<int>(MainTab::Settings);
            if (icon_button("cluster-settings", icon::kSettings, tr(ui_language, UiTextKey::Settings), settings_open, "Ctrl+,")) {
                main_tab = settings_open ? static_cast<int>(MainTab::Smart) : static_cast<int>(MainTab::Settings);
            }
            for (const auto& indicator : indicators) {
                const auto& health = *indicator.health;
                const ImVec4 color = health.state == ProviderHealthState::Ok   ? pal.success
                                   : health.state == ProviderHealthState::Down ? pal.warning
                                                                               : pal.text_muted;
                const float width = dot_radius * 2.0F + 5.0F + ImGui::CalcTextSize(indicator.name).x;
                ImGui::SetCursorScreenPos(ImVec2(x, tab_row_pos.y));
                ImGui::PushID(indicator.id);
                const bool clicked = ImGui::InvisibleButton("status", ImVec2(width, row_height));
                const bool hovered = ImGui::IsItemHovered();
                ImGui::PopID();
                auto* draw = ImGui::GetWindowDrawList();
                const float center_y = tab_row_pos.y + row_height * 0.5F;
                draw->AddCircleFilled(ImVec2(x + dot_radius, center_y), dot_radius, ImGui::GetColorU32(color));
                draw->AddText(ImVec2(x + dot_radius * 2.0F + 5.0F, center_y - ImGui::GetFontSize() * 0.5F),
                              ImGui::GetColorU32(hovered ? pal.text : pal.text_muted), indicator.name);
                if (hovered) {
                    const std::string state = tr(ui_language, health.state == ProviderHealthState::Ok     ? UiTextKey::HealthOk
                                                             : health.state == ProviderHealthState::Down ? UiTextKey::HealthDown
                                                                                                         : UiTextKey::NotConfigured);
                    std::string text = std::string{indicator.name} + ": " + state;
                    if (!health.detail.empty() && health.state != ProviderHealthState::Unconfigured) text += "\n" + health.detail;
                    if (health.checked_at_ms > 0) {
                        const auto seconds = std::max<std::int64_t>(0, (current_time_ms() - health.checked_at_ms) / 1000);
                        text += "\n" + tr(ui_language, UiTextKey::HealthChecked) + ": " + std::to_string(seconds) + " s";
                    }
                    text += "\n" + (indicator.checking ? tr(ui_language, UiTextKey::Testing) : tr(ui_language, UiTextKey::HealthClickToCheck));
                    ImGui::SetTooltip("%s", text.c_str());
                }
                if (clicked) health_recheck_requested = true;
                x += width + gap * 1.5F;
            }
            // Back to the row start: the tab bar is the next item (no dangling cursor move).
            ImGui::SetCursorScreenPos(tab_row_pos);
        }
        {
            const std::string tab_labels[] = {
                with_icon(icon::kZap, tr(ui_language, UiTextKey::SmartActions)),
                with_icon(icon::kFolder, tr(ui_language, UiTextKey::RecentPaths)),
                with_icon(icon::kHistory, tr(ui_language, UiTextKey::ClipboardHistory)),
            };
            // Settings lives in the utility cluster; while it is open no segment is selected.
            (void)segmented_control("main-tabs", tab_labels, main_tab);
            ImGui::Spacing();
        }
        if (main_tab == static_cast<int>(MainTab::Smart)) {
        ImGui::BeginChild("smart-actions-body", ImVec2(0.0F, 0.0F), false);
        const float body_top = ImGui::GetCursorScreenPos().y;
        const ClipboardItem* current_item = active_batch.request.snapshot.clipboard_items.empty()
            ? nullptr : &active_batch.request.snapshot.clipboard_items.front();

        // Header: content kind + detected signals on the left, ranking state on the right.
        if (current_item != nullptr) {
            pill(content_kind_label(current_item->kind), theme_palette.accent);
            for (const auto& signal : active_batch.usage_context.signals) {
                ImGui::SameLine(0.0F, 6.0F);
                pill(signal, theme_palette.text_muted);
            }
            ImGui::SameLine();
        }
        {
            const bool pending = pending_decision.pending();
            const ImVec4 dot = pending ? theme_palette.warning
                             : decision_state == DecisionState::Ranked ? theme_palette.success
                             : decision_state == DecisionState::Fallback ? theme_palette.warning
                             : theme_palette.text_muted;
            auto state_text = tr(ui_language, pending ? UiTextKey::StatusRanking
                                              : decision_state == DecisionState::Ranked ? UiTextKey::StatusRanked
                                              : decision_state == DecisionState::Fallback ? UiTextKey::StatusFallback
                                              : UiTextKey::StatusIdle);
            if (current_item != nullptr) state_text = human_size(current_item->size_bytes) + "  \xC2\xB7  " + state_text;
            ImGui::PushFont(nullptr, theme_fonts.small);
            const float state_width = ImGui::CalcTextSize(state_text.c_str()).x + 14.0F;
            const float right_x = ImGui::GetWindowContentRegionMax().x - state_width;
            if (current_item == nullptr) ImGui::NewLine();
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), right_x));
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + 4.0F, at.y + ImGui::GetFontSize() * 0.55F), 3.5F,
                                                        ImGui::GetColorU32(dot));
            ImGui::SetCursorScreenPos(ImVec2(at.x + 12.0F, at.y));
            ImGui::TextColored(theme_palette.text_muted, "%s", state_text.c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !decision_status.empty()) ImGui::SetTooltip("%s", decision_status.c_str());
            ImGui::PopFont();
        }

        // Preview card.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme_palette.surface);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0F, 10.0F));
        const auto preview = current_item == nullptr ? std::string{}
            : preview_excerpt(clipboard_preview_text.empty() ? current_item->preview : clipboard_preview_text, 3);
        const float line_height = ImGui::GetTextLineHeight();
        const float preview_lines_height = preview_editable && current_item != nullptr && preview_texture.id == 0
            // The editable area keeps four rows so there is room to type; it scrolls beyond that.
            ? line_height * 4.0F + ImGui::GetStyle().ItemSpacing.y
            : std::max(ImGui::GetFrameHeight() * 2.0F + 4.0F,
                  std::min(line_height * 3.0F + ImGui::GetStyle().ItemSpacing.y,
                           ImGui::CalcTextSize(preview.c_str(), nullptr, false, ImGui::GetContentRegionAvail().x - 24.0F - 80.0F).y));
        const float preview_height = preview_texture.id != 0
            ? std::min(120.0F, static_cast<float>(preview_texture.height)) + 20.0F
            : preview_lines_height + 20.0F;
        ImGui::BeginChild("clipboard-preview", ImVec2(0.0F, preview_height),
                          ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
        if (preview_texture.id != 0) {
            const float scale = std::min(260.0F / static_cast<float>(preview_texture.width),
                                         std::min(120.0F, static_cast<float>(preview_texture.height)) /
                                             static_cast<float>(preview_texture.height));
            if (ImGui::ImageButton("clipboard-thumbnail", (ImTextureID)(intptr_t)preview_texture.id,
                                   ImVec2(preview_texture.width * scale, preview_texture.height * scale))) {
                image_preview.open = true;
                image_preview.focus_pending = true;
            }
            ImGui::SameLine();
            ImGui::TextColored(theme_palette.text_muted, "%d \xC3\x97 %d", preview_texture.width, preview_texture.height);
        } else if (current_item != nullptr) {
            float text_x = 0.0F;
            if (const auto swatch = std::find_if(active_batch.catalog.actions.begin(), active_batch.catalog.actions.end(),
                    [](const ActionInstance& action) { return action.kind == ActionKind::CopyColorRgb; });
                swatch != active_batch.catalog.actions.end()) {
                if (const auto color = parse_color(swatch->parameters.at("color"))) {
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    const float side = preview_lines_height;
                    ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + side, at.y + side),
                        IM_COL32(color->r, color->g, color->b, static_cast<int>(color->a * 255)), 6.0F);
                    ImGui::GetWindowDrawList()->AddRect(at, ImVec2(at.x + side, at.y + side),
                        ImGui::GetColorU32(theme_palette.border), 6.0F);
                    text_x = side + 12.0F;
                }
            }
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + text_x);
            // Leave room for the Copy / Ask LLM (and Apply / Revert) buttons at the right.
            const bool edited = preview_editable && preview_edit_text != preview_edit_source;
            const float button_room = (ImGui::GetFrameHeight() + 6.0F) * 2.0F + 8.0F;
            if (preview_editable) {
                // The preview is an editable textarea; Ctrl+Enter (or leaving
                // the field) applies the edit as a new clipboard item.
                ImGui::PushStyleColor(ImGuiCol_FrameBg, theme_palette.surface);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0F, 0.0F));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0F);
                // Multiline ImGui input: Enter adds a line, Ctrl+Enter validates.
                (void)input_text_string("##preview-edit", preview_edit_text, true, 0, preview_lines_height, -button_room);
                const bool left_after_edit = ImGui::IsItemDeactivatedAfterEdit();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !ImGui::IsItemActive()) ImGui::SetTooltip("%s", tr(ui_language, UiTextKey::EditHint).c_str());
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
                if (left_after_edit) preview_edit_apply_requested = true;
            } else {
                ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x - button_room);
                ImGui::BeginGroup();
                const ImVec2 clip_min = ImGui::GetCursorScreenPos();
                ImGui::PushClipRect(clip_min, ImVec2(clip_min.x + ImGui::GetContentRegionAvail().x - button_room, clip_min.y + preview_lines_height), true);
                ImGui::TextUnformatted(preview.c_str());
                ImGui::PopClipRect();
                ImGui::EndGroup();
                ImGui::PopTextWrapPos();
            }
            // Copy and Ask LLM stacked in the top-right corner; Copy always
            // takes what the input shows now, a preview included. Apply /
            // Revert stand beside them while the text differs from the clipboard.
            const auto custom_action = std::find_if(active_batch.catalog.actions.begin(), active_batch.catalog.actions.end(),
                [](const ActionInstance& action) { return action.kind == ActionKind::CustomPrompt && action.enabled; });
            const float side = ImGui::GetFrameHeight();
            const float corner_top = ImGui::GetStyle().WindowPadding.y;
            const float column_x = ImGui::GetWindowContentRegionMax().x - side;
            ImGui::SetCursorPos(ImVec2(column_x, corner_top));
            if (icon_button("preview-copy", icon::kCopy, tr(ui_language, UiTextKey::CopyClipboardText))) {
                platform->copy_text(preview_editable ? preview_edit_text : clipboard_store.read_text(current_item->ref));
                execution_status = tr(ui_language, UiTextKey::Copied);
            }
            if (custom_action != active_batch.catalog.actions.end()) {
                ImGui::SetCursorPos(ImVec2(column_x, corner_top + side + 4.0F));
                // The feature highlight: accent-tinted, its own glyph, a shortcut.
                if (icon_button("preview-ask", icon::kAi, tr(ui_language, UiTextKey::AskLlmTitle), true, "Ctrl+L")) {
                    activated_action = custom_action->id;
                }
            }
            if (edited) {
                ImGui::SetCursorPos(ImVec2(column_x - side - 6.0F, corner_top));
                if (icon_button("preview-apply", icon::kCheck, tr(ui_language, UiTextKey::ApplyEdit))) preview_edit_apply_requested = true;
                ImGui::SetCursorPos(ImVec2(column_x - side - 6.0F, corner_top + side + 4.0F));
                if (icon_button("preview-revert", icon::kUndo, tr(ui_language, UiTextKey::RevertEdit))) {
                    preview_edit_text = preview_edit_source;
                    previewing_action_id.clear();
                }
            }
        } else {
            ImGui::TextColored(theme_palette.text_muted, "%s", tr(ui_language,UiTextKey::NoClipboard).c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        // Quick tools that open a dialog rather than run immediately.
        bool any_tool = false;
        const auto tool_button = [&](ActionKind kind, UiTextKey key, const char* glyph) {
            const auto choice = std::find_if(active_batch.catalog.actions.begin(), active_batch.catalog.actions.end(),
                [kind](const ActionInstance& action) { return action.kind == kind && action.enabled; });
            if (choice == active_batch.catalog.actions.end()) return;
            const bool listed = std::any_of(popup_model.rows.begin(), popup_model.rows.end(),
                                            [&](const PopupRow& row) { return row.action_id == choice->id; });
            if (listed) return;
            if (any_tool) ImGui::SameLine(0.0F, 6.0F);
            any_tool = true;
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0F, ImGui::GetStyle().FramePadding.y));
            if (ImGui::Button(with_icon(glyph, tr(ui_language, key)).c_str())) activated_action = choice->id;
            ImGui::PopStyleVar(2);
        };
        tool_button(ActionKind::ViewTable, UiTextKey::ViewTable, icon::kTable);
        tool_button(ActionKind::PreviewMarkdown, UiTextKey::PreviewMarkdown, icon::kBook);
        tool_button(ActionKind::Graph, UiTextKey::GraphData, icon::kChart);
        tool_button(ActionKind::RunPipeline, UiTextKey::Pipeline, icon::kPipeline);
        tool_button(ActionKind::AnonymizeText, UiTextKey::Anonymize, icon::kEyeOff);
        tool_button(ActionKind::AnnotateImage, UiTextKey::AnnotateImage, icon::kEdit);
        tool_button(ActionKind::Base64Encode, UiTextKey::Base64EncodeTool, icon::kCode);
        tool_button(ActionKind::Base64Decode, UiTextKey::Base64DecodeTool, icon::kCode);

        if (popup_model.rows.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(theme_palette.text_muted, "%s",
                               (pending_decision.pending() ? tr(ui_language,UiTextKey::WaitingDjev)
                                                           : tr(ui_language,UiTextKey::NoRankedActions)).c_str());
        } else {
            const bool typing = ImGui::GetIO().WantTextInput;
            if (!typing && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                selected_row = std::max(0, selected_row - 1);
            }
            if (!typing && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                selected_row = std::min(static_cast<int>(popup_model.rows.size()) - 1, selected_row + 1);
            }
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0F, 4.0F));
            const auto preview_tip = tr(ui_language, UiTextKey::PreviewResult);
            const auto restore_tip = tr(ui_language, UiTextKey::RestoreOriginal);
            const auto compare_tip = tr(ui_language, UiTextKey::CompareResult);
            // The text an action would produce from the clipboard, without running it.
            const auto preview_of = [&](const std::string& action_id) -> std::optional<std::string> {
                const auto found = std::find_if(active_batch.catalog.actions.begin(), active_batch.catalog.actions.end(),
                                                [&](const ActionInstance& action) { return action.id == action_id; });
                if (found == active_batch.catalog.actions.end()) return std::nullopt;
                return preview_text_action(*found, preview_edit_source);
            };
            for (std::size_t index = 0; index < popup_model.rows.size(); ++index) {
                const auto& row = popup_model.rows[index];
                const int shortcut = index < 9 ? static_cast<int>(index) + 1 : 0;
                const bool previewable = preview_editable && row.enabled && can_preview_text_action(row.kind);
                const bool previewing = previewable && previewing_action_id == row.action_id && preview_edit_text != preview_edit_source;
                const ActionCardModel card{
                    .glyph = action_icon(row.kind),
                    .label = row.label,
                    .detail = row.detail,
                    .category = row.category,
                    // Fallback scores are only an ordering, not a confidence.
                    .probability = decision_state == DecisionState::Ranked ? row.probability : 0.0,
                    .shortcut = shortcut,
                    .selected = static_cast<int>(index) == selected_row,
                    .enabled = row.enabled,
                    .previewable = previewable,
                    .previewing = previewing,
                    .preview_tooltip = preview_tip,
                    .restore_tooltip = restore_tip,
                    .compare_tooltip = compare_tip,
                };
                const bool shortcut_pressed = shortcut > 0 && !typing &&
                    ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + shortcut - 1), false);
                const auto event = action_card(row.action_id.c_str(), card);
                if ((event == ActionCardEvent::Activate || shortcut_pressed) && row.enabled) {
                    selected_row = static_cast<int>(index);
                    activated_action = row.action_id;
                }
                if (event == ActionCardEvent::Preview) {
                    selected_row = static_cast<int>(index);
                    if (previewing) {
                        preview_edit_text = preview_edit_source;
                        previewing_action_id.clear();
                    } else if (auto result = preview_of(row.action_id)) {
                        preview_edit_text = std::move(*result);
                        previewing_action_id = row.action_id;
                    } else {
                        execution_status = tr(ui_language, UiTextKey::NoPreview);
                    }
                }
                if (event == ActionCardEvent::Compare) {
                    selected_row = static_cast<int>(index);
                    if (auto result = preview_of(row.action_id)) {
                        open_diff_view(diff_view, row.label, preview_edit_source, std::move(*result));
                        diff_action_id = row.action_id;
                    } else {
                        execution_status = tr(ui_language, UiTextKey::NoPreview);
                    }
                }
            }
            ImGui::PopStyleVar();
            if (!typing && ImGui::IsKeyPressed(ImGuiKey_Enter) && selected_row >= 0 &&
                selected_row < static_cast<int>(popup_model.rows.size()) && popup_model.rows[selected_row].enabled) {
                activated_action = popup_model.rows[selected_row].action_id;
            }
        }
        if (!execution_status.empty()) {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0F);
            ImGui::TextColored(last_execution.has_value() && last_execution->status == ExecutionStatus::Failed
                                   ? theme_palette.danger : theme_palette.success, "%s", execution_status.c_str());
            ImGui::PopTextWrapPos();
            if (last_execution.has_value() && !last_execution->output_paths.empty()) {
                const auto& output = last_execution->output_paths.front();
                if (ImGui::Button(with_icon(icon::kCopy, tr(ui_language,UiTextKey::CopyPath)).c_str())) platform->copy_text(path_to_utf8_string(output));
                ImGui::SameLine();
                if (ImGui::Button(with_icon(icon::kOpen, tr(ui_language,UiTextKey::OpenResult)).c_str())) platform->open_path(output);
                ImGui::SameLine();
                if (ImGui::Button(with_icon(icon::kFolderOpen, tr(ui_language,UiTextKey::OpenFolder)).c_str())) platform->open_path(output.parent_path());
            }
        }
        // A new status line must stay visible below the cards.
        if (execution_status != fitted_status) {
            fitted_status = execution_status;
            fit_popup_pending = true;
        }
        // Fit the window to its content once per ranking instead of guessing
        // row heights; the user can still resize, and long lists scroll.
        if (fit_popup_pending) {
            const float content_height = ImGui::GetCursorScreenPos().y - body_top;
            const float chrome = body_top - main_viewport->Pos.y + ImGui::GetStyle().WindowPadding.y * 2.0F;
            if (diagnostics) {
                std::cerr << "PasteIt fit content=" << content_height << " chrome=" << chrome
                          << " rows=" << popup_model.rows.size() << '\n';
            }
            fit_popup_to(static_cast<int>(std::ceil(content_height + chrome)));
        }
        ImGui::EndChild();
        }
        if (main_tab == static_cast<int>(MainTab::Paths)) {
            const auto recent_model = build_recent_paths_model(path_history.recent(100), recent_paths_state,
                                                               active_batch.catalog);
            const auto command = render_recent_paths_panel(recent_paths_state, recent_model, ui_language);
            if (command.kind != RecentPathCommandKind::None) {
                const auto location = path_history.find(command.path_ref);
                if (command.kind == RecentPathCommandKind::CopyHere ||
                    command.kind == RecentPathCommandKind::MoveHere) {
                    activated_action = command.action_id;
                } else if (location) {
                    if (command.kind == RecentPathCommandKind::CopyPath) platform->copy_text(path_to_utf8_string(location->path));
                    if (command.kind == RecentPathCommandKind::Open) platform->open_path(location->path);
                    if (command.kind == RecentPathCommandKind::OpenParent) platform->open_path(location->path.parent_path());
                    if (command.kind == RecentPathCommandKind::UseAsDestination) manual_destination = location->path;
                }
            }
        }
        if (main_tab == static_cast<int>(MainTab::History)) {
            auto history_model = build_clipboard_history_model(
                clipboard_store.items_newest_first(50), clipboard_history_state);
            if (history_model.detail && !history_model.detail->is_image) {
                history_model.detail->preview = clipboard_store.read_text(history_model.detail->ref);
            }
            const auto texture_lookup = [&](std::string_view ref) -> std::optional<ClipboardTexture> {
                const auto item = clipboard_store.item(std::string{ref});
                if (!item || !mime_is_image(*item)) return std::nullopt;
                if (history_texture_ref != ref) {
                    history_texture.clear();
                    history_texture_ref.clear();
                    if (load_image_texture(clipboard_store.read(item->ref), history_texture)) {
                        history_texture_ref = item->ref;
                    }
                }
                if (history_texture.id == 0) return std::nullopt;
                return ClipboardTexture{.handle=history_texture.id,.width=history_texture.width,.height=history_texture.height};
            };
            const auto command = render_clipboard_history_panel(
                clipboard_history_state, history_model, ui_language, texture_lookup);
            if (command.kind == ClipboardHistoryCommandKind::Use) {
                if (const auto item = clipboard_store.item(command.ref)) {
                    if (mime_is_image(*item)) {
                        const auto mime=item->mime_types.empty()?std::string{"image/png"}:item->mime_types.front();
                        platform->publish_image(clipboard_store.read(item->ref), mime);
                    } else {
                        platform->publish_text(clipboard_store.read_text(item->ref));
                    }
                }
            } else if (command.kind == ClipboardHistoryCommandKind::Copy) {
                if (const auto item = clipboard_store.item(command.ref); item && !mime_is_image(*item)) {
                    platform->copy_text(clipboard_store.read_text(item->ref));
                }
            } else if (command.kind == ClipboardHistoryCommandKind::Save) {
                if (const auto item = clipboard_store.item(command.ref)) {
                    ActionInstance save;
                    save.id = "history_save_" + item->ref;
                    save.source_ref = item->ref;
                    save.label = "Save clipboard item";
                    save.enabled = true;
                    switch (item->kind) {
                        case ContentKind::Image: save.kind = ActionKind::SaveImageFile; break;
                        case ContentKind::Url: save.kind = ActionKind::SaveUrlFile; break;
                        case ContentKind::Email: save.kind = ActionKind::SaveEmailFile; break;
                        case ContentKind::Json: save.kind = ActionKind::SaveJsonFile; break;
                        case ContentKind::Path: save.kind = ActionKind::CopyPathToDirectory; break;
                        default: save.kind = ActionKind::SaveTextFile; break;
                    }
                    active_batch.catalog.actions.push_back(save);
                    activated_action = save.id;
                }
            }
        }
        if (main_tab == static_cast<int>(MainTab::Settings)) {
            const auto& pal = palette();
            struct SettingsSectionEntry { SettingsPage page; const char* glyph; UiTextKey label; };
            static constexpr SettingsSectionEntry sections[] = {
                {SettingsPage::Home, icon::kBook, UiTextKey::Home},
                {SettingsPage::General, icon::kSliders, UiTextKey::General},
                {SettingsPage::Ranking, icon::kGauge, UiTextKey::Djev},
                {SettingsPage::Llm, icon::kAi, UiTextKey::GeneralLlm},
                {SettingsPage::Prompts, icon::kFileText, UiTextKey::PromptTemplates},
                {SettingsPage::FastActions, icon::kZap, UiTextKey::FastActions},
                {SettingsPage::Pipelines, icon::kPipeline, UiTextKey::Pipeline},
                {SettingsPage::Privacy, icon::kShield, UiTextKey::Privacy},
                {SettingsPage::Usage, icon::kChart, UiTextKey::UsageInsights},
            };
            const float body_height = ImGui::GetContentRegionAvail().y - footer_height();
            // Section list.
            ImGui::BeginChild("settings-nav", ImVec2(ui_layout.settings_sidebar * ImGui::GetStyle().FontScaleDpi, body_height), false);
            for (const auto& entry : sections) {
                const bool selected = settings_page == entry.page;
                if (nav_item(tr(ui_language, entry.label).c_str(), with_icon(entry.glyph, tr(ui_language, entry.label)),
                             selected)) {
                    settings_page = entry.page;
                }
            }
            ImGui::EndChild();
            ImGui::SameLine(0.0F, 0.0F);
            // Sidebar | page splitter; the width is saved when the drag ends.
            (void)vertical_splitter("##settings-splitter", ui_layout.settings_sidebar, 120.0F, 320.0F, 158.0F, body_height);
            if (ImGui::IsItemDeactivated() || (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
                (void)save_ui_layout(app_data_dir() / "ui-state.json", ui_layout);
            }
            ImGui::SameLine(0.0F, 0.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0F, 6.0F));
            ImGui::BeginChild("settings-page", ImVec2(0.0F, body_height), ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PopStyleVar();
            const auto path_field = [&](const char* id, std::filesystem::path& value) {
                auto text = path_to_utf8_string(value);
                if (input_text_string(id, text)) value = path_from_utf8_string(text);
            };
            // Provider checks shared by the Home page and the provider pages.
            const auto start_djev_test = [&] {
                if (pending_provider_test) return;
                pending_provider_test_is_djev = true;
                djev_test_status = tr(ui_language, UiTextKey::Testing);
                const auto provider = settings_draft.djev;
                const auto language_copy = ui_language;
                pending_provider_test.emplace(std::async(std::launch::async, [provider,language_copy] {
                    const auto result = DjevClient(provider.endpoint, provider.model_id, DjevClient::kDefaultTimeout, {}, provider.api_key).decide(provider_test_request());
                    return result.valid ? tr(language_copy,UiTextKey::DjevTestSucceeded) : tr(language_copy,UiTextKey::DjevTestFailed) + result.error;
                }));
            };
            const auto start_llm_test = [&] {
                if (pending_provider_test) return;
                pending_provider_test_is_djev = false;
                llm_test_status = tr(ui_language, UiTextKey::Testing);
                const auto provider = settings_draft.general_llm;
                const auto language_copy = ui_language;
                pending_provider_test.emplace(std::async(std::launch::async, [provider,language_copy] {
                    OpenAiCompatibleClient client;
                    // Remote HTTPS models can take several seconds for a first token.
                    const auto result = client.generate({.request_id="settings-test",.endpoint=provider.endpoint,.api_key=provider.api_key,.model_id=provider.model_id,.system_message="Return OK.",.user_message="OK",.temperature=0.0,.timeout=std::chrono::milliseconds{20000}});
                    return result.ok ? tr(language_copy,UiTextKey::GeneralLlmTestSucceeded) : tr(language_copy,UiTextKey::GeneralLlmTestFailed) + result.error;
                }));
            };
            switch (settings_page) {
            case SettingsPage::Home: {
                ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6F);
                ImGui::TextColored(pal.accent, "%s", with_icon(icon::kPaste, "PasteIt").c_str());
                ImGui::PopFont();
                ImGui::PushTextWrapPos(0.0F);
                ImGui::TextUnformatted(tr(ui_language, UiTextKey::HomeTagline).c_str());
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
                // Principles as a 2-column grid of cards.
                struct Principle { const char* glyph; UiTextKey title; UiTextKey body; };
                static constexpr Principle principles[] = {
                    {icon::kZap, UiTextKey::PrincipleHandy, UiTextKey::PrincipleHandyBody},
                    {icon::kGauge, UiTextKey::PrincipleFast, UiTextKey::PrincipleFastBody},
                    {icon::kPipeline, UiTextKey::PrinciplePipeline, UiTextKey::PrinciplePipelineBody},
                    {icon::kGlobe, UiTextKey::PrincipleCrossPlatform, UiTextKey::PrincipleCrossPlatformBody},
                    {icon::kBrain, UiTextKey::PrincipleLearns, UiTextKey::PrincipleLearnsBody},
                    {icon::kShield, UiTextKey::PrinciplePrivate, UiTextKey::PrinciplePrivateBody},
                };
                if (ImGui::BeginTable("home-principles", 2, ImGuiTableFlags_SizingStretchSame)) {
                    for (const auto& principle : principles) {
                        ImGui::TableNextColumn();
                        ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.surface);
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0F, 10.0F));
                        ImGui::BeginChild(tr(ui_language, principle.title).c_str(), ImVec2(0.0F, ImGui::GetTextLineHeightWithSpacing() * 4.6F),
                                          ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
                        ImGui::TextColored(pal.accent, "%s", with_icon(principle.glyph, tr(ui_language, principle.title)).c_str());
                        ImGui::PushTextWrapPos(0.0F);
                        ImGui::TextColored(pal.text_muted, "%s", tr(ui_language, principle.body).c_str());
                        ImGui::PopTextWrapPos();
                        ImGui::EndChild();
                        ImGui::PopStyleVar();
                        ImGui::PopStyleColor();
                    }
                    ImGui::EndTable();
                }
                separator_heading(tr(ui_language, UiTextKey::HowToUse));
                begin_group("home-how-to");
                ImGui::PushTextWrapPos(0.0F);
                {
                    auto how_to = tr(ui_language, UiTextKey::HowToUseBody);
                    const auto hotkey = parse_hotkey(settings.global_hotkey).value_or(default_hotkey());
                    if (const auto at = how_to.find("{shortcut}"); at != std::string::npos) how_to.replace(at, 10, display_hotkey(hotkey));
                    ImGui::TextUnformatted(how_to.c_str());
                }
                ImGui::PopTextWrapPos();
                end_group();

                separator_heading(tr(ui_language, UiTextKey::Providers));
                const auto provider_row = [&](const char* id, const char* glyph, UiTextKey name, const ProviderSettings& provider,
                                              const std::string& status, const auto& start_test, SettingsPage page) {
                    ImGui::PushID(id);
                    if (ImGui::Selectable(with_icon(glyph, tr(ui_language, name)).c_str(), false, 0,
                                          ImVec2(170.0F * ImGui::GetStyle().FontScaleDpi, 0.0F))) settings_page = page;
                    ImGui::SameLine();
                    ImGui::BeginDisabled(provider.endpoint.empty() || pending_provider_test.has_value());
                    if (ImGui::Button(with_icon(icon::kPlay, tr(ui_language, UiTextKey::Test)).c_str())) start_test();
                    ImGui::EndDisabled();
                    if (!status.empty()) {
                        ImGui::SameLine();
                        ImGui::PushTextWrapPos(0.0F);
                        ImGui::TextColored(pal.text_muted, "%s", status.c_str());
                        ImGui::PopTextWrapPos();
                    }
                    // Model and endpoint on their own line so a long URL never hides the button.
                    const auto where = provider.endpoint.empty() ? tr(ui_language, UiTextKey::NotConfigured)
                                                                 : provider.model_id + "  \xC2\xB7  " + provider.endpoint;
                    ImGui::Indent(ImGui::GetFrameHeight());
                    ImGui::PushTextWrapPos(0.0F);
                    ImGui::TextColored(pal.text_muted, "%s", where.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::Unindent(ImGui::GetFrameHeight());
                    ImGui::PopID();
                };
                begin_group("home-providers");
                provider_row("home-djev", icon::kGauge, UiTextKey::Djev, settings_draft.djev, djev_test_status, start_djev_test, SettingsPage::Ranking);
                ImGui::Separator();
                provider_row("home-llm", icon::kAi, UiTextKey::GeneralLlm, settings_draft.general_llm, llm_test_status, start_llm_test, SettingsPage::Llm);
                end_group();

                separator_heading(tr(ui_language, UiTextKey::AtAGlance));
                std::size_t learned = 0;
                for (const auto& [context, actions] : usage_model.contexts) learned += actions.size();
                const std::pair<std::size_t, UiTextKey> stats[] = {
                    {clipboard_store.items().size(), UiTextKey::ClipboardItemsStat},
                    {path_history.recent(1000).size(), UiTextKey::RecentPathsStat},
                    {learned, UiTextKey::LearnedActionsStat},
                    {settings_draft.prompt_templates.size(), UiTextKey::PromptsStat},
                    {settings_draft.pipelines.recipes.size(), UiTextKey::RecipesStat},
                };
                begin_group("home-stats-group");
                if (ImGui::BeginTable("home-stats", 5, ImGuiTableFlags_SizingStretchSame)) {
                    for (const auto& [value, label] : stats) {
                        ImGui::TableNextColumn();
                        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35F);
                        ImGui::TextColored(pal.accent, "%zu", value);
                        ImGui::PopFont();
                        ImGui::PushTextWrapPos(0.0F);
                        ImGui::TextColored(pal.text_muted, "%s", tr(ui_language, label).c_str());
                        ImGui::PopTextWrapPos();
                    }
                    ImGui::EndTable();
                }
                end_group();
                ImGui::Spacing();
                if (ImGui::Button(with_icon(icon::kLink, tr(ui_language, UiTextKey::OpenRepository)).c_str())) {
                    (void)platform->open_uri("https://github.com/jstdlee/pasteit");
                }
                ImGui::SameLine();
                ImGui::TextColored(pal.text_muted, "%s %s \xC2\xB7 C++20 \xC2\xB7 Dear ImGui %s",
                                   tr(ui_language, UiTextKey::BuildInfo).c_str(), PASTEIT_GIT_COMMIT, IMGUI_VERSION);
                break;
            }
            case SettingsPage::General: {
                section_heading(icon::kSliders, tr(ui_language, UiTextKey::General));
                if (begin_form("settings-general")) {
                    form_row(tr(ui_language, UiTextKey::GlobalShortcut));
                    draw_hotkey_editor("global-hotkey", settings_draft.global_hotkey, hotkey_editor_state, *platform,
                                       ui_language, pal);
                    if (!global_shortcut_ok && settings.global_hotkey == settings_draft.global_hotkey) {
                        ImGui::PushTextWrapPos(0.0F);
                        ImGui::TextColored(pal.danger, "%s", tr(ui_language, UiTextKey::ShortcutRegisterStartupFailed).c_str());
                        ImGui::PopTextWrapPos();
                    }
                    form_row(tr(ui_language, UiTextKey::Language));
                    int language = static_cast<int>(settings_draft.language);
                    static const std::string languages[] = {"System", "English", "简体中文"};
                    if (choice_control("##language", languages, language)) settings_draft.language = static_cast<UiLanguage>(language);
                    form_row(tr(ui_language, UiTextKey::Theme));
                    // Shown System, Light, Dark, Tokyo Night; stored as UiTheme values.
                    static constexpr UiTheme theme_order[] = {UiTheme::System, UiTheme::Light, UiTheme::Dark, UiTheme::TokyoNight};
                    int theme = static_cast<int>(std::find(std::begin(theme_order), std::end(theme_order), settings_draft.theme) - std::begin(theme_order));
                    const std::string themes[] = {tr(ui_language, UiTextKey::ThemeSystem), tr(ui_language, UiTextKey::ThemeLight),
                                                  tr(ui_language, UiTextKey::ThemeDark), "Tokyo Night"};
                    if (choice_control("##theme", themes, theme)) settings_draft.theme = theme_order[theme];
                    form_row(tr(ui_language, UiTextKey::Opacity));
                    if (ImGui::SliderFloat("##opacity", &settings_draft.window_opacity, 0.55F, 1.0F, "%.2f") &&
                        !platform->set_popup_opacity(settings_draft.window_opacity)) glfwSetWindowOpacity(window, settings_draft.window_opacity);
                    form_row(tr(ui_language, UiTextKey::DefaultImageDirectory));
                    path_field("##image-dir", settings_draft.default_image_directory);
                    form_row(tr(ui_language, UiTextKey::DefaultTextDirectory));
                    path_field("##text-dir", settings_draft.default_text_directory);
                    end_form();
                }
                ImGui::Spacing();
                separator_heading(tr(ui_language, UiTextKey::ClipboardHistory));
                begin_group("settings-history");
                if (ImGui::Button(tr(ui_language,UiTextKey::KeepLatest10).c_str())) rewrite_history(10, ui_language);
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, pal.danger);
                if (ImGui::Button(tr(ui_language,UiTextKey::DeleteAllHistory).c_str())) rewrite_history(0, ui_language);
                ImGui::PopStyleColor();
                end_group();
                break;
            }
            case SettingsPage::Ranking: {
                section_heading(icon::kGauge, tr(ui_language, UiTextKey::Djev), tr(ui_language, UiTextKey::DjevHelp));
                if (begin_form("settings-djev")) {
                    form_row(tr(ui_language, UiTextKey::DjevEndpoint));
                    input_text_string("##djev-endpoint", settings_draft.djev.endpoint);
                    form_row(tr(ui_language, UiTextKey::DjevModel));
                    input_text_string("##djev-model", settings_draft.djev.model_id);
                    form_row(tr(ui_language, UiTextKey::DjevApiKey));
                    input_text_string("##djev-key", settings_draft.djev.api_key, false, ImGuiInputTextFlags_Password);
                    end_form();
                }
                if (ImGui::Button(with_icon(icon::kPlay, tr(ui_language,UiTextKey::TestDjev)).c_str())) start_djev_test();
                ImGui::SameLine();
                if (!djev_test_status.empty()) ImGui::TextColored(pal.text_muted, "%s", djev_test_status.c_str());
                ImGui::Spacing();
                if (ImGui::Button(tr(ui_language,UiTextKey::ResetProviders).c_str())) {
                    const auto defaults = default_settings();
                    settings_draft.djev = defaults.djev;
                    settings_draft.general_llm = defaults.general_llm;
                }
                break;
            }
            case SettingsPage::Llm: {
                section_heading(icon::kAi, tr(ui_language, UiTextKey::GeneralLlm), tr(ui_language, UiTextKey::LlmHelp));
                if (!pending_model_list.has_value() && model_list_endpoint != settings_draft.general_llm.endpoint) {
                    general_llm_models.clear();
                    model_list_status.clear();
                    model_list_endpoint.clear();
                }
                if (model_list_endpoint.empty() && !pending_model_list.has_value()) request_model_list();
                if (begin_form("settings-llm")) {
                    form_row(tr(ui_language, UiTextKey::LlmEndpoint));
                    input_text_string("##llm-endpoint", settings_draft.general_llm.endpoint);
                    form_row(tr(ui_language, UiTextKey::LlmModel));
                    const auto model_preview = settings_draft.general_llm.model_id.empty()
                        ? tr(ui_language, UiTextKey::ModelList) : settings_draft.general_llm.model_id;
                    const float refresh_width = ImGui::CalcTextSize(tr(ui_language,UiTextKey::RefreshModels).c_str()).x +
                                                ImGui::GetStyle().FramePadding.x * 2.0F + ImGui::GetStyle().ItemSpacing.x;
                    ImGui::SetNextItemWidth(-refresh_width);
                    if (begin_combo("##llm-model-list", model_preview.c_str())) {
                        for (const auto& model_id : general_llm_models) {
                            const bool selected = settings_draft.general_llm.model_id == model_id;
                            if (ImGui::Selectable(model_id.c_str(), selected)) settings_draft.general_llm.model_id = model_id;
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(tr(ui_language,UiTextKey::RefreshModels).c_str())) {
                        general_llm_models.clear();
                        model_list_status.clear();
                        model_list_endpoint.clear();
                        request_model_list();
                    }
                    form_row(tr(ui_language, UiTextKey::LlmModel) + " ID");
                    input_text_string("##llm-model", settings_draft.general_llm.model_id);
                    form_row(tr(ui_language, UiTextKey::LlmApiKey));
                    input_text_string("##llm-key", settings_draft.general_llm.api_key, false, ImGuiInputTextFlags_Password);
                    end_form();
                }
                if (!model_list_status.empty()) {
                    ImGui::PushTextWrapPos(0.0F);
                    ImGui::TextColored(pal.warning, "%s", (tr(ui_language,UiTextKey::ModelListFailed) + model_list_status).c_str());
                    ImGui::PopTextWrapPos();
                }
                if (ImGui::Button(with_icon(icon::kPlay, tr(ui_language,UiTextKey::TestGeneralLlm)).c_str())) start_llm_test();
                ImGui::SameLine();
                if (!llm_test_status.empty()) ImGui::TextColored(pal.text_muted, "%s", llm_test_status.c_str());
                break;
            }
            case SettingsPage::Prompts: {
                section_heading(icon::kFileText, tr(ui_language, UiTextKey::PromptTemplates), tr(ui_language, UiTextKey::PromptTemplatesHelp));
                PromptTemplateService service(settings_draft.prompt_templates);
                if (prompt_panel_model.modal == PromptTemplateModal::None) prompt_panel_model = build_prompt_templates_panel_model(settings_draft.prompt_templates);
                std::string view_id, edit_id, duplicate_id, delete_id;
                // Name, on/off and thinking per template; temperature lives in the editor.
                if (ImGui::BeginTable("settings-prompt-templates", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                      ImGuiTableFlags_SizingStretchProp)) {
                    const float toggle_column = std::round(ImGui::GetFrameHeight() * 0.8F * 1.75F) + 8.0F;
                    ImGui::TableSetupColumn(tr(ui_language,UiTextKey::Enabled).c_str(), ImGuiTableColumnFlags_WidthFixed,
                                            std::max(toggle_column, ImGui::CalcTextSize(tr(ui_language,UiTextKey::Enabled).c_str()).x));
                    ImGui::TableSetupColumn(tr(ui_language,UiTextKey::Name).c_str(), ImGuiTableColumnFlags_WidthStretch, 1);
                    ImGui::TableSetupColumn(tr(ui_language,UiTextKey::ThinkingColumn).c_str(), ImGuiTableColumnFlags_WidthFixed,
                                            std::max(toggle_column, ImGui::CalcTextSize(tr(ui_language,UiTextKey::ThinkingColumn).c_str()).x));
                    ImGui::TableSetupColumn(tr(ui_language,UiTextKey::Actions).c_str(), ImGuiTableColumnFlags_WidthFixed, icon_buttons_width(4));
                    ImGui::TableHeadersRow();
                    for (const auto& row : prompt_panel_model.rows) {
                        ImGui::PushID(("settings-"+row.id).c_str());
                        ImGui::TableNextRow(); ImGui::TableNextColumn();
                        bool enabled = row.enabled;
                        if (toggle_switch("##enabled", &enabled)) { std::string error; service.set_enabled(row.id, enabled, error); }
                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        // Built-in templates are marked by a muted name and a tooltip.
                        ImGui::TextColored(row.built_in ? pal.text_muted : pal.text, "%s", row.name.c_str());
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                            char temperature[32];
                            std::snprintf(temperature, sizeof temperature, "%.1f", row.temperature);
                            ImGui::SetTooltip("%s%s%s %s", row.built_in ? tr(ui_language,UiTextKey::BuiltIn).c_str() : "",
                                              row.built_in ? "\n" : "", tr(ui_language, UiTextKey::Temperature).c_str(), temperature);
                        }
                        ImGui::TableNextColumn();
                        bool thinking = row.thinking;
                        if (toggle_switch("##thinking", &thinking)) { std::string error; service.set_thinking(row.id, thinking, error); }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                            ImGui::SetTooltip("%s\n%s", tr(ui_language, UiTextKey::PromptThinking).c_str(),
                                              tr(ui_language, UiTextKey::PromptThinkingHelp).c_str());
                        }
                        ImGui::TableNextColumn();
                        if (icon_button("view", icon::kEye, tr(ui_language, UiTextKey::View))) view_id = row.id;
                        ImGui::SameLine();
                        if (icon_button("edit", icon::kEdit, tr(ui_language, UiTextKey::Edit))) edit_id = row.id;
                        ImGui::SameLine();
                        if (icon_button("duplicate", icon::kCopy, tr(ui_language, UiTextKey::Duplicate))) duplicate_id = row.id;
                        ImGui::SameLine();
                        if (icon_button("delete", icon::kTrash, tr(ui_language, UiTextKey::Delete))) delete_id = row.id;
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                if (!view_id.empty()) view_prompt_template(prompt_panel_model, view_id);
                if (!edit_id.empty()) begin_prompt_template_edit(prompt_panel_model, edit_id);
                if (!duplicate_id.empty()) duplicate_prompt_template(prompt_panel_model, service, duplicate_id);
                if (!delete_id.empty()) begin_prompt_template_delete(prompt_panel_model, delete_id);
                ImGui::Spacing();
                if (ImGui::Button(with_icon(icon::kPlus, tr(ui_language,UiTextKey::NewTemplate)).c_str())) { std::string error; if (const auto created = service.create("New Prompt", "Transform {text}", 0.2, error)) { prompt_panel_model = build_prompt_templates_panel_model(settings_draft.prompt_templates); begin_prompt_template_edit(prompt_panel_model, created->id); } }
                ImGui::SameLine();
                if (ImGui::Button(with_icon(icon::kUndo, tr(ui_language,UiTextKey::RestoreDefaultTemplates)).c_str())) { service.restore_defaults(); prompt_panel_model = build_prompt_templates_panel_model(settings_draft.prompt_templates); }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tr(ui_language, UiTextKey::RestoreDefaultTemplatesHelp).c_str());
                // Instructions used by Optimize with LLM in the template editor.
                separator_heading(tr(ui_language, UiTextKey::PromptOptimizerInstructions));
                begin_group("settings-prompt-optimizer");
                ImGui::PushTextWrapPos(0.0F);
                ImGui::TextColored(pal.text_muted, "%s", tr(ui_language, UiTextKey::PromptOptimizerInstructionsHelp).c_str());
                ImGui::PopTextWrapPos();
                input_text_string("##prompt-optimizer-system", settings_draft.prompt_optimizer_system, true, 0,
                                  ImGui::GetTextLineHeightWithSpacing() * 9.0F);
                ImGui::BeginDisabled(settings_draft.prompt_optimizer_system == default_prompt_optimizer_system());
                if (ImGui::Button(with_icon(icon::kUndo, tr(ui_language, UiTextKey::ResetPrompt)).c_str())) {
                    settings_draft.prompt_optimizer_system = default_prompt_optimizer_system();
                }
                ImGui::EndDisabled();
                end_group();
                if (prompt_panel_model.modal == PromptTemplateModal::View && prompt_panel_model.draft) {
                    bool detail_open = true;
                    if (begin_tool_window(prompt_panel_model.draft->name + "###settings-prompt-detail", &detail_open, ImVec2(540, 360))) {
                        ImGui::BeginChild("settings-prompt-detail-body", ImVec2(0.0F, -footer_height()), ImGuiChildFlags_Borders);
                        copyable_text(prompt_panel_model.draft->system_prompt, true);
                        ImGui::EndChild();
                        const int clicked = footer_buttons({{tr(ui_language,UiTextKey::Edit)}, {tr(ui_language,UiTextKey::Close), true}});
                        if (clicked == 0) begin_prompt_template_edit(prompt_panel_model, prompt_panel_model.draft->id);
                        if (clicked == 1) detail_open = false;
                    }
                    ImGui::End();
                    if (!detail_open && prompt_panel_model.modal == PromptTemplateModal::View) cancel_prompt_template_modal(prompt_panel_model);
                }
                if (prompt_panel_model.modal == PromptTemplateModal::Edit && prompt_panel_model.draft) {
                    bool edit_open = true;
                    if (begin_tool_window(tr(ui_language,UiTextKey::Edit) + "###settings-prompt-edit", &edit_open, ImVec2(620, 460))) {
                        if (begin_form("prompt-edit-form", 140.0F)) {
                            form_row(tr(ui_language,UiTextKey::TemplateName));
                            input_text_string("##template-name", prompt_panel_model.draft->name);
                            form_row(tr(ui_language,UiTextKey::Enabled));
                            toggle_switch("##template-enabled", &prompt_panel_model.draft->enabled);
                            form_row(tr(ui_language,UiTextKey::Temperature));
                            float temperature = static_cast<float>(prompt_panel_model.draft->temperature);
                            if (ImGui::SliderFloat("##template-temperature", &temperature, 0.0F, 2.0F, "%.1f")) prompt_panel_model.draft->temperature = temperature;
                            form_row(tr(ui_language,UiTextKey::ThinkingColumn));
                            toggle_switch(tr(ui_language,UiTextKey::PromptThinking).c_str(), &prompt_panel_model.draft->thinking);
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tr(ui_language, UiTextKey::PromptThinkingHelp).c_str());
                            end_form();
                        }
                        auto& draft = *prompt_panel_model.draft;
                        if (prompt_optimize_template_id != draft.id) {
                            // A different template: forget the previous optimization.
                            prompt_optimize_template_id = draft.id;
                            prompt_optimize_original.clear();
                            prompt_optimize_status.clear();
                        }
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextColored(pal.text_muted, "%s", tr(ui_language,UiTextKey::SystemPrompt).c_str());
                        // Optimize / Revert sit at the right of the label row.
                        const auto optimize_label = with_icon(icon::kWand, tr(ui_language, UiTextKey::OptimizePrompt));
                        const auto revert_label = with_icon(icon::kUndo, tr(ui_language, UiTextKey::RevertOptimization));
                        const auto& style = ImGui::GetStyle();
                        const auto reset_label = with_icon(icon::kRotateCcw, tr(ui_language, UiTextKey::ResetPrompt));
                        const auto defaults = default_prompt_templates();
                        const auto shipped = std::find_if(defaults.begin(), defaults.end(), [&](const auto& value) { return value.id == draft.id; });
                        const bool built_in = shipped != defaults.end();
                        float buttons = ImGui::CalcTextSize(optimize_label.c_str()).x + style.FramePadding.x * 2.0F;
                        if (built_in) buttons += button_width(reset_label) + style.ItemSpacing.x;
                        if (!prompt_optimize_original.empty()) buttons += ImGui::CalcTextSize(revert_label.c_str()).x + style.FramePadding.x * 2.0F + style.ItemSpacing.x;
                        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - buttons));
                        if (built_in) {
                            // Back to the shipped prompt, in case the edits went astray.
                            ImGui::BeginDisabled(draft.system_prompt == shipped->system_prompt);
                            if (ImGui::Button(reset_label.c_str(), ImVec2(button_width(reset_label), 0.0F))) {
                                draft.system_prompt = shipped->system_prompt;
                                prompt_optimize_original.clear();
                                prompt_optimize_status.clear();
                            }
                            ImGui::EndDisabled();
                            ImGui::SameLine();
                        }
                        const bool llm_ready = !settings_draft.general_llm.endpoint.empty() && !settings_draft.general_llm.model_id.empty();
                        ImGui::BeginDisabled(pending_prompt_optimize.has_value() || !llm_ready || trim(draft.system_prompt).empty());
                        if (ImGui::Button(optimize_label.c_str())) {
                            prompt_optimize_template_id = draft.id;
                            prompt_optimize_status = tr(ui_language, UiTextKey::OptimizingPrompt);
                            const auto provider = settings_draft.general_llm;
                            const auto raw = draft.system_prompt;
                            const auto instructions = prompt_optimizer_system(settings_draft.prompt_optimizer_system);
                            pending_prompt_optimize.emplace(std::async(std::launch::async, [provider, raw, instructions] {
                                const auto result = OpenAiCompatibleClient{}.generate({
                                    .request_id = "prompt-optimize", .endpoint = provider.endpoint, .api_key = provider.api_key,
                                    .model_id = provider.model_id, .system_message = instructions,
                                    .user_message = prompt_optimizer_user_message(raw), .temperature = 0.3,
                                    .timeout = std::chrono::milliseconds{60000}});
                                if (!result.ok) return OptimizedPrompt{.prompt = {}, .restored_placeholders = {}, .error = result.error};
                                return finish_optimized_prompt(result.content, raw);
                            }));
                        }
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("%s", llm_ready ? tr(ui_language, UiTextKey::OptimizePromptHelp).c_str()
                                                              : "Configure the General LLM in Settings first");
                        }
                        if (!prompt_optimize_original.empty()) {
                            ImGui::SameLine();
                            if (ImGui::Button(revert_label.c_str())) {
                                draft.system_prompt = prompt_optimize_original;
                                prompt_optimize_original.clear();
                                prompt_optimize_status.clear();
                            }
                        }
                        const float status_height = prompt_optimize_status.empty() ? 0.0F : ImGui::GetTextLineHeightWithSpacing() * 2.0F;
                        input_text_string("##template-system-prompt", draft.system_prompt, true, 0, -footer_height() - status_height);
                        if (!prompt_optimize_status.empty()) {
                            ImGui::PushTextWrapPos(0.0F);
                            ImGui::TextColored(pending_prompt_optimize ? pal.text_muted : pal.accent, "%s", prompt_optimize_status.c_str());
                            ImGui::PopTextWrapPos();
                        }
                        const int clicked = footer_buttons({{tr(ui_language,UiTextKey::Cancel)}, {tr(ui_language,UiTextKey::Save), true}});
                        if (clicked == 0) edit_open = false;
                        if (clicked == 1) { const auto result = save_prompt_template_edit(prompt_panel_model, service); if (result.error.empty()) edit_open = false; }
                    }
                    ImGui::End();
                    if (!edit_open && prompt_panel_model.modal != PromptTemplateModal::None) cancel_prompt_template_modal(prompt_panel_model);
                }
                if (prompt_panel_model.modal == PromptTemplateModal::Delete) {
                    bool delete_open = true;
                    if (begin_tool_window(tr(ui_language,UiTextKey::Delete) + "###settings-prompt-delete", &delete_open, ImVec2(420, 170))) {
                        ImGui::PushTextWrapPos(0.0F);
                        ImGui::TextUnformatted(tr(ui_language,UiTextKey::DeleteTemplateText).c_str());
                        ImGui::PopTextWrapPos();
                        const int clicked = footer_buttons({{tr(ui_language,UiTextKey::Keep)}, {tr(ui_language,UiTextKey::ConfirmDelete), true}});
                        if (clicked == 0) delete_open = false;
                        if (clicked == 1) { confirm_prompt_template_delete(prompt_panel_model, service); delete_open = false; }
                    }
                    ImGui::End();
                    if (!delete_open && prompt_panel_model.modal != PromptTemplateModal::None) cancel_prompt_template_modal(prompt_panel_model);
                }
                break;
            }
            case SettingsPage::FastActions: {
                section_heading(icon::kZap, tr(ui_language, UiTextKey::FastActions));
                separator_heading(tr(UiTextKey::Downloads));
                if (begin_form("settings-downloads")) {
                    form_row(tr(UiTextKey::DownloadResumeDirectory));
                    path_field("##download-dir", settings_draft.downloads.resume_directory);
                    form_row(tr(UiTextKey::KeepPartFiles));
                    toggle_switch("##keep-part", &settings_draft.downloads.keep_part_files);
                    end_form();
                }
                separator_heading(tr(UiTextKey::TerminalAndFiles));
                if (begin_form("settings-terminal", 190.0F)) {
                    form_row(tr(UiTextKey::TerminalCommand));
                    auto terminal_command = join_argv(settings_draft.terminal.command);
                    if (input_text_string("##terminal-command", terminal_command)) settings_draft.terminal.command = split_argv_field(terminal_command);
                    form_row(tr(UiTextKey::TerminalProfile));
                    input_text_string("##terminal-profile", settings_draft.terminal.profile);
                    bool sha256 = std::find(settings_draft.hash.default_algorithms.begin(), settings_draft.hash.default_algorithms.end(), "sha256") != settings_draft.hash.default_algorithms.end();
                    bool sha512 = std::find(settings_draft.hash.default_algorithms.begin(), settings_draft.hash.default_algorithms.end(), "sha512") != settings_draft.hash.default_algorithms.end();
                    form_row(tr(UiTextKey::ShowSha256));
                    const bool hash_changed_256 = toggle_switch("##sha256", &sha256);
                    form_row(tr(UiTextKey::ShowSha512));
                    const bool hash_changed_512 = toggle_switch("##sha512", &sha512);
                    if (hash_changed_256 || hash_changed_512) {
                        settings_draft.hash.default_algorithms.clear();
                        if (sha256) settings_draft.hash.default_algorithms.push_back("sha256");
                        if (sha512) settings_draft.hash.default_algorithms.push_back("sha512");
                    }
                    form_row(tr(UiTextKey::AnnotationDirectory), tr(UiTextKey::AnnotationSvgOnly));
                    path_field("##annotation-dir", settings_draft.annotation.save_directory);
                    settings_draft.annotation.export_format = "svg";
                    end_form();
                }
                separator_heading(tr(UiTextKey::DateTime));
                if (begin_form("settings-datetime")) {
                    form_row(tr(UiTextKey::SourceTimeZone));
                    input_text_string("##source-zone", settings_draft.date_time.source_zone);
                    form_row(tr(UiTextKey::TargetTimeZone));
                    input_text_string("##target-zone", settings_draft.date_time.target_zone);
                    end_form();
                }
                break;
            }
            case SettingsPage::Pipelines:
                draw_pipeline_settings(ui_language);
                break;
            case SettingsPage::Privacy:
                draw_privacy_settings(ui_language);
                break;
            case SettingsPage::Usage: {
                section_heading(icon::kChart, tr(ui_language, UiTextKey::UsageInsights), tr(ui_language,UiTextKey::UsageInsightsHelp));
                const auto summary = usage_summary(usage_model, current_time_ms(), 5);
                if (summary.empty()) ImGui::TextColored(pal.text_muted, "%s", tr(ui_language,UiTextKey::UsageInsightsEmpty).c_str());
                for (const auto& [kind, habits] : summary) {
                    separator_heading(kind);
                    if (ImGui::BeginTable(("usage-" + kind).c_str(), 3, ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch, 3.0F);
                        ImGui::TableSetupColumn("share", ImGuiTableColumnFlags_WidthStretch, 2.0F);
                        ImGui::TableSetupColumn("count", ImGuiTableColumnFlags_WidthFixed, 90.0F);
                        for (const auto& habit : habits) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(usage_action_label(habit.action_key).c_str());
                            ImGui::TableNextColumn();
                            meter(static_cast<float>(habit.share), {}, 6.0F * ImGui::GetStyle().FontScaleDpi);
                            ImGui::TableNextColumn();
                            ImGui::TextColored(pal.text_muted, "%3.0f%%  ×%u", habit.share * 100.0, habit.count);
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, pal.danger);
                if (ImGui::Button(with_icon(icon::kTrash, tr(ui_language,UiTextKey::ResetUsage)).c_str())) {
                    usage_model = {};
                    std::string usage_error;
                    settings_status = usage_store.save(usage_model, usage_error)
                        ? tr(ui_language,UiTextKey::UsageReset) : usage_error;
                }
                ImGui::PopStyleColor();
                break;
            }
            }
            ImGui::EndChild();
            // Footer: status, unsaved marker, Cancel/Save.
            const bool dirty = !(settings_draft == settings);
            const auto status = !settings_status.empty() ? settings_status
                              : dirty ? tr(ui_language, UiTextKey::UnsavedChanges) : std::string{};
            const int clicked = footer_buttons({{tr(ui_language,UiTextKey::Cancel), false, dirty},
                                                {tr(ui_language,UiTextKey::Save), true, dirty}}, status);
            if (clicked == 0) {
                settings_draft = settings;
                settings_status.clear();
                if (!platform->set_popup_opacity(settings.window_opacity)) glfwSetWindowOpacity(window, settings.window_opacity);
            }
            if (clicked == 1) {
                AppSettings candidate = settings; std::string error;
                // Take the new global shortcut before saving so a combination
                // someone else holds is never written as the setting.
                const bool hotkey_changed = settings_draft.global_hotkey != settings.global_hotkey;
                const auto new_hotkey = parse_hotkey(settings_draft.global_hotkey);
                bool hotkey_ok = true;
                if (hotkey_changed && new_hotkey && !has_blocking_conflict(find_hotkey_conflicts(*new_hotkey))) {
                    hotkey_ok = platform->register_global_shortcut(*new_hotkey);
                    hotkey_editor_state.probed_text.clear();
                }
                if (!hotkey_ok) {
                    settings_status = tr(ui_language, UiTextKey::ShortcutRegisterFailed);
                } else if (save_settings_draft(settings_store, settings, settings_draft, candidate, error)) {
                    const bool llm_endpoint_changed = settings.general_llm.endpoint != candidate.general_llm.endpoint;
                    const bool djev_changed = settings.djev.endpoint != candidate.djev.endpoint ||
                                              settings.djev.model_id != candidate.djev.model_id ||
                                              settings.djev.api_key != candidate.djev.api_key;
                    settings = candidate; settings_draft = settings; settings_status = tr(ui_language,UiTextKey::Saved);
                    health_recheck_requested = true;  // endpoints or keys may have changed
                    if (llm_endpoint_changed) { general_llm_models.clear(); model_list_endpoint.clear(); model_list_status.clear(); }
                    (void)platform->set_popup_opacity(settings.window_opacity);
                    if (djev_changed) {
                        djev_client = DjevClient(settings.djev.endpoint, settings.djev.model_id,
                                                 DjevClient::kDefaultTimeout, {}, settings.djev.api_key);
                    }
                    fast_action_executor.set_renderer_settings(settings.renderers);
                    download_manager.set_options({.keep_part_files_on_cancel = settings.downloads.keep_part_files});
                    annotation_panel.export_directory = settings.annotation.save_directory;
                    annotation_panel.export_format = settings.annotation.export_format;
                    platform->apply_settings(settings);
                    if (hotkey_changed) global_shortcut_ok = true;
                } else {
                    settings_status = error;
                    if (hotkey_changed && new_hotkey) {
                        (void)platform->register_global_shortcut(parse_hotkey(settings.global_hotkey).value_or(default_hotkey()));
                        hotkey_editor_state.probed_text.clear();
                    }
                }
            }
            if (dirty && settings_status == tr(ui_language, UiTextKey::Saved)) settings_status.clear();
        }
        if (modal_open) {
            ImGui::EndDisabled();
            ImGui::GetForegroundDrawList(ImGui::GetWindowViewport())->AddRectFilled(
                main_viewport->Pos, ImVec2(main_viewport->Pos.x + main_viewport->Size.x, main_viewport->Pos.y + main_viewport->Size.y),
                ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg));
        }
        if ((hide_popup_requested || (!modal_open && !command_palette.open && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::GetIO().WantTextInput && !hotkey_editor_state.capturing && ImGui::IsKeyPressed(ImGuiKey_Escape)))) {
            hide_popup_requested = false;
            popup_visible = false;
            const bool keep_visible=has_any_auxiliary_window();
            if (!keep_visible) {
                glfwSetWindowAttrib(window, GLFW_FLOATING, GLFW_FALSE);
                glfwHideWindow(window);
            }
        }
        ImGui::End();
        }

        if (prompt_parameter_dialog.open) {
            bool open = true;
            const auto parameter_rows = std::clamp<std::size_t>(prompt_parameter_dialog.names.size(), 1, 10);
            const auto parameter_height = 130.0F + static_cast<float>(parameter_rows) * ImGui::GetFrameHeightWithSpacing();
            const auto title = prompt_parameter_dialog.template_name + "###prompt-parameters";
            if (begin_tool_window(title, &open, ImVec2(520.0F, parameter_height), &prompt_parameter_dialog.focus_pending)) {
                ImGui::TextColored(palette().text_muted, "%s", tr(ui_language, UiTextKey::PromptParametersHelp).c_str());
                ImGui::BeginChild("prompt-parameters-body", ImVec2(0.0F, -footer_height()), false);
                (void)draw_prompt_parameter_fields("prompt-parameters-form", prompt_parameter_dialog.names,
                                                   prompt_parameter_dialog.values);
                ImGui::EndChild();
                const int clicked = footer_buttons({{tr(ui_language, UiTextKey::Cancel)}, {tr(ui_language, UiTextKey::Confirm), true}});
                if (clicked == 0) prompt_parameter_dialog.open = false;
                if (clicked == 1 || (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
                    PromptVariables values;
                    values.values = prompt_parameter_dialog.values;
                    prompt_parameter_memory[prompt_parameter_dialog.template_id] = values.values;
                    save_choice_memory();
                    activated_prompt_variables = std::move(values);
                    activated_action = prompt_parameter_dialog.action_id;
                    prompt_parameter_dialog.open = false;
                }
            }
            ImGui::End();
            if (!open) prompt_parameter_dialog.open = false;
        }

        if (custom_prompt_dialog.open) {
            // Ask LLM: a prompt template or a custom instruction for the clipboard text.
            const auto title = with_icon(icon::kAi, tr(ui_language, UiTextKey::AskLlmTitle)) + "###custom-prompt";
            if (begin_tool_window(title, &custom_prompt_dialog.open, ImVec2(580, 320), &custom_prompt_dialog.focus_pending)) {
                const auto custom_label = tr(ui_language, UiTextKey::CustomPrompt);
                const auto selected = std::find_if(settings.prompt_templates.begin(), settings.prompt_templates.end(),
                    [&](const PromptTemplate& prompt) { return prompt.id == custom_prompt_dialog.template_id; });
                const bool custom = selected == settings.prompt_templates.end();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (begin_combo("##ask-template", custom ? custom_label.c_str() : selected->name.c_str())) {
                    if (ImGui::Selectable(custom_label.c_str(), custom)) {
                        custom_prompt_dialog.template_id.clear();
                        custom_prompt_dialog.thinking = false;
                    }
                    for (const auto& prompt : settings.prompt_templates) {
                        if (prompt.enabled && ImGui::Selectable(prompt.name.c_str(), prompt.id == custom_prompt_dialog.template_id)) {
                            custom_prompt_dialog.template_id = prompt.id;
                            custom_prompt_dialog.thinking = prompt.thinking;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (custom) {
                    ImGui::TextColored(palette().text_muted, "%s", tr(ui_language, UiTextKey::PromptInstructions).c_str());
                    input_text_string("##custom-prompt-input", custom_prompt_dialog.prompt, true, 0, -footer_height());
                } else {
                    ImGui::PushTextWrapPos(0.0F);
                    ImGui::TextColored(palette().text_muted, "%s", selected->system_prompt.c_str());
                    ImGui::PopTextWrapPos();
                    // The template's placeholders (e.g. source/target language) are editable here too.
                    const auto names = prompt_variable_names(selected->system_prompt);
                    if (!names.empty()) {
                        auto& values = prompt_parameter_memory[selected->id];
                        fill_prompt_parameter_defaults(names, values);
                        ImGui::Spacing();
                        (void)draw_prompt_parameter_fields("ask-llm-parameters", names, values);
                    }
                }
                toggle_switch(tr(ui_language, UiTextKey::PromptThinking).c_str(), &custom_prompt_dialog.thinking);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tr(ui_language, UiTextKey::PromptThinkingHelp).c_str());
                const bool can_run = !custom || !trim(custom_prompt_dialog.prompt).empty();
                const auto privacy_note = settings.privacy.anonymize_before_llm ? tr(ui_language, UiTextKey::AskLlmHelp) : std::string{};
                const int clicked = footer_buttons({{tr(ui_language, UiTextKey::Cancel)}, {tr(ui_language, UiTextKey::Generate), true, can_run}},
                                                   privacy_note);
                if (clicked == 0) custom_prompt_dialog.open = false;
                const auto source = clicked == 1 ? clipboard_store.item(custom_prompt_dialog.action.source_ref) : std::nullopt;
                if (clicked == 1 && !source) {
                    execution_status = "Clipboard source is no longer available";
                    custom_prompt_dialog.open = false;
                } else if (clicked == 1) {
                    const auto source_text = clipboard_store.read_text(source->ref);
                    const auto [llm_text, protected_text] = protect_for_llm(source_text);
                    // Next Ask LLM starts from this template and its placeholder values.
                    choice_memory.choices["ask_llm.template"] = custom ? std::string{} : selected->id;
                    save_choice_memory();
                    if (custom) {
                        start_text_job(custom_prompt_dialog.action, custom_prompt_dialog.prompt, llm_text, 0.2, protected_text,
                                       custom_prompt_dialog.thinking);
                    } else {
                        PromptVariables variables;
                        if (const auto remembered = prompt_parameter_memory.find(selected->id); remembered != prompt_parameter_memory.end()) {
                            variables.values = remembered->second;
                        }
                        const auto expanded = expand_prompt(*selected, llm_text, variables);
                        start_text_job(custom_prompt_dialog.action, expanded.system_message, expanded.user_message,
                                       selected->temperature, protected_text, custom_prompt_dialog.thinking);
                    }
                    custom_prompt_dialog.open = false;
                }
            }
            ImGui::End();
        }

        draw_chart_view(chart_view, data_view_host, ui_language);
        if (chart_view.open) remember_choice("chart.kind", std::to_string(chart_view.kind));
        draw_table_view(table_view, chart_view, data_view_host, ui_language, default_chart_path());
        draw_diff_view(diff_view, ui_language, data_view_host.copy_text, [&] {
            // Preview in input: the compared result replaces the input text.
            if (!preview_editable) return;
            preview_edit_text = diff_view.right;
            previewing_action_id = diff_action_id;
        });
        draw_markdown_view(markdown_view, data_view_host, ui_language);
        {
            PipelineOptions pipeline_options;
            pipeline_options.allowed_tools = settings.pipelines.allowed_tools;
            pipeline_options.allow_any_program = settings.pipelines.allow_any_program;
            for (const auto& custom : settings.pipelines.custom_commands) pipeline_options.custom_commands[custom.name] = custom.command;
            pipeline_options.anonymize = [options = anonymize_options(), &privacy_vault](std::string_view text) {
                auto copy = options;
                return anonymize_text(text, copy, copy.style == ReplacementStyle::Placeholder ? &privacy_vault : nullptr).text;
            };
            const PipelineViewHost pipeline_host{
                .data = data_view_host,
                .replace_clipboard = [&](std::string_view text) { platform->publish_text(text); },
                .save_recipe = [&](PipelineRecipe recipe) {
                    recipe.id = "custom-" + std::to_string(current_time_ms());
                    settings.pipelines.recipes.push_back(recipe);
                    settings_draft.pipelines.recipes.push_back(recipe);
                    std::string save_error;
                    if (!settings_store.save(settings, save_error)) settings_status = save_error;
                },
            };
            draw_pipeline_view(pipeline_view, pipeline_options, pipeline_host, ui_language);
        }
        {
            AnonymizeViewHost anonymize_host{
                .data = data_view_host,
                .replace_clipboard = [&](std::string_view text) { platform->publish_text(text); },
                .vault = &privacy_vault,
                .templates = {},
                .draw_template_parameters = [&](const std::string& template_id) {
                    const auto prompt = std::find_if(settings.prompt_templates.begin(), settings.prompt_templates.end(),
                                                     [&](const PromptTemplate& value) { return value.id == template_id; });
                    if (prompt == settings.prompt_templates.end()) return;
                    const auto names = prompt_variable_names(prompt->system_prompt);
                    if (names.empty()) return;
                    auto& values = prompt_parameter_memory[prompt->id];
                    fill_prompt_parameter_defaults(names, values);
                    (void)draw_prompt_parameter_fields("anonymize-ask-parameters", names, values);
                },
                .ask_llm = [&](const std::string& template_id, const std::string& custom_prompt, const std::string& text) {
                    ActionInstance action{};
                    action.id = "anonymized_ask_" + std::to_string(++request_counter);
                    action.kind = ActionKind::TransformText;
                    const auto prompt = std::find_if(settings.prompt_templates.begin(), settings.prompt_templates.end(),
                                                     [&](const PromptTemplate& value) { return value.id == template_id; });
                    choice_memory.choices["anonymize.ask_template"] =
                        prompt == settings.prompt_templates.end() ? std::string{} : template_id;
                    save_choice_memory();
                    if (prompt == settings.prompt_templates.end()) {
                        start_text_job(action, custom_prompt, text, 0.2, true, false);
                        return;
                    }
                    PromptVariables variables;
                    if (const auto remembered = prompt_parameter_memory.find(prompt->id); remembered != prompt_parameter_memory.end()) {
                        variables.values = remembered->second;
                    }
                    const auto expanded = expand_prompt(*prompt, text, variables);
                    start_text_job(action, expanded.system_message, expanded.user_message, prompt->temperature, true, prompt->thinking);
                },
            };
            for (const auto& prompt : settings.prompt_templates) {
                if (prompt.enabled) anonymize_host.templates.emplace_back(prompt.id, prompt.name);
            }
            draw_anonymize_view(anonymize_view, anonymize_host, ui_language);
        }
        if (page_consent) {
            bool open = true;
            if (begin_tool_window(with_icon(icon::kScanText, tr(ui_language, UiTextKey::AllowPageFetchTitle)) + "###page-consent",
                                  &open, ImVec2(480.0F, 200.0F), &page_consent_focus)) {
                ImGui::PushTextWrapPos(0.0F);
                ImGui::TextUnformatted(tr(ui_language, UiTextKey::AllowPageFetchBody).c_str());
                ImGui::PopTextWrapPos();
                if (page_consent->parameters.contains("url")) {
                    ImGui::TextColored(palette().text_muted, "%s", display_path(page_consent->parameters.at("url"), 70).c_str());
                }
                const int clicked = footer_buttons({{tr(ui_language, UiTextKey::Cancel)}, {tr(ui_language, UiTextKey::AllowOnce)},
                                                    {tr(ui_language, UiTextKey::AlwaysAllow), true}});
                if (clicked == 2) {
                    settings.privacy.allow_page_fetch = true;
                    settings_draft.privacy.allow_page_fetch = true;
                    std::string save_error;
                    if (!settings_store.save(settings, save_error)) settings_status = save_error;
                }
                if (clicked >= 1) {
                    start_page_summary(*page_consent);
                    open = false;
                }
                if (clicked == 0) open = false;
            }
            ImGui::End();
            if (!open) page_consent.reset();
        }

        if (file_confirmation) {
            auto& state = *file_confirmation;
            bool close_confirmation = false;
            const auto destination_rows = std::clamp<std::size_t>(state.draft.candidate_destinations.size(), 1, 5);
            const float destinations_height = (ImGui::GetFrameHeightWithSpacing() + 4.0F) * static_cast<float>(destination_rows + 1);
            bool open = true;
            const auto confirmation_title = with_icon(icon::kSave, tr(ui_language,UiTextKey::ConfirmFileOperation)) + "###file-confirm";
            if (begin_tool_window(confirmation_title, &open, ImVec2(660, 360 + destinations_height), &state.focus_pending)) {
                const auto& pal = palette();
                ImGui::BeginChild("file-confirm-body", ImVec2(0.0F, -footer_height()), false, ImGuiWindowFlags_HorizontalScrollbar);
                // What is being saved.
                ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.surface);
                ImGui::BeginChild("file-confirm-source", ImVec2(0.0F, ImGui::GetTextLineHeightWithSpacing() * 2.0F + 16.0F),
                                  ImGuiChildFlags_AlwaysUseWindowPadding);
                ImGui::PushTextWrapPos(0.0F);
                ImGui::TextUnformatted(preview_excerpt(state.source_preview, 2).c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndChild();
                ImGui::PopStyleColor();
                if (begin_form("file-confirm-form", 110.0F)) {
                    form_row(tr(ui_language,UiTextKey::Destination));
                    std::string directory = path_to_utf8_string(state.draft.destination);
                    const float browse_width = ImGui::CalcTextSize(tr(ui_language,UiTextKey::Browse).c_str()).x +
                                               ImGui::GetStyle().FramePadding.x * 2.0F + ImGui::GetStyle().ItemSpacing.x + 24.0F;
                    ImGui::SetNextItemWidth(-browse_width);
                    if (input_text_string("##destination", directory)) {
                        state.draft.destination = path_from_utf8_string(directory);
                        manual_destination = state.draft.destination;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(with_icon(icon::kFolderOpen, tr(ui_language,UiTextKey::Browse)).c_str())) {
                        if (const auto chosen = platform->choose_directory(state.draft.destination)) {
                            state.draft.destination = *chosen;
                            manual_destination = *chosen;
                            const auto observed = path_history.observe_path(*chosen, PathKind::Directory,
                                                                           "manual", current_time_ms());
                            state.draft.candidate_destinations.insert(state.draft.candidate_destinations.begin(), observed);
                        }
                    }
                    form_row(tr(ui_language,UiTextKey::Filename));
                    input_text_string("##filename", state.draft.filename);
                    end_form();
                }
                // Suggested destinations, most used first.
                ImGui::TextColored(pal.text_muted, "%s", tr(ui_language,UiTextKey::SuggestedFolders).c_str());
                if (ImGui::BeginTable("confirmation-destinations", 2,
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp,
                                      ImVec2(0.0F, destinations_height))) {
                    ImGui::TableSetupColumn("path", ImGuiTableColumnFlags_WidthStretch, 4.0F);
                    ImGui::TableSetupColumn("uses", ImGuiTableColumnFlags_WidthFixed, 70.0F);
                    for (const auto& shortcut : state.draft.candidate_destinations) {
                        ImGui::PushID(shortcut.ref.c_str());
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        const bool chosen = shortcut.path == state.draft.destination;
                        const auto label = with_icon(icon::kFolder, display_path(path_to_utf8_string(shortcut.path), 80));
                        if (ImGui::Selectable(label.c_str(), chosen, ImGuiSelectableFlags_SpanAllColumns)) {
                            state.draft.destination = shortcut.path;
                            manual_destination = shortcut.path;
                        }
                        ImGui::TableNextColumn();
                        if (shortcut.use_count > 0) ImGui::TextColored(pal.text_muted, "\xC3\x97%u", shortcut.use_count);
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                state.can_confirm = validate_file_operation_draft(state.draft, clipboard_store);
                const auto panel = build_file_operation_confirmation_panel_model(state);
                ImGui::TextColored(pal.text_muted, "%s", (tr(ui_language,UiTextKey::OutputPath) + ": " + panel.output_preview).c_str());
                if (!panel.validation_error.empty()) {
                    ImGui::PushTextWrapPos(0.0F);
                    ImGui::TextColored(pal.danger, "%s", panel.validation_error.c_str());
                    ImGui::PopTextWrapPos();
                }
                ImGui::EndChild();
                const int clicked = footer_buttons({{tr(ui_language,UiTextKey::Cancel)},
                                                    {tr(ui_language,UiTextKey::Confirm), true, state.can_confirm}});
                if (clicked == 0) close_confirmation = true;
                if (clicked == 1 || (state.can_confirm && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
                    if (auto confirmed = confirmed_action(state.draft, clipboard_store)) {
                        confirmed->id += "_confirmed_" + std::to_string(++request_counter);
                        confirmed->parameters["confirmation_complete"] = "1";
                        const auto observed = path_history.observe_path(state.draft.destination,
                            PathKind::Directory, "manual", current_time_ms());
                        confirmed->target_ref = observed.ref;
                        active_batch.catalog.actions.push_back(*confirmed);
                        activated_action = confirmed->id;
                        close_confirmation = true;
                    }
                }
            }
            ImGui::End();
            if (diagnostics && (!open || close_confirmation)) {
            }
            if (!open || close_confirmation) file_confirmation.reset();
        }

        if (contact_panel.open && fast_action_executor.contact_result().has_value()) {
            ImGui::SetNextWindowClass(&auxiliary_window_class);
            draw_contact_result_panel(*fast_action_executor.contact_result(), contact_panel.open,
                                      contact_panel.focus_pending);
        }

        if (network_panel.open && fast_action_executor.network_report().has_value()) {
            ImGui::SetNextWindowClass(&auxiliary_window_class);
            draw_network_report_panel(*fast_action_executor.network_report(), network_panel.open,
                                      network_panel.focus_pending);
        }

        if (mermaid_panel.open && fast_action_executor.renderer_result().has_value() &&
            fast_action_executor.renderer_result()->active().kind == RendererResultKind::Mermaid) {
            const auto& rendered = fast_action_executor.renderer_result()->active();
            if (rendered.status == RendererResultStatus::Ready && rendered.output_path &&
                rendered.output_path->extension() == ".png" &&
                mermaid_result_texture_path != *rendered.output_path) {
                mermaid_result_texture_path = *rendered.output_path;
                mermaid_result_texture.clear();
                if (load_image_texture_file(*rendered.output_path, mermaid_result_texture)) {
                    mermaid_preview_state.mode = RendererPreviewPanelState::Mode::Preview;
                }
            }
            ImGui::SetNextWindowClass(&auxiliary_window_class);
            draw_mermaid_preview_panel(*fast_action_executor.renderer_result(), mermaid_preview_state,
                                       mermaid_panel.open, mermaid_panel.focus_pending,
                                       mermaid_result_texture.id, mermaid_result_texture.width,
                                       mermaid_result_texture.height,
                                       [platform = platform.get()](const std::filesystem::path& path) {
                                           return platform->open_path(path);
                                       });
        }

        if (qr_panel.open && fast_action_executor.renderer_result().has_value() &&
            fast_action_executor.renderer_result()->active().kind == RendererResultKind::Qr) {
            const auto& rendered = fast_action_executor.renderer_result()->active();
            if (rendered.status == RendererResultStatus::Ready && rendered.output_path &&
                qr_result_texture_path != *rendered.output_path) {
                qr_result_texture_path = *rendered.output_path;
                qr_result_texture.clear();
                if (load_image_texture_file(*rendered.output_path, qr_result_texture)) {
                    qr_preview_state.mode = RendererPreviewPanelState::Mode::Preview;
                }
            }
            ImGui::SetNextWindowClass(&auxiliary_window_class);
            draw_qr_preview_panel(*fast_action_executor.renderer_result(), qr_preview_state,
                                  qr_panel.open, qr_panel.focus_pending,
                                  qr_result_texture.id, qr_result_texture.width, qr_result_texture.height,
                                  [platform = platform.get()](const std::filesystem::path& path) {
                                      return platform->open_path(path);
                                  });
        }

        if (download_panel.open && !active_download_panel_job_id.empty()) {
            ImGui::SetNextWindowClass(&auxiliary_window_class);
            draw_download_progress_panel(download_manager, active_download_panel_job_id, download_panel.open,
                                         download_panel.focus_pending);
        }

        if (hash_panel.open && fast_action_executor.hash_result().has_value()) {
            ImGui::SetNextWindowClass(&auxiliary_window_class);
            draw_hash_result_panel(*fast_action_executor.hash_result(), hash_panel.open,
                                   hash_panel.focus_pending);
        }

        if (annotation_panel.open) {
            ImGui::SetNextWindowClass(&auxiliary_window_class);
            if (preview_texture.id != 0) {
                draw_image_annotation_panel(annotation_panel, preview_texture.id, preview_texture.width,
                                            preview_texture.height);
            } else {
                auto model = build_image_annotation_panel_model(annotation_panel);
                if (annotation_panel.focus_pending) {
                    ImGui::SetNextWindowFocus();
                    const auto* viewport = ImGui::GetMainViewport();
                    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
                }
                ImGui::SetNextWindowSize(ImVec2(model.viewport.initial_width, model.viewport.initial_height),
                                         ImGuiCond_FirstUseEver);
                if (ImGui::Begin(model.viewport.title.c_str(), &annotation_panel.open,
                                 ImGuiWindowFlags_NoSavedSettings)) {
                    if (annotation_panel.focus_pending) {
                        ImGui::SetWindowFocus();
                        annotation_panel.focus_pending = false;
                    }
                    copyable_text("Image preview unavailable; annotation export cannot be saved from this item.", true);
                    if (!annotation_panel.status_text.empty()) copyable_text(annotation_panel.status_text, true);
                }
                ImGui::End();
            }
        }

        if (image_preview.open && preview_texture.id != 0) {
            const auto image_title = with_icon(icon::kMedia, tr(ui_language,UiTextKey::ImagePreview)) + "###image-preview";
            if (begin_tool_window(image_title, &image_preview.open, ImVec2(760, 600), &image_preview.focus_pending)) {
                copyable_text(tr(ui_language,UiTextKey::Zoom) + " " + std::to_string(static_cast<int>(image_preview.zoom * 100.0F)) + "%"); ImGui::SameLine(); if(ImGui::Button(tr(ui_language,UiTextKey::Save).c_str())){const auto action=std::find_if(active_batch.catalog.actions.begin(),active_batch.catalog.actions.end(),[](const auto&a){return a.kind==ActionKind::SaveImageFile&&a.enabled;});if(action!=active_batch.catalog.actions.end())activated_action=action->id;}
                ImGui::SameLine();
                if(ImGui::Button(tr(ui_language,UiTextKey::CopyImage).c_str())&&!active_batch.request.snapshot.clipboard_items.empty()){
                    const auto& item=active_batch.request.snapshot.clipboard_items.front();const auto mime=item.mime_types.empty()?std::string{"image/png"}:item.mime_types.front();platform->publish_image(clipboard_store.read(item.ref),mime);
                }
                ImGui::SameLine();
                if(ImGui::Button(tr(ui_language,UiTextKey::CopyTemporaryPath).c_str())&&!active_batch.request.snapshot.clipboard_items.empty()){
                    const auto action=std::find_if(active_batch.catalog.actions.begin(),active_batch.catalog.actions.end(),[](const auto&a){return a.kind==ActionKind::CopyTemporaryImagePath&&a.enabled;});
                    if(action!=active_batch.catalog.actions.end()) activated_action=action->id;
                }
                ImGui::SameLine();
                if(ImGui::Button(tr(ui_language,UiTextKey::OpenTemporaryFile).c_str())&&!active_batch.request.snapshot.clipboard_items.empty()){
                    const auto& item=active_batch.request.snapshot.clipboard_items.front();std::string extension=".bin";for(const auto& mime:item.mime_types){if(mime=="image/png")extension=".png";else if(mime=="image/jpeg"||mime=="image/jpg")extension=".jpg";else if(mime=="image/bmp")extension=".bmp";}
                    const auto output=std::filesystem::temp_directory_path()/("pasteit-preview-"+item.ref+extension);std::ofstream stream(output,std::ios::binary|std::ios::trunc);const auto bytes=clipboard_store.read(item.ref);stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));stream.close();if(stream){(void)path_history.observe_path(output,PathKind::File,"preview",current_time_ms());platform->open_path(output);execution_status=tr(ui_language,UiTextKey::OpenedTemporaryImage)+path_to_utf8_string(output);}
                }
                ImGui::BeginChild("image-scroll",ImVec2(0,0),true,ImGuiWindowFlags_HorizontalScrollbar);
                if(ImGui::IsWindowHovered())image_preview.set_zoom(image_preview.zoom+ImGui::GetIO().MouseWheel*0.1F);
                const auto available=ImGui::GetContentRegionAvail();const float fit=std::min({1.0F,available.x/static_cast<float>(preview_texture.width),available.y/static_cast<float>(preview_texture.height)});
                ImGui::Image((ImTextureID)(intptr_t)preview_texture.id,ImVec2(preview_texture.width*fit*image_preview.zoom,preview_texture.height*fit*image_preview.zoom));ImGui::EndChild();
            }ImGui::End();
        }

        for (auto& job : generation_jobs) {
            if (!job->panel.open) continue;
            const auto result_rows = std::clamp<std::size_t>(multiline_editor_row_count(job->panel.editable_text), 4, 18);
            const float result_height = job->panel.running ? 240.0F :
                130.0F + static_cast<float>(result_rows) * ImGui::GetTextLineHeightWithSpacing();
            const auto* result_begin = job->panel.editable_text.data();
            const auto* result_end = result_begin + std::min<std::size_t>(job->panel.editable_text.size(), 2048);
            const float result_width = job->panel.running ? 460.0F :
                std::clamp(100.0F + ImGui::CalcTextSize(result_begin, result_end).x, 460.0F, 720.0F);
            const auto title = with_icon(icon::kAi, tr(ui_language,UiTextKey::AiResult)) + "###" + job->panel.request_id;
            if (begin_tool_window(title, &job->panel.open, ImVec2(result_width, result_height), &job->focus_pending)) {
                const auto& pal = palette();
                if (job->panel.running) {
                    // Reasoning models think before answering; say so instead of a bare spinner.
                    const bool thinking = job->panel.editable_text.empty() && job->reasoning_chars > 0;
                    const auto status = thinking ? tr(ui_language, UiTextKey::ModelThinking) + " (" +
                                                       std::to_string(job->reasoning_chars) + ")"
                                                 : tr(ui_language, UiTextKey::Generating);
                    ImGui::TextColored(pal.warning, "%s", status.c_str());
                }
                if (!job->panel.error.empty()) {
                    ImGui::PushTextWrapPos(0.0F);
                    ImGui::TextColored(pal.danger, "%s", job->panel.error.c_str());
                    ImGui::PopTextWrapPos();
                }
                // Read-only while streaming, so edits are not overwritten by the next chunk.
                input_text_string("##ai-result", job->panel.editable_text, true,
                                  job->panel.running ? ImGuiInputTextFlags_ReadOnly : 0, -footer_height());
                const bool ready = !job->panel.running && !job->panel.editable_text.empty();
                const int clicked = footer_buttons({{with_icon(icon::kRefresh, tr(ui_language,UiTextKey::Retry)), false, !job->pending.has_value()},
                                                    {tr(ui_language,UiTextKey::ReplaceClipboard), false, ready},
                                                    {with_icon(icon::kCopy, tr(ui_language,UiTextKey::CopyResult)), true, ready}});
                if (clicked == 0) {
                    if (auto retry = job->state.retry_request()) {
                        job->panel.running = true;
                        job->panel.error.clear();
                        job->focus_pending = true;
                        job->panel.editable_text.clear();
                        launch_streaming_job(*job, *retry);
                    }
                }
                if (clicked == 1) platform->publish_text(job->panel.editable_text);
                if (clicked == 2) platform->copy_text(job->panel.editable_text);
            }
            ImGui::End();
        }

        if (!popup_visible && !has_any_auxiliary_window()) {
            glfwSetWindowAttrib(window, GLFW_FLOATING, GLFW_FALSE);
            glfwHideWindow(window);
        }
        // Closing a dialog or result window hands OS focus to whatever app was
        // behind it; give it back to the popup so another action can be picked.
        const bool popup_focus_requested = refocus_popup;
        refocus_popup = false;
        const auto auxiliary_window_count = open_auxiliary_window_count();
        if (popup_visible && auxiliary_window_count < last_auxiliary_window_count) refocus_popup = true;
        last_auxiliary_window_count = auxiliary_window_count;

        ImGui::Render();
        int display_width = 0;
        int display_height = 0;
        glfwGetFramebufferSize(window, &display_width, &display_height);
        glViewport(0, 0, display_width, display_height);
        const auto clear = palette().background;
        glClearColor(clear.x, clear.y, clear.z, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            // Each new sub-window becomes owned by the popup so it never
            // opens (or falls) behind it.
            std::set<std::uint64_t> current_sub_windows;
            for (ImGuiViewport* viewport : ImGui::GetPlatformIO().Viewports) {
                if (viewport == ImGui::GetMainViewport() || viewport->PlatformHandle == nullptr) continue;
                auto* native = static_cast<GLFWwindow*>(viewport->PlatformHandle);
#if defined(_WIN32)
                const auto native_id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(glfwGetWin32Window(native)));
#else
                const auto native_id = static_cast<std::uint64_t>(glfwGetX11Window(native));
#endif
                if (native_id == 0) continue;
                current_sub_windows.insert(native_id);
                if (!owned_sub_windows.contains(native_id)) platform->keep_above_popup(native_id);
            }
            owned_sub_windows = std::move(current_sub_windows);
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_context);
        }
        glfwSwapBuffers(window);
        if (refocus_popup && !popup_focus_requested) glfwFocusWindow(window);
        if (activated_action.has_value()) {
            // A stale clipboard ref or unreadable blob must not take the app down.
            try {
                execute_selected(*activated_action, std::move(activated_prompt_variables));
            } catch (const std::exception& error) {
                execution_status = std::string{"Action failed: "} + error.what();
            }
        }
    }

    std::string settings_save_error;
    if (!save_settings_on_exit(settings_store, settings, settings_draft, settings_save_error)) {
        std::cerr << "Could not save all settings on exit: " << settings_save_error << '\n';
    }
    preview_texture.clear();
    history_texture.clear();
    pending_decision.wait_all();
    for (auto& job : generation_jobs) if (job->pending.has_value()) job->pending->wait();
    if (pending_provider_test.has_value()) pending_provider_test->wait();
    if (pending_model_list.has_value()) pending_model_list->wait();
    std::string path_save_error;
    (void)path_history_store.save(path_history, path_save_error);
    const auto final_history = clipboard_history_store.save(
        clipboard_store.items_newest_first(50), clipboard_store.next_ref());
    if (final_history.success) {
        std::string cleanup_error;
        (void)clipboard_history_store.cleanup_orphan_blobs(final_history, cleanup_error);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
#else
    return 1;
#endif
}

}  // namespace pasteit
