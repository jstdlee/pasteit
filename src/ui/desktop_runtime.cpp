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
#include "ui/desktop_flow.hpp"
#include "ui/decision_future_slot.hpp"
#include "ui/ai_result_panel.hpp"
#include "ui/font_loader.hpp"
#include "ui/image_preview_panel.hpp"
#include "ui/clipboard_history_model.hpp"
#include "ui/clipboard_history_panel.hpp"
#include "ui/contact_result_panel.hpp"
#include "ui/download_progress_panel.hpp"
#include "ui/file_operation_confirmation_panel.hpp"
#include "ui/hash_result_panel.hpp"
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
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(PASTIT_HAS_DESKTOP_DEPS)
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

namespace pastit {
namespace {

#if defined(PASTIT_HAS_DESKTOP_DEPS)

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
    if (const char* explicit_path = std::getenv("PASTIT_ENV_FILE")) {
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
    bool focus_pending = true;
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
};

struct GraphDialog {
    bool open = false;
    bool focus_pending = true;
    std::string source;
    int type = 0;
    bool header = false;
    bool convert_dates = false;
    bool dirty = true;
    std::string save_path;
    std::string status;
    std::optional<GraphData> data;
    std::vector<std::byte> png;
    ImageTexture texture;
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

std::string action_row_text(const PopupRow& row) {
    std::ostringstream label;
    label << static_cast<int>(std::lround(row.probability * 100.0)) << "%  " << row.label;
    if (!row.target_path.empty()) {
        label << "\n      " << row.target_path;
    }
    return label.str();
}

#endif

}  // namespace

int run_desktop_runtime() {
#if defined(PASTIT_HAS_DESKTOP_DEPS)
    load_desktop_environment();
#if defined(_WIN32)
    WindowsSingleInstance single_instance;
#else
    LinuxSingleInstance single_instance;
#endif
    if (!single_instance.acquired()) {
#if !defined(_WIN32)
        if (single_instance.request_show_existing_instance()) return 0;
#endif
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
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(720, 420, "PasteIt", nullptr, nullptr);
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
    const auto font_path = first_existing_font(platform->preferred_ui_fonts());
    if (!font_path.empty()) {
        const auto font_filename =
#if defined(_WIN32)
            path_to_utf8(font_path);
#else
            font_path.string();
#endif
        ImGui::GetIO().Fonts->AddFontFromFileTTF(font_filename.c_str(), 17.0F, nullptr,
                                                 ImGui::GetIO().Fonts->GetGlyphRangesChineseFull());
    }
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0F;
    style.FrameRounding = 7.0F;
    style.WindowPadding = ImVec2(18.0F, 16.0F);
    style.ItemSpacing = ImVec2(8.0F, 9.0F);
    configure_independent_viewports(ImGui::GetIO(), style);
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
    if (!platform->register_global_shortcut()) {
        std::cerr << "Could not register global Ctrl+Alt+F; set PASTIT_SHOW_ON_START=1 to open manually.\n";
    }

    DjevClient djev_client(settings.djev.endpoint, settings.djev.model_id, DjevClient::kDefaultTimeout, {},
                           settings.djev.api_key);
    OpenAiCompatibleClient llm_client;
    DecisionFutureSlot pending_decision;
    std::vector<std::unique_ptr<GenerationJob>> generation_jobs;
    PromptParameterDialog prompt_parameter_dialog;
    CustomPromptDialog custom_prompt_dialog;
    GraphDialog graph_dialog;
    std::map<std::string, std::map<std::string, std::string>> prompt_parameter_memory;
    std::optional<std::future<std::string>> pending_provider_test;
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
    std::string decision_status = "Press Ctrl+Alt+F";
    std::string execution_status;
    std::optional<ExecutionResult> last_execution;
    int selected_row = 0;
    std::uint64_t request_counter = 0;
    bool popup_visible = false;
    bool show_settings = false;
    AppSettings settings_draft = settings;
    std::string settings_status = settings_load.warning;
    if (!clipboard_history_load.warning.empty()) {
        if (!settings_status.empty()) settings_status += "\n";
        settings_status += clipboard_history_load.warning;
    }
    const bool diagnostics = std::getenv("PASTIT_DIAGNOSTICS") != nullptr;
    auto last_clipboard_poll = std::chrono::steady_clock::time_point{};
    if(!platform->set_popup_opacity(settings.window_opacity))glfwSetWindowOpacity(window, settings.window_opacity);

    const auto has_fast_action_result_panel = [&] {
        return contact_panel.open || network_panel.open || mermaid_panel.open || qr_panel.open ||
               download_panel.open || hash_panel.open || annotation_panel.open;
    };

    const auto has_any_auxiliary_window = [&] {
        return show_settings || image_preview.open || file_confirmation.has_value() ||
               prompt_parameter_dialog.open || custom_prompt_dialog.open || graph_dialog.open ||
               has_fast_action_result_panel() ||
               std::any_of(generation_jobs.begin(), generation_jobs.end(), [](const auto& job) {
                   return job->panel.open;
               });
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
        const auto saved = clipboard_history_store.save(clipboard_store.items_newest_first(50), clipboard_store.next_ref());
        if (saved.success) {
            std::string cleanup_error;
            (void)clipboard_history_store.cleanup_orphan_blobs(saved, cleanup_error);
            clipboard_store.restore(saved.retained_items, saved.next_ref);
        } else if (diagnostics) {
            std::cerr << "PasteIt clipboard history save failed: " << saved.error << '\n';
        }
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
        input.direct_send_available = std::getenv("PASTIT_SENDMAIL") != nullptr;
        input.prompt_templates = settings.prompt_templates;
        input.general_llm = settings.general_llm;
        input.default_image_directory = settings.default_image_directory;
        input.default_text_directory = settings.default_text_directory;
        input.downloads = settings.downloads;
        input.hash = settings.hash;
        input.date_time = settings.date_time;
        input.action_preferences = settings.action_preferences;
        if (focus.current_directory.has_value()) {
            input.focused_current_directory = *focus.current_directory;
        }
        return build_desktop_decision(input);
    };

    const auto open_popup = [&] {
        capture_clipboard(true);
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
            decision_status = "Clipboard is empty or unsupported";
            pending_decision.clear();
        } else {
            decision_status = "Djev is ranking actions…";
            const auto request = active_batch.request;
            pending_decision.replace(std::async(std::launch::async, [client = djev_client, request] {
                return client.decide(request);
            }));
        }
        glfwSetWindowAttrib(window, GLFW_FLOATING, GLFW_TRUE);
        glfwRestoreWindow(window);
        glfwShowWindow(window);
        glfwPollEvents();
        position_popup(window);
        glfwFocusWindow(window);
        popup_visible = true;
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
            ("pastit-annotation-" + item->ref + image_extension_for(*item));
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
                mermaid_preview_state.destination = path_to_utf8_string(std::filesystem::temp_directory_path() /
                    ("pastit-" + renderer.action_id + ".mmd"));
                break;
            case RendererResultKind::Qr:
                qr_panel.bring_to_front();
                qr_preview_state.mode = RendererPreviewPanelState::Mode::Source;
                qr_preview_state.destination = path_to_utf8_string(std::filesystem::temp_directory_path() /
                    ("pastit-" + renderer.action_id + ".txt"));
                break;
            case RendererResultKind::Annotation: {
                annotation_panel.open = true;
                annotation_panel.focus_pending = true;
                annotation_panel.status_text.clear();
                annotation_panel.export_directory = settings.annotation.save_directory;
                annotation_panel.export_format = settings.annotation.export_format;
                const auto source_ref = renderer.payload.empty() ? renderer.source : renderer.payload;
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
            .direct_send_configured = std::getenv("PASTIT_SENDMAIL") != nullptr,
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
            const char* adapter = std::getenv("PASTIT_SENDMAIL");
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
        std::string path_save_error;
        (void)path_history_store.save(path_history, path_save_error);
        if (!publish_clipboard_result(result)) {
            execution_status = "Action completed, but publishing the clipboard result failed";
        }
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
            // Keep the key semantic so regenerated action ids do not erase the
            // user's local ranking bias.
            record_action_preference(settings.action_preferences, *action, active_batch.ranking_context);
            std::string preference_save_error;
            if (!settings_store.save(settings, preference_save_error) && diagnostics) {
                std::cerr << "PasteIt action preference save failed: " << preference_save_error << '\n';
            }
        }
        if (action->kind == ActionKind::Graph) {
            graph_dialog.open = true;
            graph_dialog.focus_pending = true;
            graph_dialog.source = clipboard_store.read_text(action->source_ref);
            graph_dialog.type = 0;
            graph_dialog.header = !parse_graph_data(graph_dialog.source, false, false).has_value();
            graph_dialog.convert_dates = false;
            graph_dialog.dirty = true;
            graph_dialog.status.clear();
            const auto directory = settings.default_image_directory.empty()
                ? std::filesystem::current_path() : settings.default_image_directory;
            graph_dialog.save_path = path_to_utf8_string(directory / "graph.png");
            return;
        }
        if (action->kind == ActionKind::CustomPrompt) {
            custom_prompt_dialog.open = true;
            custom_prompt_dialog.focus_pending = true;
            custom_prompt_dialog.action = *action;
            custom_prompt_dialog.prompt.clear();
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
                const auto remembered = prompt_parameter_memory.find(prompt.id);
                for (const auto& name : variables) {
                    if (remembered != prompt_parameter_memory.end()) {
                        const auto value = remembered->second.find(name);
                        if (value != remembered->second.end()) {
                            prompt_parameter_dialog.values[name] = value->second;
                            continue;
                        }
                    }
                    if (name == "source_language") prompt_parameter_dialog.values[name] = "auto";
                    else if (name == "target_language") prompt_parameter_dialog.values[name] = "Simplified Chinese";
                    else prompt_parameter_dialog.values[name] = {};
                }
                return;
            }
            const auto expanded = expand_prompt(prompt, source_text, prompt_variables);
            TextGenerationRequest request{
                .request_id = active_batch.request.request_id + "_ai_" + std::to_string(++request_counter),
                .endpoint = action->parameters.at("llm_endpoint"),
                .api_key = active_batch.general_llm.api_key,
                .model_id = action->parameters.at("llm_model_id"),
                .system_message = expanded.system_message,
                .user_message = expanded.user_message,
                .temperature = prompt.temperature,
            };
            auto job = std::make_unique<GenerationJob>();
            job->kind = GenerationJob::Kind::TextPrompt;
            job->action = *action;
            job->source_text = source_text;
            job->state.start(action->id, source_text, request);
            job->panel = {
                .open = true,
                .running = true,
                .request_id = request.request_id,
                .editable_text = {},
                .error = {},
            };
            job->pending.emplace(std::async(std::launch::async, [&llm_client, request] {
                return llm_client.generate(request);
            }));
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

    bool show_on_start = std::getenv("PASTIT_SHOW_ON_START") != nullptr;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        platform->process_events();
        capture_clipboard(false);
        bool show_requested = false;
#if !defined(_WIN32)
        show_requested = single_instance.take_show_request();
#endif
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
            active_batch.ranking_context.djev_proposed_kind.reset();
            if (const auto proposed = active_batch.catalog.find(response.choice); proposed.has_value()) {
                active_batch.ranking_context.djev_proposed_kind = proposed->kind;
            }
            const auto prepared = prepare_popup_decision(active_batch.request, response, active_batch.catalog,
                                                         current.request.snapshot, settings.action_preferences,
                                                         active_batch.ranking_context);
            if (prepared.status == DecisionSessionStatus::Ready) {
                popup_model = build_popup_model(active_batch.request.snapshot, prepared.ranked);
                if (popup_visible) {
                    const int rows_height = static_cast<int>(popup_model.rows.size()) * 34;
                    const bool image_source = !active_batch.request.snapshot.clipboard_items.empty() &&
                        active_batch.request.snapshot.clipboard_items.front().kind == ContentKind::Image;
                    glfwSetWindowSize(window, 720, std::clamp(190 + rows_height + (image_source ? 105 : 35), 340, 575));
                    position_popup(window);
                }
                decision_status = prepared.message == "ranked" ? "Ranked by local Djev" : prepared.message;
            } else {
                decision_status = prepared.message.empty() ? "Djev did not return executable actions" : prepared.message;
            }
        }

        for (auto& job : generation_jobs) {
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
                (void)job->state.complete(result.request_id, result.content);
                job->panel.editable_text = result.content;
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

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        const char* locale_value=std::getenv("LANG");
        const auto ui_language=resolve_language(settings.language,locale_value==nullptr?std::string_view{}:std::string_view{locale_value});
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
        ImGui::Begin("PasteItPopup", nullptr, flags);
            copyable_text("PasteIt");
        ImGui::SameLine(0.0F, 10.0F);
            copyable_text(decision_status, true);
        if (ImGui::BeginTabBar("main-tabs")) {
        if (ImGui::BeginTabItem(tr(ui_language,UiTextKey::SmartActions).c_str())) {
        const auto smart_body_height = std::max(0.0F, ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing());
        ImGui::BeginChild("smart-actions-body", ImVec2(0.0F, smart_body_height), false);
        if (preview_texture.id != 0) {
            constexpr float max_width = 190.0F;
            constexpr float max_height = 120.0F;
            const float scale = std::min(max_width / static_cast<float>(preview_texture.width),
                                         max_height / static_cast<float>(preview_texture.height));
            if (ImGui::ImageButton("clipboard-thumbnail", (ImTextureID)(intptr_t)preview_texture.id,
                                   ImVec2(preview_texture.width * scale, preview_texture.height * scale))) {
                image_preview.open = true;
                image_preview.focus_pending = true;
            }
        } else if (!active_batch.request.snapshot.clipboard_items.empty()) {
                copyable_text(active_batch.request.snapshot.clipboard_items.front().preview, true);
        } else {
                copyable_text(tr(ui_language,UiTextKey::NoClipboard));
        }
        ImGui::Separator();

        const auto graph_choice = std::find_if(active_batch.catalog.actions.begin(), active_batch.catalog.actions.end(),
            [](const ActionInstance& action) { return action.kind == ActionKind::Graph && action.enabled; });
        if (graph_choice != active_batch.catalog.actions.end()) {
            if (ImGui::SmallButton(tr(ui_language, UiTextKey::GraphData).c_str())) activated_action = graph_choice->id;
            ImGui::SameLine();
        }
        const auto annotation_choice = std::find_if(active_batch.catalog.actions.begin(), active_batch.catalog.actions.end(),
            [](const ActionInstance& action) { return action.kind == ActionKind::AnnotateImage && action.enabled; });
        if (annotation_choice != active_batch.catalog.actions.end()) {
            if (ImGui::SmallButton(tr(ui_language, UiTextKey::AnnotateImage).c_str())) {
                activated_action = annotation_choice->id;
            }
            ImGui::SameLine();
        }
        const auto custom_choice = std::find_if(active_batch.catalog.actions.begin(), active_batch.catalog.actions.end(),
            [](const ActionInstance& action) { return action.kind == ActionKind::CustomPrompt && action.enabled; });
        if (custom_choice != active_batch.catalog.actions.end() &&
            ImGui::SmallButton(tr(ui_language, UiTextKey::CustomPrompt).c_str())) {
            activated_action = custom_choice->id;
        }

        if (popup_model.rows.empty()) {
            copyable_text(pending_decision.pending() ? tr(ui_language,UiTextKey::WaitingDjev) : tr(ui_language,UiTextKey::NoRankedActions));
        } else {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                selected_row = std::max(0, selected_row - 1);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                selected_row = std::min(static_cast<int>(popup_model.rows.size()) - 1, selected_row + 1);
            }
            for (std::size_t index = 0; index < popup_model.rows.size(); ++index) {
                const auto& row = popup_model.rows[index];
                ImGui::PushID(row.action_id.c_str());
                const auto selectable_flags = row.enabled ? ImGuiSelectableFlags_AllowDoubleClick : ImGuiSelectableFlags_Disabled;
                if (ImGui::Selectable(action_row_text(row).c_str(), static_cast<int>(index) == selected_row,
                                      selectable_flags, ImVec2(0.0F, row.target_path.empty() ? 30.0F : 48.0F))) {
                    selected_row = static_cast<int>(index);
                    if (row.enabled) {
                        activated_action = row.action_id;
                    }
                }
                ImGui::PopID();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) && selected_row >= 0 &&
                selected_row < static_cast<int>(popup_model.rows.size()) && popup_model.rows[selected_row].enabled) {
                activated_action = popup_model.rows[selected_row].action_id;
            }
        }
        if (!execution_status.empty()) {
            ImGui::Separator();
                copyable_text(execution_status, true);
            if (last_execution.has_value() && !last_execution->output_paths.empty()) {
                const auto& output = last_execution->output_paths.front();
                if (ImGui::SmallButton(tr(ui_language,UiTextKey::CopyPath).c_str())) platform->copy_text(path_to_utf8_string(output));
                ImGui::SameLine();
                if (ImGui::SmallButton(tr(ui_language,UiTextKey::OpenResult).c_str())) platform->open_path(output);
                ImGui::SameLine();
                if (ImGui::SmallButton(tr(ui_language,UiTextKey::OpenFolder).c_str())) platform->open_path(output.parent_path());
            }
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr(ui_language,UiTextKey::RecentPaths).c_str())) {
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
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr(ui_language,UiTextKey::ClipboardHistory).c_str())) {
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
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr(ui_language,UiTextKey::Settings).c_str())) {
            const auto settings_body_height = std::max(0.0F, ImGui::GetContentRegionAvail().y);
            ImGui::BeginChild("settings-tab-body", ImVec2(0.0F, settings_body_height), false);
            if (ImGui::CollapsingHeader(tr(ui_language,UiTextKey::General).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                int language = static_cast<int>(settings_draft.language);
                const char* languages[] = {"System", "English", "简体中文"};
                if (ImGui::Combo(tr(ui_language,UiTextKey::Language).c_str(), &language, languages, 3)) {
                    settings_draft.language = static_cast<UiLanguage>(language);
                }
                if (ImGui::SliderFloat(tr(ui_language,UiTextKey::Opacity).c_str(), &settings_draft.window_opacity, 0.55F, 1.0F) &&
                    !platform->set_popup_opacity(settings_draft.window_opacity)) glfwSetWindowOpacity(window, settings_draft.window_opacity);
                auto image_dir = path_to_utf8_string(settings_draft.default_image_directory);
                if (input_text_string(tr(ui_language,UiTextKey::DefaultImageDirectory).c_str(), image_dir)) settings_draft.default_image_directory = path_from_utf8_string(image_dir);
                auto text_dir = path_to_utf8_string(settings_draft.default_text_directory);
                if (input_text_string(tr(ui_language,UiTextKey::DefaultTextDirectory).c_str(), text_dir)) settings_draft.default_text_directory = path_from_utf8_string(text_dir);
                if (ImGui::Button(tr(ui_language,UiTextKey::KeepLatest10).c_str())) rewrite_history(10, ui_language);
                ImGui::SameLine();
                if (ImGui::Button(tr(ui_language,UiTextKey::DeleteAllHistory).c_str())) rewrite_history(0, ui_language);
            }
            if (ImGui::CollapsingHeader(tr(ui_language,UiTextKey::Djev).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                input_text_string(tr(ui_language,UiTextKey::DjevEndpoint).c_str(), settings_draft.djev.endpoint);
                input_text_string(tr(ui_language,UiTextKey::DjevModel).c_str(), settings_draft.djev.model_id);
                input_text_string(tr(ui_language,UiTextKey::DjevApiKey).c_str(), settings_draft.djev.api_key, false, ImGuiInputTextFlags_Password);
                if (ImGui::Button(tr(ui_language,UiTextKey::TestDjev).c_str()) && !pending_provider_test) {
                    pending_provider_test_is_djev = true;
                    djev_test_status = "Testing…";
                    const auto provider = settings_draft.djev;
                    const auto language_copy = ui_language;
                    pending_provider_test.emplace(std::async(std::launch::async, [provider,language_copy] {
                        const auto result = DjevClient(provider.endpoint, provider.model_id, DjevClient::kDefaultTimeout, {}, provider.api_key).decide(provider_test_request());
                        return result.valid ? tr(language_copy,UiTextKey::DjevTestSucceeded) : tr(language_copy,UiTextKey::DjevTestFailed) + result.error;
                    }));
                }
                ImGui::SameLine();
                if (!djev_test_status.empty()) ImGui::TextUnformatted(djev_test_status.c_str());
            }
            if (ImGui::CollapsingHeader(tr(ui_language,UiTextKey::GeneralLlm).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                input_text_string(tr(ui_language,UiTextKey::LlmEndpoint).c_str(), settings_draft.general_llm.endpoint);
                input_text_string(tr(ui_language,UiTextKey::LlmModel).c_str(), settings_draft.general_llm.model_id);
                if (!pending_model_list.has_value() && model_list_endpoint != settings_draft.general_llm.endpoint) {
                    general_llm_models.clear();
                    model_list_status.clear();
                    model_list_endpoint.clear();
                }
                if (model_list_endpoint.empty() && !pending_model_list.has_value()) request_model_list();
                const auto model_preview = settings_draft.general_llm.model_id.empty()
                    ? tr(ui_language, UiTextKey::ModelList)
                    : settings_draft.general_llm.model_id;
                if (ImGui::BeginCombo((tr(ui_language,UiTextKey::ModelList)+"##llm-model-list").c_str(), model_preview.c_str())) {
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
                if (!model_list_status.empty()) copyable_text(tr(ui_language,UiTextKey::ModelListFailed) + model_list_status, true);
                input_text_string(tr(ui_language,UiTextKey::LlmApiKey).c_str(), settings_draft.general_llm.api_key, false, ImGuiInputTextFlags_Password);
                if (ImGui::Button(tr(ui_language,UiTextKey::TestGeneralLlm).c_str()) && !pending_provider_test) {
                    pending_provider_test_is_djev = false;
                    llm_test_status = "Testing…";
                    const auto provider = settings_draft.general_llm;
                    const auto language_copy = ui_language;
                    pending_provider_test.emplace(std::async(std::launch::async, [provider,language_copy] {
                        OpenAiCompatibleClient client;
                        const auto result = client.generate({.request_id="settings-test",.endpoint=provider.endpoint,.api_key=provider.api_key,.model_id=provider.model_id,.system_message="Return OK.",.user_message="OK",.temperature=0.0,.timeout=std::chrono::milliseconds{5000}});
                        return result.ok ? tr(language_copy,UiTextKey::GeneralLlmTestSucceeded) : tr(language_copy,UiTextKey::GeneralLlmTestFailed) + result.error;
                    }));
                }
                ImGui::SameLine();
                if (!llm_test_status.empty()) ImGui::TextUnformatted(llm_test_status.c_str());
            }
            if (ImGui::CollapsingHeader("Fast actions", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextUnformatted("Mermaid uses mmdc for an in-window image when available; offline HTML is the fallback.");
                if (ImGui::SmallButton("Mermaid homepage")) platform->open_uri("https://mermaid.js.org/");
                auto mermaid_cli = path_to_utf8_string(settings_draft.renderers.mermaid_cli_path);
                if (input_text_string("Mermaid CLI path (optional)", mermaid_cli)) {
                    settings_draft.renderers.mermaid_cli_path = path_from_utf8_string(mermaid_cli);
                }
                std::string mermaid_args;
                for (const auto& arg : settings_draft.renderers.mermaid_arguments) {
                    if (!mermaid_args.empty()) mermaid_args += ' ';
                    mermaid_args += arg;
                }
                if (input_text_string("Mermaid CLI arguments", mermaid_args)) {
                    std::istringstream args(mermaid_args);
                    settings_draft.renderers.mermaid_arguments.assign(
                        std::istream_iterator<std::string>{args}, std::istream_iterator<std::string>{});
                }
                ImGui::TextUnformatted("QR codes use the native Project Nayuki library.");
                if (ImGui::SmallButton("QR library homepage")) platform->open_uri("https://www.nayuki.io/page/qr-code-generator-library");
                input_text_string("QR error correction (L/M/Q/H)", settings_draft.renderers.qr_error_correction);
                ImGui::SliderInt("QR margin", &settings_draft.renderers.qr_margin, 0, 10);
                ImGui::SliderInt("QR scale", &settings_draft.renderers.qr_scale, 1, 64);

                auto download_dir = path_to_utf8_string(settings_draft.downloads.resume_directory);
                if (input_text_string("Download resume directory", download_dir)) {
                    settings_draft.downloads.resume_directory = path_from_utf8_string(download_dir);
                }
                ImGui::Checkbox("Keep .part files after cancel", &settings_draft.downloads.keep_part_files);

                auto terminal_command = join_argv(settings_draft.terminal.command);
                if (input_text_string("Terminal command", terminal_command)) {
                    settings_draft.terminal.command = split_argv_field(terminal_command);
                }
                input_text_string("Terminal profile (stored; launcher support pending)", settings_draft.terminal.profile);

                bool sha256 = std::find(settings_draft.hash.default_algorithms.begin(),
                                         settings_draft.hash.default_algorithms.end(), "sha256") !=
                              settings_draft.hash.default_algorithms.end();
                bool sha512 = std::find(settings_draft.hash.default_algorithms.begin(),
                                         settings_draft.hash.default_algorithms.end(), "sha512") !=
                              settings_draft.hash.default_algorithms.end();
                const bool hash_changed_256 = ImGui::Checkbox("Show SHA-256 hash action", &sha256);
                const bool hash_changed_512 = ImGui::Checkbox("Show SHA-512 hash action", &sha512);
                if (hash_changed_256 || hash_changed_512) {
                    settings_draft.hash.default_algorithms.clear();
                    if (sha256) settings_draft.hash.default_algorithms.push_back("sha256");
                    if (sha512) settings_draft.hash.default_algorithms.push_back("sha512");
                }

                input_text_string("Source time zone", settings_draft.date_time.source_zone);
                input_text_string("Target time zone", settings_draft.date_time.target_zone);
                bool twenty_four_hour = settings_draft.date_time.use_24_hour_clock;
                ImGui::BeginDisabled();
                ImGui::Checkbox("24-hour display preference (stored; ISO output currently used)", &twenty_four_hour);
                ImGui::EndDisabled();

                auto annotation_dir = path_to_utf8_string(settings_draft.annotation.save_directory);
                if (input_text_string("Annotation save directory", annotation_dir)) {
                    settings_draft.annotation.save_directory = path_from_utf8_string(annotation_dir);
                }
                settings_draft.annotation.export_format = "svg";
                auto annotation_format = settings_draft.annotation.export_format;
                ImGui::BeginDisabled();
                input_text_string("Annotation export format", annotation_format);
                ImGui::EndDisabled();
                copyable_text("Annotation export is SVG-only in this build; PNG/JPG preferences are normalized to SVG.", true);
            }
            if (ImGui::CollapsingHeader(tr(ui_language,UiTextKey::PromptTemplates).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                PromptTemplateService service(settings_draft.prompt_templates);
                if (prompt_panel_model.modal == PromptTemplateModal::None) prompt_panel_model = build_prompt_templates_panel_model(settings_draft.prompt_templates);
                std::string view_id, edit_id, duplicate_id, delete_id;
                if (ImGui::BeginTable("settings-prompt-templates", 5, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn(tr(ui_language,UiTextKey::Enabled).c_str(), ImGuiTableColumnFlags_WidthFixed, 70);
                    ImGui::TableSetupColumn(tr(ui_language,UiTextKey::Name).c_str(), ImGuiTableColumnFlags_WidthStretch, 3);
                    ImGui::TableSetupColumn(tr(ui_language,UiTextKey::Temperature).c_str(), ImGuiTableColumnFlags_WidthFixed, 90);
                    ImGui::TableSetupColumn((tr(ui_language,UiTextKey::BuiltIn)+"/"+tr(ui_language,UiTextKey::Custom)).c_str(), ImGuiTableColumnFlags_WidthFixed, 110);
                    ImGui::TableSetupColumn(tr(ui_language,UiTextKey::Actions).c_str(), ImGuiTableColumnFlags_WidthFixed, 190);
                    ImGui::TableHeadersRow();
                    for (const auto& row : prompt_panel_model.rows) {
                        ImGui::PushID(("settings-"+row.id).c_str());
                        ImGui::TableNextRow(); ImGui::TableNextColumn();
                        bool enabled = row.enabled;
                        if (ImGui::Checkbox("##enabled", &enabled)) { std::string error; service.set_enabled(row.id, enabled, error); }
                        ImGui::TableNextColumn(); copyable_text(row.name);
                        ImGui::TableNextColumn(); copyable_text(std::to_string(row.temperature));
                        ImGui::TableNextColumn(); copyable_text(row.built_in ? tr(ui_language,UiTextKey::BuiltIn) : tr(ui_language,UiTextKey::Custom));
                        ImGui::TableNextColumn();
                        if (ImGui::SmallButton((tr(ui_language,UiTextKey::View)+"##view").c_str())) view_id = row.id; ImGui::SameLine();
                        if (ImGui::SmallButton((tr(ui_language,UiTextKey::Edit)+"##edit").c_str())) edit_id = row.id; ImGui::SameLine();
                        if (ImGui::SmallButton((tr(ui_language,UiTextKey::Duplicate)+"##duplicate").c_str())) duplicate_id = row.id; ImGui::SameLine();
                        if (ImGui::SmallButton((tr(ui_language,UiTextKey::Delete)+"##delete").c_str())) delete_id = row.id;
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                if (!view_id.empty()) view_prompt_template(prompt_panel_model, view_id);
                if (!edit_id.empty()) begin_prompt_template_edit(prompt_panel_model, edit_id);
                if (!duplicate_id.empty()) duplicate_prompt_template(prompt_panel_model, service, duplicate_id);
                if (!delete_id.empty()) begin_prompt_template_delete(prompt_panel_model, delete_id);
                if (ImGui::Button(tr(ui_language,UiTextKey::NewTemplate).c_str())) { std::string error; if (const auto created = service.create("New Prompt", "Transform {text}", 0.2, error)) { prompt_panel_model = build_prompt_templates_panel_model(settings_draft.prompt_templates); begin_prompt_template_edit(prompt_panel_model, created->id); } }
                ImGui::SameLine();
                if (ImGui::Button(tr(ui_language,UiTextKey::RestoreDefaults).c_str())) { service.restore_defaults(); prompt_panel_model = build_prompt_templates_panel_model(settings_draft.prompt_templates); }
                const auto prompt_detail_title = tr(ui_language,UiTextKey::Details) + "##settings-prompt-detail";
                const auto prompt_edit_title = tr(ui_language,UiTextKey::Edit) + "##settings-prompt-edit";
                const auto prompt_delete_title = tr(ui_language,UiTextKey::Delete) + "##settings-prompt-delete";
                if (prompt_panel_model.modal == PromptTemplateModal::View && prompt_panel_model.draft) {
                    bool detail_open = true; ImGui::SetNextWindowClass(&auxiliary_window_class); ImGui::SetNextWindowSize(ImVec2(520,320),ImGuiCond_FirstUseEver);
                    if (ImGui::Begin(prompt_detail_title.c_str(), &detail_open)) {
                        copyable_text(prompt_panel_model.draft->name); ImGui::Separator();
                        ImGui::BeginChild("settings-prompt-detail-body", ImVec2(0.0F, 0.0F), true);
                        copyable_text(prompt_panel_model.draft->system_prompt, true); ImGui::EndChild();
                    } ImGui::End();
                    if (!detail_open) cancel_prompt_template_modal(prompt_panel_model);
                }
                if (prompt_panel_model.modal == PromptTemplateModal::Edit && prompt_panel_model.draft) {
                    bool edit_open = true; ImGui::SetNextWindowClass(&auxiliary_window_class); ImGui::SetNextWindowSize(ImVec2(600,440),ImGuiCond_FirstUseEver);
                    if (ImGui::Begin(prompt_edit_title.c_str(), &edit_open)) {
                        input_text_string(tr(ui_language,UiTextKey::TemplateName).c_str(), prompt_panel_model.draft->name);
                        const float edit_footer_height = 3.0F * ImGui::GetFrameHeightWithSpacing();
                        input_text_string(tr(ui_language,UiTextKey::SystemPrompt).c_str(),
                                          prompt_panel_model.draft->system_prompt, true, 0, -edit_footer_height);
                        ImGui::Checkbox(tr(ui_language,UiTextKey::Enabled).c_str(), &prompt_panel_model.draft->enabled);
                        float temperature = static_cast<float>(prompt_panel_model.draft->temperature);
                        if (ImGui::SliderFloat(tr(ui_language,UiTextKey::Temperature).c_str(), &temperature, 0.0F, 2.0F)) prompt_panel_model.draft->temperature = temperature;
                        if (ImGui::Button(tr(ui_language,UiTextKey::Save).c_str())) { const auto result = save_prompt_template_edit(prompt_panel_model, service); if (result.error.empty()) edit_open = false; }
                        ImGui::SameLine(); if (ImGui::Button(tr(ui_language,UiTextKey::Cancel).c_str())) edit_open = false;
                    } ImGui::End();
                    if (!edit_open && prompt_panel_model.modal != PromptTemplateModal::None) cancel_prompt_template_modal(prompt_panel_model);
                }
                if (prompt_panel_model.modal == PromptTemplateModal::Delete) {
                    bool delete_open = true; ImGui::SetNextWindowClass(&auxiliary_window_class); ImGui::SetNextWindowSize(ImVec2(420,180),ImGuiCond_FirstUseEver);
                    if (ImGui::Begin(prompt_delete_title.c_str(), &delete_open)) {
                        copyable_text(tr(ui_language,UiTextKey::DeleteTemplateText));
                        if (ImGui::Button(tr(ui_language,UiTextKey::ConfirmDelete).c_str())) { confirm_prompt_template_delete(prompt_panel_model, service); delete_open = false; }
                        ImGui::SameLine(); if (ImGui::Button(tr(ui_language,UiTextKey::Keep).c_str())) delete_open = false;
                    } ImGui::End();
                    if (!delete_open && prompt_panel_model.modal != PromptTemplateModal::None) cancel_prompt_template_modal(prompt_panel_model);
                }
            }
            if (!settings_status.empty()) copyable_text(settings_status, true);
            if (ImGui::Button(tr(ui_language,UiTextKey::Save).c_str())) {
                AppSettings candidate = settings; std::string error;
                if (save_settings_draft(settings_store, settings, settings_draft, candidate, error)) {
                    const bool llm_endpoint_changed = settings.general_llm.endpoint != candidate.general_llm.endpoint;
                    const bool djev_changed = settings.djev.endpoint != candidate.djev.endpoint ||
                                              settings.djev.model_id != candidate.djev.model_id ||
                                              settings.djev.api_key != candidate.djev.api_key;
                    settings = candidate; settings_draft = settings; settings_status = tr(ui_language,UiTextKey::Saved);
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
                } else settings_status = error;
            }
            ImGui::SameLine();
            if (ImGui::Button(tr(ui_language,UiTextKey::ResetProviders).c_str())) { const auto defaults = default_settings(); settings_draft.djev = defaults.djev; settings_draft.general_llm = defaults.general_llm; }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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
            if (prompt_parameter_dialog.focus_pending) {
                ImGui::SetNextWindowFocus();
                const auto* viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
            }
            ImGui::SetNextWindowClass(&auxiliary_window_class);
            const auto parameter_rows = std::clamp<std::size_t>(prompt_parameter_dialog.names.size(), 1, 10);
            const auto parameter_height = 125.0F + static_cast<float>(parameter_rows) * ImGui::GetFrameHeightWithSpacing();
            ImGui::SetNextWindowSize(ImVec2(520.0F, parameter_height), ImGuiCond_Appearing);
            const auto title = "Prompt Parameters: " + prompt_parameter_dialog.template_name + "##prompt-parameters";
            if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
                if (prompt_parameter_dialog.focus_pending) {
                    ImGui::SetWindowFocus();
                    prompt_parameter_dialog.focus_pending = false;
                }
                copyable_text("Enter values used by this prompt.");
                ImGui::BeginChild("prompt-parameters-body", ImVec2(0.0F, -ImGui::GetFrameHeightWithSpacing() * 1.5F), true);
                for (const auto& name : prompt_parameter_dialog.names) {
                    auto& value = prompt_parameter_dialog.values[name];
                    input_text_string(name.c_str(), value);
                }
                ImGui::EndChild();
                if (ImGui::Button(tr(ui_language, UiTextKey::Confirm).c_str())) {
                    PromptVariables values;
                    values.values = prompt_parameter_dialog.values;
                    prompt_parameter_memory[prompt_parameter_dialog.template_id] = values.values;
                    activated_prompt_variables = std::move(values);
                    activated_action = prompt_parameter_dialog.action_id;
                    prompt_parameter_dialog.open = false;
                }
                ImGui::SameLine();
                if (ImGui::Button(tr(ui_language, UiTextKey::Cancel).c_str())) {
                    prompt_parameter_dialog.open = false;
                }
            }
            ImGui::End();
            if (!open) prompt_parameter_dialog.open = false;
        }

        if (custom_prompt_dialog.open) {
            if (custom_prompt_dialog.focus_pending) {
                ImGui::SetNextWindowFocus();
                ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
            }
            ImGui::SetNextWindowClass(&auxiliary_window_class);
            ImGui::SetNextWindowSize(ImVec2(560, 280), ImGuiCond_Appearing);
            const auto custom_prompt_title = tr(ui_language, UiTextKey::CustomPrompt) + "##custom-prompt";
            if (ImGui::Begin(custom_prompt_title.c_str(), &custom_prompt_dialog.open,
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
                if (custom_prompt_dialog.focus_pending) {
                    ImGui::SetWindowFocus();
                    custom_prompt_dialog.focus_pending = false;
                }
                copyable_text(tr(ui_language, UiTextKey::PromptInstructions));
                input_text_string("##custom-prompt-input", custom_prompt_dialog.prompt, true, 0, 150.0F);
                const bool can_run = !trim(custom_prompt_dialog.prompt).empty();
                if (!can_run) ImGui::BeginDisabled();
                if (ImGui::Button(tr(ui_language, UiTextKey::Generate).c_str())) {
                    if (active_batch.general_llm.endpoint.empty() || active_batch.general_llm.model_id.empty()) {
                        execution_status = "General LLM provider is not configured";
                    } else {
                        const auto source_text = clipboard_store.read_text(custom_prompt_dialog.action.source_ref);
                        TextGenerationRequest request{
                            .request_id = active_batch.request.request_id + "_custom_" + std::to_string(++request_counter),
                            .endpoint = active_batch.general_llm.endpoint,
                            .api_key = active_batch.general_llm.api_key,
                            .model_id = active_batch.general_llm.model_id,
                            .system_message = custom_prompt_dialog.prompt,
                            .user_message = source_text,
                            .temperature = 0.2,
                        };
                        auto job = std::make_unique<GenerationJob>();
                        job->kind = GenerationJob::Kind::TextPrompt;
                        job->action = custom_prompt_dialog.action;
                        job->source_text = source_text;
                        job->state.start(job->action.id, source_text, request);
                        job->panel = {.open = true, .running = true, .request_id = request.request_id,
                                      .editable_text = {}, .error = {}};
                        job->pending.emplace(std::async(std::launch::async, [&llm_client, request] {
                            return llm_client.generate(request);
                        }));
                        generation_jobs.push_back(std::move(job));
                        custom_prompt_dialog.open = false;
                        execution_status = "Custom prompt is running…";
                    }
                }
                if (!can_run) ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button(tr(ui_language, UiTextKey::Cancel).c_str())) custom_prompt_dialog.open = false;
                if (!execution_status.empty()) copyable_text(execution_status, true);
            }
            ImGui::End();
        }

        if (graph_dialog.open) {
            if (graph_dialog.dirty) {
                graph_dialog.dirty = false;
                graph_dialog.data = parse_graph_data(graph_dialog.source, graph_dialog.header, graph_dialog.convert_dates);
                graph_dialog.png.clear();
                graph_dialog.texture.clear();
                graph_dialog.status.clear();
                if (graph_dialog.data) {
                    graph_dialog.png = render_graph_png(*graph_dialog.data, static_cast<GraphType>(graph_dialog.type));
                    if (!graph_dialog.png.empty()) (void)load_image_texture(graph_dialog.png, graph_dialog.texture);
                }
                if (graph_dialog.png.empty()) graph_dialog.status =
                    graph_dialog.data && graph_dialog.type == static_cast<int>(GraphType::Pie)
                        ? "Pie charts need nonnegative values with a positive total."
                        : "No valid numeric series for these options.";
            }
            if (graph_dialog.focus_pending) {
                ImGui::SetNextWindowFocus();
                ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
            }
            ImGui::SetNextWindowClass(&auxiliary_window_class);
            const auto* graph_viewport = ImGui::GetMainViewport();
            const float graph_width = std::max(420.0F, std::min(820.0F, graph_viewport->WorkSize.x - 40.0F));
            const float preview_scale = std::min(1.0F, (graph_width - 24.0F) / 800.0F);
            const float graph_height = std::min(graph_viewport->WorkSize.y - 40.0F,
                                                165.0F + 480.0F * preview_scale);
            ImGui::SetNextWindowSize(ImVec2(graph_width, graph_height), ImGuiCond_Appearing);
            const auto graph_title = tr(ui_language, UiTextKey::GraphPreview) + "##graph";
            if (ImGui::Begin(graph_title.c_str(), &graph_dialog.open,
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
                if (graph_dialog.focus_pending) {
                    ImGui::SetWindowFocus();
                    graph_dialog.focus_pending = false;
                }
                const bool chinese = ui_language == UiLanguage::SimplifiedChinese;
                const char* types[] = {chinese ? "折线图" : "Line", chinese ? "柱状图" : "Bar",
                                       chinese ? "饼图" : "Pie"};
                graph_dialog.dirty |= ImGui::Combo(tr(ui_language, UiTextKey::ChartType).c_str(), &graph_dialog.type, types, 3);
                ImGui::SameLine();
                graph_dialog.dirty |= ImGui::Checkbox(tr(ui_language, UiTextKey::HeaderRow).c_str(), &graph_dialog.header);
                ImGui::SameLine();
                graph_dialog.dirty |= ImGui::Checkbox(tr(ui_language, UiTextKey::ConvertDates).c_str(), &graph_dialog.convert_dates);
                if (graph_dialog.texture.id != 0) {
                    const float scale = std::max(0.1F, std::min({1.0F, ImGui::GetContentRegionAvail().x / graph_dialog.texture.width,
                                                  (graph_height - 165.0F) / graph_dialog.texture.height}));
                    ImGui::Image((ImTextureID)(intptr_t)graph_dialog.texture.id,
                                 ImVec2(graph_dialog.texture.width * scale, graph_dialog.texture.height * scale));
                }
                if (graph_dialog.data) {
                    ImGui::Text("%zu points", graph_dialog.data->points.size());
                    if (graph_dialog.data->has_dates) ImGui::SameLine(), ImGui::TextUnformatted("Date spacing enabled");
                }
                input_text_string(tr(ui_language, UiTextKey::SavePngAs).c_str(), graph_dialog.save_path);
                const bool has_image = !graph_dialog.png.empty();
                if (!has_image) ImGui::BeginDisabled();
                if (ImGui::Button(tr(ui_language, UiTextKey::CopyGraphImage).c_str())) {
                    graph_dialog.status = platform->publish_image(graph_dialog.png, "image/png")
                        ? "Graph copied to clipboard" : "Could not copy graph image";
                }
                ImGui::SameLine();
                if (ImGui::Button(tr(ui_language, UiTextKey::SaveGraphImage).c_str())) {
                    const auto path = path_from_utf8_string(trim(graph_dialog.save_path));
                    std::ofstream output(path, std::ios::binary | std::ios::trunc);
                    output.write(reinterpret_cast<const char*>(graph_dialog.png.data()),
                                 static_cast<std::streamsize>(graph_dialog.png.size()));
                    output.close();
                    graph_dialog.status = output ? "Saved graph: " + path_to_utf8_string(path) : "Could not save graph image";
                    if (output) (void)path_history.observe_path(path, PathKind::File, "graph", current_time_ms());
                }
                if (!has_image) ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button(tr(ui_language, UiTextKey::Close).c_str())) graph_dialog.open = false;
                if (!graph_dialog.status.empty()) copyable_text(graph_dialog.status, true);
            }
            ImGui::End();
        }

        if (file_confirmation) {
            auto& state = *file_confirmation;
            bool close_confirmation = false;
            if (state.focus_pending) {
                ImGui::SetNextWindowFocus();
                const auto* viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
            }
            ImGui::SetNextWindowClass(&auxiliary_window_class);
            const auto destination_rows = std::clamp<std::size_t>(state.draft.candidate_destinations.size(), 1, 5);
            const auto preview_rows = std::clamp<std::size_t>(multiline_editor_row_count(state.source_preview), 1, 4);
            const float destinations_height = 28.0F + 27.0F * static_cast<float>(destination_rows);
            const float confirmation_height = std::clamp(205.0F + destinations_height +
                static_cast<float>(preview_rows) * ImGui::GetTextLineHeightWithSpacing(), 320.0F, 560.0F);
            ImGui::SetNextWindowSize(ImVec2(650, confirmation_height), ImGuiCond_Appearing);
            bool open = true;
            const auto confirmation_title=tr(ui_language,UiTextKey::ConfirmFileOperation)+"##file-confirm";
            if (ImGui::Begin(confirmation_title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
                if (state.focus_pending) {
                    ImGui::SetWindowFocus();
                    state.focus_pending = false;
                }
                ImGui::BeginChild("file-confirm-body", ImVec2(0.0F, -ImGui::GetFrameHeightWithSpacing() * 1.5F), true,
                                  ImGuiWindowFlags_HorizontalScrollbar);
                copyable_text(state.source_preview, true);
                std::string directory = path_to_utf8_string(state.draft.destination);
                if (input_text_string(tr(ui_language,UiTextKey::Destination).c_str(), directory)) {
                    state.draft.destination = path_from_utf8_string(directory);
                    manual_destination = state.draft.destination;
                }
                ImGui::SameLine();
                if (ImGui::Button(tr(ui_language,UiTextKey::Browse).c_str())) {
                    if (const auto chosen = platform->choose_directory(state.draft.destination)) {
                        state.draft.destination = *chosen;
                        manual_destination = *chosen;
                        const auto observed = path_history.observe_path(*chosen, PathKind::Directory,
                                                                       "manual", current_time_ms());
                        state.draft.candidate_destinations.insert(state.draft.candidate_destinations.begin(), observed);
                    }
                }
                input_text_string(tr(ui_language,UiTextKey::Filename).c_str(), state.draft.filename);
                copyable_text(tr(ui_language,UiTextKey::Seen));
                if (ImGui::BeginTable("confirmation-destinations", 3,
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                          ImGuiTableFlags_SizingStretchProp,
                                      ImVec2(0.0F, destinations_height))) {
                    ImGui::TableSetupColumn(tr(ui_language,UiTextKey::Path).c_str(), ImGuiTableColumnFlags_WidthStretch, 4.0F);
                    ImGui::TableSetupColumn(tr(ui_language,UiTextKey::Source).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0F);
                    ImGui::TableSetupColumn(tr(ui_language,UiTextKey::Use).c_str(), ImGuiTableColumnFlags_WidthFixed, 80.0F);
                    ImGui::TableHeadersRow();
                    for (const auto& shortcut : state.draft.candidate_destinations) {
                        ImGui::PushID(shortcut.ref.c_str());
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); copyable_text(path_to_utf8_string(shortcut.path));
                        ImGui::TableNextColumn(); copyable_text(shortcut.source);
                        ImGui::TableNextColumn();
                        if (ImGui::SmallButton(tr(ui_language,UiTextKey::Use).c_str())) {
                            state.draft.destination = shortcut.path;
                            manual_destination = shortcut.path;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                state.can_confirm = validate_file_operation_draft(state.draft, clipboard_store);
                const auto panel = build_file_operation_confirmation_panel_model(state);
                copyable_text(tr(ui_language,UiTextKey::OutputPath) + ": " + panel.output_preview);
                if (!panel.validation_error.empty()) {
                    copyable_text(panel.validation_error, true);
                }
                ImGui::EndChild();
                if (!state.can_confirm) ImGui::BeginDisabled();
                if (ImGui::Button(tr(ui_language,UiTextKey::Confirm).c_str())) {
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
                if (!state.can_confirm) ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button(tr(ui_language,UiTextKey::Cancel).c_str())) close_confirmation = true;
            }
            ImGui::End();
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
            if (image_preview.focus_pending) {
                ImGui::SetNextWindowFocus();
                const auto* viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
            }
            ImGui::SetNextWindowClass(&auxiliary_window_class);
            ImGui::SetNextWindowSize(ImVec2(760,600),ImGuiCond_FirstUseEver);
            const auto image_title=tr(ui_language,UiTextKey::ImagePreview)+"##image-preview";
            if (ImGui::Begin(image_title.c_str(), &image_preview.open, ImGuiWindowFlags_NoSavedSettings)) {
                if (image_preview.focus_pending) {
                    ImGui::SetWindowFocus();
                    image_preview.focus_pending = false;
                }
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
                    const auto output=std::filesystem::temp_directory_path()/("pastit-preview-"+item.ref+extension);std::ofstream stream(output,std::ios::binary|std::ios::trunc);const auto bytes=clipboard_store.read(item.ref);stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));stream.close();if(stream){(void)path_history.observe_path(output,PathKind::File,"preview",current_time_ms());platform->open_path(output);execution_status=tr(ui_language,UiTextKey::OpenedTemporaryImage)+path_to_utf8_string(output);}
                }
                ImGui::BeginChild("image-scroll",ImVec2(0,0),true,ImGuiWindowFlags_HorizontalScrollbar);
                if(ImGui::IsWindowHovered())image_preview.set_zoom(image_preview.zoom+ImGui::GetIO().MouseWheel*0.1F);
                const auto available=ImGui::GetContentRegionAvail();const float fit=std::min({1.0F,available.x/static_cast<float>(preview_texture.width),available.y/static_cast<float>(preview_texture.height)});
                ImGui::Image((ImTextureID)(intptr_t)preview_texture.id,ImVec2(preview_texture.width*fit*image_preview.zoom,preview_texture.height*fit*image_preview.zoom));ImGui::EndChild();
            }ImGui::End();
        }

        for (auto& job : generation_jobs) {
            if (!job->panel.open) continue;
            if (job->focus_pending) {
                ImGui::SetNextWindowFocus();
                const auto* viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
            }
            ImGui::SetNextWindowClass(&auxiliary_window_class);
            const auto result_rows = std::clamp<std::size_t>(multiline_editor_row_count(job->panel.editable_text), 3, 16);
            const float result_height = job->panel.running ? 220.0F :
                115.0F + static_cast<float>(result_rows) * ImGui::GetTextLineHeightWithSpacing();
            const auto* result_begin = job->panel.editable_text.data();
            const auto* result_end = result_begin + std::min<std::size_t>(job->panel.editable_text.size(), 2048);
            const float result_width = job->panel.running ? 420.0F :
                std::clamp(100.0F + ImGui::CalcTextSize(result_begin, result_end).x, 420.0F, 680.0F);
            ImGui::SetNextWindowSize(ImVec2(result_width, result_height), ImGuiCond_Appearing);
            const auto title=tr(ui_language,UiTextKey::AiResult)+"##"+job->panel.request_id;
            if (ImGui::Begin(title.c_str(),&job->panel.open, ImGuiWindowFlags_NoSavedSettings)) {
                if (job->focus_pending) {
                    ImGui::SetWindowFocus();
                    job->focus_pending = false;
                }
                if(job->panel.running)copyable_text(tr(ui_language,UiTextKey::Generating));
                ImGui::BeginChild("ai-result-body", ImVec2(0.0F, -ImGui::GetFrameHeightWithSpacing()), true,
                                  ImGuiWindowFlags_HorizontalScrollbar);
                if(!job->panel.error.empty())copyable_text(job->panel.error, true);
                input_text_string("##ai-result",job->panel.editable_text,true,0,-1.0F);
                ImGui::EndChild();
                if(ImGui::Button(tr(ui_language,UiTextKey::CopyResult).c_str()))platform->copy_text(job->panel.editable_text);
                if(ImGui::Button(tr(ui_language,UiTextKey::ReplaceClipboard).c_str()))platform->publish_text(job->panel.editable_text);
                if(ImGui::Button(tr(ui_language,UiTextKey::Retry).c_str())&&!job->pending.has_value()){if(auto retry=job->state.retry_request()){job->panel.running=true;job->panel.error.clear();job->focus_pending=true;job->pending.emplace(std::async(std::launch::async,[&llm_client,request=*retry]{return llm_client.generate(request);}));}}
            }ImGui::End();
        }

        if (show_settings) {
            ImGui::SetNextWindowClass(&auxiliary_window_class);
            ImGui::SetNextWindowSize(ImVec2(680,620),ImGuiCond_FirstUseEver);
            const auto settings_title=tr(ui_language,UiTextKey::Settings)+"##settings";
            if (ImGui::Begin(settings_title.c_str(),&show_settings)) {
                int language=static_cast<int>(settings_draft.language);const char* languages[]={"System","English","简体中文"};if(ImGui::Combo(tr(ui_language,UiTextKey::Language).c_str(),&language,languages,3))settings_draft.language=static_cast<UiLanguage>(language);
                if(ImGui::SliderFloat(tr(ui_language,UiTextKey::Opacity).c_str(),&settings_draft.window_opacity,0.55F,1.0F)&&
                   !platform->set_popup_opacity(settings_draft.window_opacity))glfwSetWindowOpacity(window,settings_draft.window_opacity);
                auto image_dir=path_to_utf8_string(settings_draft.default_image_directory);if(input_text_string(tr(ui_language,UiTextKey::DefaultImageDirectory).c_str(),image_dir))settings_draft.default_image_directory=path_from_utf8_string(image_dir);
                auto text_dir=path_to_utf8_string(settings_draft.default_text_directory);if(input_text_string(tr(ui_language,UiTextKey::DefaultTextDirectory).c_str(),text_dir))settings_draft.default_text_directory=path_from_utf8_string(text_dir);
                if(ImGui::CollapsingHeader(tr(ui_language,UiTextKey::Djev).c_str(),ImGuiTreeNodeFlags_DefaultOpen)){input_text_string(tr(ui_language,UiTextKey::DjevEndpoint).c_str(),settings_draft.djev.endpoint);input_text_string(tr(ui_language,UiTextKey::DjevModel).c_str(),settings_draft.djev.model_id);input_text_string(tr(ui_language,UiTextKey::DjevApiKey).c_str(),settings_draft.djev.api_key,false,ImGuiInputTextFlags_Password);if(ImGui::Button(tr(ui_language,UiTextKey::TestDjev).c_str())&&!pending_provider_test){pending_provider_test_is_djev=true;djev_test_status="Testing…";const auto provider=settings_draft.djev;const auto request=provider_test_request();const auto language=ui_language;pending_provider_test.emplace(std::async(std::launch::async,[provider,request,language]{const auto result=DjevClient(provider.endpoint,provider.model_id,DjevClient::kDefaultTimeout,{},provider.api_key).decide(request);return result.valid?tr(language,UiTextKey::DjevTestSucceeded):tr(language,UiTextKey::DjevTestFailed)+result.error;}));}ImGui::SameLine();if(!djev_test_status.empty())ImGui::TextUnformatted(djev_test_status.c_str());}
                if(ImGui::CollapsingHeader(tr(ui_language,UiTextKey::GeneralLlm).c_str(),ImGuiTreeNodeFlags_DefaultOpen)){input_text_string(tr(ui_language,UiTextKey::LlmEndpoint).c_str(),settings_draft.general_llm.endpoint);input_text_string(tr(ui_language,UiTextKey::LlmModel).c_str(),settings_draft.general_llm.model_id);input_text_string(tr(ui_language,UiTextKey::LlmApiKey).c_str(),settings_draft.general_llm.api_key,false,ImGuiInputTextFlags_Password);if(ImGui::Button(tr(ui_language,UiTextKey::TestGeneralLlm).c_str())&&!pending_provider_test){pending_provider_test_is_djev=false;llm_test_status="Testing…";const auto provider=settings_draft.general_llm;const auto language=ui_language;pending_provider_test.emplace(std::async(std::launch::async,[provider,language]{OpenAiCompatibleClient client;const auto result=client.generate({.request_id="settings-test",.endpoint=provider.endpoint,.api_key=provider.api_key,.model_id=provider.model_id,.system_message="Return OK.",.user_message="OK",.temperature=0.0,.timeout=std::chrono::milliseconds{5000}});return result.ok?tr(language,UiTextKey::GeneralLlmTestSucceeded):tr(language,UiTextKey::GeneralLlmTestFailed)+result.error;}));}ImGui::SameLine();if(!llm_test_status.empty())ImGui::TextUnformatted(llm_test_status.c_str());}
                if(ImGui::CollapsingHeader(tr(ui_language,UiTextKey::PromptTemplates).c_str(),ImGuiTreeNodeFlags_DefaultOpen)){
                    PromptTemplateService service(settings_draft.prompt_templates);
                    if (prompt_panel_model.modal == PromptTemplateModal::None) {
                        prompt_panel_model = build_prompt_templates_panel_model(settings_draft.prompt_templates);
                    }
                    std::string view_id, edit_id, duplicate_id, delete_id;
                    if (ImGui::BeginTable("prompt-templates",5,ImGuiTableFlags_Resizable|ImGuiTableFlags_RowBg|ImGuiTableFlags_Borders|ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn(tr(ui_language,UiTextKey::Enabled).c_str(),ImGuiTableColumnFlags_WidthFixed,70);
                        ImGui::TableSetupColumn(tr(ui_language,UiTextKey::Name).c_str(),ImGuiTableColumnFlags_WidthStretch,3);
                        ImGui::TableSetupColumn(tr(ui_language,UiTextKey::Temperature).c_str(),ImGuiTableColumnFlags_WidthFixed,90);
                        const auto template_kind_header=tr(ui_language,UiTextKey::BuiltIn)+"/"+tr(ui_language,UiTextKey::Custom);
                        ImGui::TableSetupColumn(template_kind_header.c_str(),ImGuiTableColumnFlags_WidthFixed,110);
                        ImGui::TableSetupColumn(tr(ui_language,UiTextKey::Actions).c_str(),ImGuiTableColumnFlags_WidthFixed,190);
                        ImGui::TableHeadersRow();
                        for (const auto& row : prompt_panel_model.rows) {
                            ImGui::PushID(row.id.c_str());ImGui::TableNextRow();ImGui::TableNextColumn();
                            bool enabled=row.enabled;if(ImGui::Checkbox("##enabled",&enabled)){std::string error;service.set_enabled(row.id,enabled,error);}
                            ImGui::TableNextColumn();copyable_text(row.name);
                            ImGui::TableNextColumn();copyable_text(std::to_string(row.temperature));
                            ImGui::TableNextColumn();copyable_text(row.built_in?tr(ui_language,UiTextKey::BuiltIn):tr(ui_language,UiTextKey::Custom));
                            ImGui::TableNextColumn();
                            if(ImGui::SmallButton(tr(ui_language,UiTextKey::View).c_str()))view_id=row.id;ImGui::SameLine();
                            if(ImGui::SmallButton(tr(ui_language,UiTextKey::Edit).c_str()))edit_id=row.id;ImGui::SameLine();
                            if(ImGui::SmallButton(tr(ui_language,UiTextKey::Duplicate).c_str()))duplicate_id=row.id;ImGui::SameLine();
                            if(ImGui::SmallButton(tr(ui_language,UiTextKey::Delete).c_str()))delete_id=row.id;
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                    if(!view_id.empty())view_prompt_template(prompt_panel_model,view_id);
                    if(!edit_id.empty())begin_prompt_template_edit(prompt_panel_model,edit_id);
                    if(!duplicate_id.empty())duplicate_prompt_template(prompt_panel_model,service,duplicate_id);
                    if(!delete_id.empty())begin_prompt_template_delete(prompt_panel_model,delete_id);
                    const auto prompt_detail_title=tr(ui_language,UiTextKey::Details)+"##prompt-detail";
                    const auto prompt_edit_title=tr(ui_language,UiTextKey::Edit)+"##prompt-edit";
                    const auto prompt_delete_title=tr(ui_language,UiTextKey::Delete)+"##prompt-delete";
                    if(prompt_panel_model.modal==PromptTemplateModal::View&&prompt_panel_model.draft){
                        bool detail_open=true;ImGui::SetNextWindowClass(&auxiliary_window_class);
                        const auto detail_rows=std::clamp<std::size_t>(multiline_editor_row_count(prompt_panel_model.draft->system_prompt),2,14);
                        const float detail_width=std::clamp(80.0F+ImGui::CalcTextSize(prompt_panel_model.draft->system_prompt.c_str()).x,340.0F,620.0F);
                        ImGui::SetNextWindowSize(ImVec2(detail_width,95+detail_rows*ImGui::GetTextLineHeightWithSpacing()),ImGuiCond_Appearing);
                        if(ImGui::Begin(prompt_detail_title.c_str(),&detail_open)){copyable_text(prompt_panel_model.draft->name);ImGui::Separator();copyable_text(prompt_panel_model.draft->system_prompt,true);if(ImGui::Button(tr(ui_language,UiTextKey::Close).c_str()))detail_open=false;}ImGui::End();
                        if(!detail_open)cancel_prompt_template_modal(prompt_panel_model);
                    }
                    if(prompt_panel_model.modal==PromptTemplateModal::Edit&&prompt_panel_model.draft){
                        bool edit_open=true;ImGui::SetNextWindowClass(&auxiliary_window_class);ImGui::SetNextWindowSize(ImVec2(600,440),ImGuiCond_FirstUseEver);
                        if(ImGui::Begin(prompt_edit_title.c_str(),&edit_open)){
                            input_text_string(tr(ui_language,UiTextKey::TemplateName).c_str(),prompt_panel_model.draft->name);
                            const float edit_footer_height=3.0F*ImGui::GetFrameHeightWithSpacing();
                            input_text_string(tr(ui_language,UiTextKey::SystemPrompt).c_str(),
                                              prompt_panel_model.draft->system_prompt,true,0,-edit_footer_height);
                            ImGui::Checkbox(tr(ui_language,UiTextKey::Enabled).c_str(),&prompt_panel_model.draft->enabled);
                            float temperature=static_cast<float>(prompt_panel_model.draft->temperature);
                            if(ImGui::SliderFloat(tr(ui_language,UiTextKey::Temperature).c_str(),&temperature,0,2))prompt_panel_model.draft->temperature=temperature;
                            if(ImGui::Button(tr(ui_language,UiTextKey::Save).c_str())){const auto result=save_prompt_template_edit(prompt_panel_model,service);if(result.error.empty())edit_open=false;}
                            ImGui::SameLine();if(ImGui::Button(tr(ui_language,UiTextKey::Cancel).c_str()))edit_open=false;
                        }ImGui::End();
                        if(!edit_open&&prompt_panel_model.modal!=PromptTemplateModal::None)cancel_prompt_template_modal(prompt_panel_model);
                    }
                    if(prompt_panel_model.modal==PromptTemplateModal::Delete){
                        bool delete_open=true;ImGui::SetNextWindowClass(&auxiliary_window_class);ImGui::SetNextWindowSize(ImVec2(420,115),ImGuiCond_Appearing);
                        if(ImGui::Begin(prompt_delete_title.c_str(),&delete_open)){copyable_text(tr(ui_language,UiTextKey::DeleteTemplateText));if(ImGui::Button(tr(ui_language,UiTextKey::ConfirmDelete).c_str())){confirm_prompt_template_delete(prompt_panel_model,service);delete_open=false;}ImGui::SameLine();if(ImGui::Button(tr(ui_language,UiTextKey::Keep).c_str()))delete_open=false;}ImGui::End();
                        if(!delete_open&&prompt_panel_model.modal!=PromptTemplateModal::None)cancel_prompt_template_modal(prompt_panel_model);
                    }
                    if(ImGui::Button(tr(ui_language,UiTextKey::NewTemplate).c_str())){std::string error;if(const auto created=service.create("New Prompt","Transform {text}",0.2,error)){prompt_panel_model=build_prompt_templates_panel_model(settings_draft.prompt_templates);begin_prompt_template_edit(prompt_panel_model,created->id);}}ImGui::SameLine();if(ImGui::Button(tr(ui_language,UiTextKey::RestoreDefaults).c_str())){service.restore_defaults();prompt_panel_model=build_prompt_templates_panel_model(settings_draft.prompt_templates);}
                }
                if(ImGui::Button(tr(ui_language,UiTextKey::ResetGeneral).c_str())){const auto defaults=default_settings();settings_draft.language=defaults.language;settings_draft.window_opacity=defaults.window_opacity;if(!platform->set_popup_opacity(settings_draft.window_opacity))glfwSetWindowOpacity(window,settings_draft.window_opacity);}ImGui::SameLine();
                if(ImGui::Button(tr(ui_language,UiTextKey::ResetPaths).c_str())){const auto defaults=default_settings();settings_draft.default_image_directory=defaults.default_image_directory;settings_draft.default_text_directory=defaults.default_text_directory;}ImGui::SameLine();
                if(ImGui::Button(tr(ui_language,UiTextKey::ResetProviders).c_str())){const auto defaults=default_settings();settings_draft.djev=defaults.djev;settings_draft.general_llm=defaults.general_llm;}
                ImGui::Separator();ImGui::TextUnformatted("Mermaid: self-contained offline HTML");
                if(ImGui::SmallButton("Mermaid homepage##aux"))platform->open_uri("https://mermaid.js.org/");ImGui::SameLine();
                if(ImGui::SmallButton("QR library homepage##aux"))platform->open_uri("https://www.nayuki.io/page/qr-code-generator-library");
                if(!settings_status.empty())copyable_text(settings_status,true);
                if(ImGui::Button(tr(ui_language,UiTextKey::Save).c_str())){AppSettings candidate=settings;std::string error;if(save_settings_draft(settings_store,settings,settings_draft,candidate,error)){settings=candidate;settings_draft=settings;if(!platform->set_popup_opacity(settings.window_opacity))glfwSetWindowOpacity(window,settings.window_opacity);djev_client=DjevClient(settings.djev.endpoint,settings.djev.model_id,DjevClient::kDefaultTimeout,{},settings.djev.api_key);fast_action_executor.set_renderer_settings(settings.renderers);download_manager.set_options({.keep_part_files_on_cancel=settings.downloads.keep_part_files});annotation_panel.export_directory=settings.annotation.save_directory;annotation_panel.export_format=settings.annotation.export_format;
                    platform->apply_settings(settings);
                    settings_status=tr(ui_language,UiTextKey::Saved);show_settings=false;}else settings_status=error;}ImGui::SameLine();if(ImGui::Button(tr(ui_language,UiTextKey::Cancel).c_str())){settings_draft=settings;show_settings=false;}
            }ImGui::End();
            if(!show_settings){settings_draft=settings;if(!platform->set_popup_opacity(settings.window_opacity))glfwSetWindowOpacity(window,settings.window_opacity);}
        }

        if (!popup_visible && !has_any_auxiliary_window()) {
            glfwSetWindowAttrib(window, GLFW_FLOATING, GLFW_FALSE);
            glfwHideWindow(window);
        }

        ImGui::Render();
        int display_width = 0;
        int display_height = 0;
        glfwGetFramebufferSize(window, &display_width, &display_height);
        glViewport(0, 0, display_width, display_height);
        glClearColor(0.055F, 0.06F, 0.075F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_context);
        }
        glfwSwapBuffers(window);
        if (activated_action.has_value()) {
            execute_selected(*activated_action, std::move(activated_prompt_variables));
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

}  // namespace pastit
