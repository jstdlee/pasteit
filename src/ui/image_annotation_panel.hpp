#pragma once

#include "annotation/annotation_export.hpp"
#include "annotation/image_annotation.hpp"
#include "ui/multi_viewport.hpp"

#include <filesystem>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pasteit {

// Canvas tools; shortcuts (while the canvas window is focused and no text is
// being typed): S select/move, P pen, L line, A arrow, R rectangle, C circle, T text.
enum class AnnotationPanelTool { Select, Pen, Line, Arrow, Rectangle, Circle, Text };

struct ImageAnnotationPanelState {
    bool open = false;
    bool focus_pending = false;
    AnnotationDocument document;
    AnnotationPanelTool active_tool = AnnotationPanelTool::Pen;
    AnnotationColor color = annotation_red();
    float stroke_width = 3.0F;  // image pixels
    float font_size = 22.0F;    // image pixels
    std::string status_text;
    std::filesystem::path last_export_path;
    std::string output_path;  // editable; empty until first shown
    bool drawing = false;
    AnnotationPoint drag_start;
    std::vector<AnnotationPoint> pending_pen;
    // Text tool: a click opens a small editor at the anchor.
    // Select tool: the picked overlay and the last drag position.
    int selected = -1;
    bool moving = false;
    AnnotationPoint move_last;
    bool text_editing = false;
    int text_session = 0;  // new widget ID per edit so a stale input cannot re-commit
    bool text_focus_pending = false;
    AnnotationPoint text_anchor;
    std::string text_draft;
    // Shell hooks for the saved file.
    std::function<bool(const std::filesystem::path&)> open_path;
    std::function<void(std::string_view)> copy_text;
    std::optional<std::future<AnnotationExportResult>> pending_export;
    std::filesystem::path export_directory;
    std::string export_format = "svg";

    bool export_running() const { return pending_export.has_value(); }
    using Exporter = std::function<AnnotationExportResult(const AnnotationDocument&, const std::filesystem::path&)>;
    bool start_export(const std::filesystem::path& output, Exporter exporter = export_annotation_svg);
    void poll_export();
};

FastActionPanelModel build_image_annotation_panel_model(const ImageAnnotationPanelState& state);

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
void draw_image_annotation_panel(ImageAnnotationPanelState& state,
                                 unsigned int texture_id,
                                 int image_width,
                                 int image_height);
#endif

}  // namespace pasteit
