#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pasteit {

struct AnnotationPoint {
    float x = 0.0F;
    float y = 0.0F;
};

struct AnnotationColor {
    std::uint8_t r = 255;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

enum class AnnotationTool { Pen, Line, Rectangle, Arrow, Circle };
enum class AnnotationOverlayKind { Stroke, Comment };

struct AnnotationStroke {
    AnnotationTool tool = AnnotationTool::Pen;
    std::vector<AnnotationPoint> points;
    float width = 3.0F;
    AnnotationColor color;
};

struct AnnotationComment {
    std::string text;
    AnnotationPoint anchor;
    AnnotationColor foreground{0, 0, 0, 255};
    AnnotationColor background{255, 255, 255, 255};
    float font_size = 18.0F;  // image pixels
};

struct AnnotationOverlay {
    AnnotationOverlayKind kind = AnnotationOverlayKind::Stroke;
    AnnotationStroke stroke;
    AnnotationComment comment;
};

struct AnnotationExportRequest {
    std::filesystem::path original_image;
    std::filesystem::path output_image;
    std::string format = "png";
    std::vector<AnnotationOverlay> overlays;
};

class AnnotationDocument {
public:
    AnnotationDocument() = default;
    explicit AnnotationDocument(std::filesystem::path original_image);

    void set_original_image(std::filesystem::path original_image);
    const std::filesystem::path& original_image() const { return original_image_; }

    void add_stroke(AnnotationStroke stroke);
    void add_pen_stroke(std::vector<AnnotationPoint> points, float width = 3.0F);
    void add_line(AnnotationPoint start, AnnotationPoint end, float width = 3.0F);
    void add_rectangle(AnnotationPoint top_left, AnnotationPoint bottom_right, float width = 3.0F);
    void add_arrow(AnnotationPoint start, AnnotationPoint end, float width = 3.0F);
    // Circle: an ellipse inscribed in the box from corner to corner.
    void add_circle(AnnotationPoint corner, AnnotationPoint opposite, float width = 3.0F);
    void add_comment(std::string text, AnnotationPoint anchor);
    void add_comment(std::string text, AnnotationPoint anchor, AnnotationColor color, float font_size);

    bool undo();
    void clear();

    const std::vector<AnnotationOverlay>& overlays() const { return overlays_; }
    std::vector<AnnotationStroke> strokes() const;
    std::vector<AnnotationComment> comments() const;
    AnnotationExportRequest export_request(std::filesystem::path output_image, std::string format) const;

private:
    std::filesystem::path original_image_;
    std::vector<AnnotationOverlay> overlays_;
};

AnnotationColor annotation_red();
// Estimated rendered width of one line of comment text (CJK and other wide
// characters count as a full em). Shared by the canvas and the SVG export.
float annotation_text_width(std::string_view line, float font_size);
AnnotationColor annotation_white();

}  // namespace pasteit
