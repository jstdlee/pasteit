#include "annotation/image_annotation.hpp"

#include <utility>

namespace pastit {
namespace {

AnnotationStroke stroke_from(AnnotationTool tool,
                             std::vector<AnnotationPoint> points,
                             float width) {
    return AnnotationStroke{
        .tool = tool,
        .points = std::move(points),
        .width = width,
        .color = annotation_red(),
    };
}

}  // namespace

AnnotationColor annotation_red() {
    return AnnotationColor{255, 0, 0, 255};
}

AnnotationColor annotation_white() {
    return AnnotationColor{255, 255, 255, 255};
}

AnnotationDocument::AnnotationDocument(std::filesystem::path original_image)
    : original_image_(std::move(original_image)) {}

void AnnotationDocument::set_original_image(std::filesystem::path original_image) {
    original_image_ = std::move(original_image);
}

void AnnotationDocument::add_stroke(AnnotationStroke stroke) {
    if (stroke.points.empty()) {
        return;
    }
    if (stroke.color.a == 0) {
        stroke.color = annotation_red();
    }
    overlays_.push_back(AnnotationOverlay{
        .kind = AnnotationOverlayKind::Stroke,
        .stroke = std::move(stroke),
        .comment = {},
    });
}

void AnnotationDocument::add_pen_stroke(std::vector<AnnotationPoint> points, float width) {
    add_stroke(stroke_from(AnnotationTool::Pen, std::move(points), width));
}

void AnnotationDocument::add_line(AnnotationPoint start, AnnotationPoint end, float width) {
    add_stroke(stroke_from(AnnotationTool::Line, {start, end}, width));
}

void AnnotationDocument::add_rectangle(AnnotationPoint top_left, AnnotationPoint bottom_right, float width) {
    add_stroke(stroke_from(AnnotationTool::Rectangle, {top_left, bottom_right}, width));
}

void AnnotationDocument::add_arrow(AnnotationPoint start, AnnotationPoint end, float width) {
    add_stroke(stroke_from(AnnotationTool::Arrow, {start, end}, width));
}

void AnnotationDocument::add_comment(std::string text, AnnotationPoint anchor) {
    overlays_.push_back(AnnotationOverlay{
        .kind = AnnotationOverlayKind::Comment,
        .stroke = {},
        .comment = AnnotationComment{
            .text = std::move(text),
            .anchor = anchor,
            .foreground = AnnotationColor{0, 0, 0, 255},
            .background = annotation_white(),
        },
    });
}

bool AnnotationDocument::undo() {
    if (overlays_.empty()) {
        return false;
    }
    overlays_.pop_back();
    return true;
}

void AnnotationDocument::clear() {
    overlays_.clear();
}

std::vector<AnnotationStroke> AnnotationDocument::strokes() const {
    std::vector<AnnotationStroke> result;
    for (const auto& overlay : overlays_) {
        if (overlay.kind == AnnotationOverlayKind::Stroke) {
            result.push_back(overlay.stroke);
        }
    }
    return result;
}

std::vector<AnnotationComment> AnnotationDocument::comments() const {
    std::vector<AnnotationComment> result;
    for (const auto& overlay : overlays_) {
        if (overlay.kind == AnnotationOverlayKind::Comment) {
            result.push_back(overlay.comment);
        }
    }
    return result;
}

AnnotationExportRequest AnnotationDocument::export_request(std::filesystem::path output_image,
                                                           std::string format) const {
    return AnnotationExportRequest{
        .original_image = original_image_,
        .output_image = std::move(output_image),
        .format = std::move(format),
        .overlays = overlays_,
    };
}

}  // namespace pastit
