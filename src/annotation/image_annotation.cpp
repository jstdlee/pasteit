#include "annotation/image_annotation.hpp"

#include <algorithm>
#include <cctype>

#include <utility>

namespace pasteit {
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

void AnnotationDocument::add_circle(AnnotationPoint corner, AnnotationPoint opposite, float width) {
    add_stroke(stroke_from(AnnotationTool::Circle, {corner, opposite}, width));
}

void AnnotationDocument::add_comment(std::string text, AnnotationPoint anchor, AnnotationColor color, float font_size) {
    if (text.empty()) return;
    overlays_.push_back(AnnotationOverlay{
        .kind = AnnotationOverlayKind::Comment,
        .stroke = {},
        .comment = AnnotationComment{
            .text = std::move(text),
            .anchor = anchor,
            .foreground = color,
            .background = annotation_white(),
            .font_size = std::clamp(font_size, 8.0F, 96.0F),
        },
    });
}

float annotation_text_width(std::string_view line, float font_size) {
    float ems = 0.0F;
    for (std::size_t index = 0; index < line.size();) {
        const auto byte = static_cast<unsigned char>(line[index]);
        const std::size_t length = byte < 0x80 ? 1 : byte >= 0xF0 ? 4 : byte >= 0xE0 ? 3 : byte >= 0xC0 ? 2 : 1;
        // Three- and four-byte UTF-8 sequences are mostly CJK or emoji: full width.
        ems += length >= 3 ? 1.0F : std::isupper(byte) ? 0.66F : 0.56F;
        index += length;
    }
    return ems * font_size;
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

bool AnnotationDocument::move_overlay(std::size_t index, float dx, float dy) {
    if (index >= overlays_.size()) return false;
    auto& overlay = overlays_[index];
    if (overlay.kind == AnnotationOverlayKind::Comment) {
        overlay.comment.anchor.x += dx;
        overlay.comment.anchor.y += dy;
    } else {
        for (auto& point : overlay.stroke.points) {
            point.x += dx;
            point.y += dy;
        }
    }
    return true;
}

bool AnnotationDocument::erase_overlay(std::size_t index) {
    if (index >= overlays_.size()) return false;
    overlays_.erase(overlays_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool AnnotationDocument::set_overlay_color(std::size_t index, AnnotationColor color) {
    if (index >= overlays_.size()) return false;
    auto& overlay = overlays_[index];
    if (overlay.kind == AnnotationOverlayKind::Comment) overlay.comment.foreground = color;
    else overlay.stroke.color = color;
    return true;
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

}  // namespace pasteit
