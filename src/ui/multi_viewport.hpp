#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <filesystem>
#include <functional>
#include <future>
#include <optional>

#if defined(PASTIT_HAS_DESKTOP_DEPS)
#include <imgui.h>
#endif

namespace pastit {

struct ViewportSize {
    float width = 640.0F;
    float height = 420.0F;
};

struct IndependentViewportDescriptor {
    std::string title;
    bool independent = true;
    bool is_root_child = false;
    float initial_width = 640.0F;
    float initial_height = 420.0F;
};

enum class PanelCommandKind { None, ShowSource, ShowPreview, CopyText, SaveText, SaveRenderedOutput };

struct PanelSaveResult {
    bool success = false;
    std::filesystem::path destination;
    std::string error;
};

struct FastActionPanelCommand {
    std::string id;
    std::string label;
    std::string value;
    bool enabled = true;
    PanelCommandKind kind = PanelCommandKind::None;
};

struct FastActionPanelRow {
    std::string label;
    std::string value;
    bool selectable = true;
    FastActionPanelCommand copy_command;
};

struct FastActionPanelModel {
    IndependentViewportDescriptor viewport;
    std::vector<FastActionPanelCommand> toolbar;
    std::vector<FastActionPanelRow> rows;
    std::string status_text;
    std::string primary_text;
    std::string fallback_text;
    std::size_t max_visible_rows = 10;
    bool preview_available = false;
    bool annotation_section_expanded = false;
    bool selectable_read_only_multiline = false;
};

PanelSaveResult save_panel_command(const FastActionPanelCommand& command,
                                   const std::filesystem::path& destination,
                                   bool confirm_overwrite,
                                   std::string_view required_extension);

struct RendererPreviewPanelState {
    enum class Mode { Source, Preview };
    Mode mode = Mode::Source;
    std::string destination;
    bool confirm_overwrite = false;
    std::string status_text;
    std::optional<std::future<PanelSaveResult>> pending_save;
    std::optional<std::future<bool>> pending_open;

    bool select_view(const FastActionPanelCommand& command, bool preview_available);
    bool save(const FastActionPanelCommand& command, std::string_view extension);
    void open_preview(std::filesystem::path path, std::function<bool(const std::filesystem::path&)> opener);
    void poll();
};

IndependentViewportDescriptor independent_panel_viewport(std::string title, ViewportSize size);
std::size_t panel_text_visible_rows(std::string_view value);

#if defined(PASTIT_HAS_DESKTOP_DEPS)
void configure_independent_viewports(ImGuiIO& io, ImGuiStyle& style);
ImGuiWindowClass independent_window_class();
#endif

}  // namespace pastit
