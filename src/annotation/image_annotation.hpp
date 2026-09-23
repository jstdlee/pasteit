#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pastit {

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

enum class AnnotationTool { Pen, Line, Rectangle, Arrow };
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
    void add_comment(std::string text, AnnotationPoint anchor);

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
AnnotationColor annotation_white();

}  // namespace pastit
