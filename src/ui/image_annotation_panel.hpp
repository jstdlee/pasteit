#pragma once

#include "annotation/annotation_export.hpp"
#include "annotation/image_annotation.hpp"
#include "ui/multi_viewport.hpp"

#include <filesystem>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace pasteit {

struct ImageAnnotationPanelState {
    bool open = false;
    bool focus_pending = false;
    AnnotationDocument document;
    AnnotationTool active_tool = AnnotationTool::Pen;
    std::string comment_text = "Comment";
    std::string status_text;
    std::filesystem::path last_export_path;
    bool drawing = false;
    AnnotationPoint drag_start;
    std::vector<AnnotationPoint> pending_pen;
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
