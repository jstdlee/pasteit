#include "ui/image_annotation_panel.hpp"

#include "ui/imgui_widgets.hpp"
#include "util/path_utf8.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <sstream>

#if defined(PASTIT_HAS_DESKTOP_DEPS)
#include <imgui.h>
#endif

namespace pastit {
namespace {

std::filesystem::path svg_export_path(std::filesystem::path output) {
    output.replace_extension(".svg");
    return output;
}

#if defined(PASTIT_HAS_DESKTOP_DEPS)
std::string tool_label(AnnotationTool tool) {
    switch (tool) {
        case AnnotationTool::Pen: return "Pen";
        case AnnotationTool::Line: return "Line";
        case AnnotationTool::Rectangle: return "Rectangle";
        case AnnotationTool::Arrow: return "Arrow";
    }
    return "Tool";
}

std::filesystem::path default_export_path(const ImageAnnotationPanelState& state) {
    const auto& document = state.document;
    const auto base = document.original_image().empty()
        ? std::filesystem::temp_directory_path() / "pastit-annotation"
        : document.original_image();
    const auto directory = state.export_directory.empty() ? base.parent_path() : state.export_directory;
    return directory / path_from_utf8_string(path_to_utf8_string(base.stem()) + "-annotated.svg");
}

ImVec2 to_imvec2(const AnnotationPoint& point, const ImVec2& origin, float scale) {
    return ImVec2(origin.x + point.x * scale, origin.y + point.y * scale);
}

AnnotationPoint from_mouse(const ImVec2& mouse, const ImVec2& origin, float scale) {
    return AnnotationPoint{(mouse.x - origin.x) / scale, (mouse.y - origin.y) / scale};
}

ImU32 color_u32(const AnnotationColor& color) {
    return IM_COL32(color.r, color.g, color.b, color.a);
}

void draw_overlay(ImDrawList* draw_list, const AnnotationOverlay& overlay, const ImVec2& origin, float scale) {
    if (overlay.kind == AnnotationOverlayKind::Comment) {
        const auto top_left = to_imvec2(overlay.comment.anchor, origin, scale);
        const auto width = std::max(96.0F, static_cast<float>(overlay.comment.text.size()) * 7.0F + 20.0F) * scale;
        const auto height = 30.0F * scale;
        draw_list->AddRectFilled(top_left, ImVec2(top_left.x + width, top_left.y + height),
                                 color_u32(overlay.comment.background), 4.0F);
        draw_list->AddRect(top_left, ImVec2(top_left.x + width, top_left.y + height), IM_COL32(255, 0, 0, 255));
        draw_list->AddText(ImVec2(top_left.x + 10.0F * scale, top_left.y + 8.0F * scale),
                           color_u32(overlay.comment.foreground), overlay.comment.text.c_str());
        return;
    }
    const auto& stroke = overlay.stroke;
    const auto color = color_u32(stroke.color);
    const auto width = stroke.width * scale;
    if (stroke.tool == AnnotationTool::Rectangle && stroke.points.size() >= 2) {
        draw_list->AddRect(to_imvec2(stroke.points[0], origin, scale),
                           to_imvec2(stroke.points[1], origin, scale), color, 0.0F, 0, width);
        return;
    }
    if ((stroke.tool == AnnotationTool::Line || stroke.tool == AnnotationTool::Arrow) && stroke.points.size() >= 2) {
        const auto start = to_imvec2(stroke.points[0], origin, scale);
        const auto end = to_imvec2(stroke.points[1], origin, scale);
        draw_list->AddLine(start, end, color, width);
        if (stroke.tool == AnnotationTool::Arrow) {
            const auto angle = std::atan2(end.y - start.y, end.x - start.x);
            constexpr float arrow_size = 12.0F;
            const auto left = ImVec2(end.x - std::cos(angle - 0.45F) * arrow_size,
                                     end.y - std::sin(angle - 0.45F) * arrow_size);
            const auto right = ImVec2(end.x - std::cos(angle + 0.45F) * arrow_size,
                                      end.y - std::sin(angle + 0.45F) * arrow_size);
            draw_list->AddTriangleFilled(end, left, right, color);
        }
        return;
    }
    for (std::size_t index = 1; index < stroke.points.size(); ++index) {
        draw_list->AddLine(to_imvec2(stroke.points[index - 1], origin, scale),
                           to_imvec2(stroke.points[index], origin, scale), color, width);
    }
}
#endif

}  // namespace

bool ImageAnnotationPanelState::start_export(const std::filesystem::path& output, Exporter exporter) {
    if (pending_export.has_value()) {
        status_text = "Annotation export is already running";
        return false;
    }
    status_text = "Saving annotated SVG...";
    const auto snapshot = document;
    const auto svg_output = svg_export_path(output);
    pending_export.emplace(std::async(std::launch::async,
        [snapshot, svg_output, exporter = std::move(exporter)] { return exporter(snapshot, svg_output); }));
    return true;
}

void ImageAnnotationPanelState::poll_export() {
    if (!pending_export || pending_export->wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) return;
    try {
        const auto result = pending_export->get();
        status_text = result.success ? "Saved annotated SVG: " + path_to_utf8_string(result.output_path)
                                     : "Annotation export unavailable: " + result.error;
        if (result.success) last_export_path = result.output_path;
    } catch (const std::exception& error) {
        status_text = std::string{"Annotation export unavailable: "} + error.what();
    }
    pending_export.reset();
}

FastActionPanelModel build_image_annotation_panel_model(const ImageAnnotationPanelState& state) {
    std::ostringstream detail;
    detail << "Image: " << path_to_utf8_string(state.document.original_image()) << '\n'
           << "Overlays: " << state.document.overlays().size() << '\n'
           << "Export: SVG only; PNG/JPG export is not available in this build.\n";
    if (!state.status_text.empty()) {
        detail << state.status_text << '\n';
    }
    FastActionPanelModel model;
    model.viewport = independent_panel_viewport("PasteIt Image Annotation##" + path_to_utf8_string(state.document.original_image()),
                                                {820.0F, 680.0F});
    model.toolbar = {
        {.id = "pen", .label = "Pen", .value = {}},
        {.id = "line", .label = "Line", .value = {}},
        {.id = "rectangle", .label = "Rectangle", .value = {}},
        {.id = "arrow", .label = "Arrow", .value = {}},
        {.id = "comment", .label = "Comment", .value = {}},
        {.id = "undo", .label = "Undo", .value = {}},
        {.id = "clear", .label = "Clear", .value = {}},
        {.id = "export_svg", .label = "Save annotated SVG", .value = {}},
        {.id = "copy_temp_path", .label = "Copy temporary image path", .value = path_to_utf8_string(state.document.original_image())},
    };
    model.status_text = state.status_text;
    model.primary_text = detail.str();
    model.annotation_section_expanded = true;
    return model;
}

#if defined(PASTIT_HAS_DESKTOP_DEPS)
void draw_image_annotation_panel(ImageAnnotationPanelState& state,
                                 unsigned int texture_id,
                                 int image_width,
                                 int image_height) {
    auto model = build_image_annotation_panel_model(state);
    state.poll_export();
    if (state.focus_pending) {
        ImGui::SetNextWindowFocus();
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    }
    ImGui::SetNextWindowSize(ImVec2(model.viewport.initial_width, model.viewport.initial_height), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(model.viewport.title.c_str(), &state.open, ImGuiWindowFlags_NoSavedSettings)) {
        if (state.focus_pending) {
            ImGui::SetWindowFocus();
            state.focus_pending = false;
        }
        const auto bottom_height = 178.0F;
        ImGui::BeginChild("annotation-canvas", ImVec2(0.0F, -bottom_height), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        const auto available = ImGui::GetContentRegionAvail();
        const auto scale = std::max(0.05F, std::min(available.x / static_cast<float>(image_width),
                                                    available.y / static_cast<float>(image_height)));
        const auto size = ImVec2(static_cast<float>(image_width) * scale, static_cast<float>(image_height) * scale);
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(intptr_t)texture_id, size);
        const bool hovered = ImGui::IsItemHovered();
        auto* draw_list = ImGui::GetWindowDrawList();
        for (const auto& overlay : state.document.overlays()) {
            draw_overlay(draw_list, overlay, origin, scale);
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            state.drag_start = from_mouse(ImGui::GetIO().MousePos, origin, scale);
            state.drawing = true;
            state.pending_pen.clear();
            state.pending_pen.push_back(state.drag_start);
        }
        if (state.drawing && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && state.active_tool == AnnotationTool::Pen) {
            state.pending_pen.push_back(from_mouse(ImGui::GetIO().MousePos, origin, scale));
        }
        if (state.drawing && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const auto end = from_mouse(ImGui::GetIO().MousePos, origin, scale);
            if (state.active_tool == AnnotationTool::Pen) {
                state.pending_pen.push_back(end);
                state.document.add_pen_stroke(state.pending_pen);
            } else if (state.active_tool == AnnotationTool::Line) {
                state.document.add_line(state.drag_start, end);
            } else if (state.active_tool == AnnotationTool::Rectangle) {
                state.document.add_rectangle(state.drag_start, end);
            } else if (state.active_tool == AnnotationTool::Arrow) {
                state.document.add_arrow(state.drag_start, end);
            }
            state.drawing = false;
        }
        ImGui::EndChild();

        if (ImGui::CollapsingHeader("Fast annotation", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto tool : {AnnotationTool::Pen, AnnotationTool::Line, AnnotationTool::Rectangle, AnnotationTool::Arrow}) {
                if (tool != AnnotationTool::Pen) ImGui::SameLine();
                const bool selected = state.active_tool == tool;
                if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8F, 0.1F, 0.1F, 1.0F));
                if (ImGui::Button(tool_label(tool).c_str())) state.active_tool = tool;
                if (selected) ImGui::PopStyleColor();
            }
            input_text_string("Comment", state.comment_text);
            ImGui::SameLine();
            if (ImGui::Button("Comment")) state.document.add_comment(state.comment_text, {24.0F, 24.0F});
            ImGui::SameLine();
            if (ImGui::Button("Undo")) (void)state.document.undo();
            ImGui::SameLine();
            if (ImGui::Button("Clear")) state.document.clear();
            if (ImGui::Button("Save annotated SVG")) {
                const auto output = default_export_path(state);
                state.start_export(output);
            }
            ImGui::SameLine();
            if (ImGui::Button("Copy temporary image path")) {
                ImGui::SetClipboardText(path_to_utf8_string(state.document.original_image()).c_str());
            }
            if (!state.status_text.empty()) copyable_text(state.status_text, true);
        }
    }
    ImGui::End();
}
#endif

}  // namespace pastit
