#include "annotation/annotation_export.hpp"
#include "annotation/image_annotation.hpp"

#include <fstream>
#include <iterator>

#include <cassert>
#include <filesystem>
#include <string>
#include <vector>

namespace {

bool is_red(const pasteit::AnnotationColor& color) {
    return color.r == 255 && color.g == 0 && color.b == 0 && color.a == 255;
}

bool is_white(const pasteit::AnnotationColor& color) {
    return color.r == 255 && color.g == 255 && color.b == 255 && color.a == 255;
}

}  // namespace

void svg_export_draws_every_overlay() {
    using namespace pasteit;
    const auto dir = std::filesystem::temp_directory_path() / "pasteit-annotation-svg-test";
    std::filesystem::create_directories(dir);
    const auto image = dir / "shot.png";
    // PNG signature + IHDR with a 40 x 30 size is all the exporter reads.
    const unsigned char png[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R',
                                 0, 0, 0, 40, 0, 0, 0, 30, 8, 6, 0, 0, 0};
    std::ofstream(image, std::ios::binary).write(reinterpret_cast<const char*>(png), sizeof(png));
    AnnotationDocument doc{image};
    doc.add_circle({2, 2}, {12, 8});
    doc.add_stroke({.tool = AnnotationTool::Arrow, .points = {{1, 1}, {20, 20}}, .width = 2, .color = {40, 110, 230, 255}});
    doc.add_comment("Fix <this>\n\xE4\xBD\xA0\xE5\xA5\xBD", {5, 5}, {40, 170, 70, 255}, 20.0F);
    doc.add_comment("", {0, 0}, {0, 0, 0, 255}, 20.0F);  // empty text is ignored
    assert(doc.comments().size() == 1 && doc.comments().front().font_size == 20.0F);
    const auto out = dir / "out.svg";
    const auto result = export_annotation_svg(doc, out);
    assert(result.success);
    std::ifstream in(out);
    const std::string svg{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    assert(svg.find("width=\"40\" height=\"30\"") != std::string::npos);
    assert(svg.find("<ellipse cx=\"7\" cy=\"5\" rx=\"5\" ry=\"3\"") != std::string::npos);
    assert(svg.find("<polygon") != std::string::npos && svg.find("#286EE6") != std::string::npos);  // arrowhead in stroke colour
    assert(svg.find("Fix &lt;this&gt;</tspan>") != std::string::npos);
    assert(svg.find("\xE4\xBD\xA0\xE5\xA5\xBD</tspan>") != std::string::npos);
    assert(svg.find("'Noto Sans'") != std::string::npos && svg.find("sans-serif") != std::string::npos);
    assert(svg.find("xlink:href=\"data:image/png;base64,") != std::string::npos);
    // Wide characters count as a full em.
    assert(annotation_text_width("\xE4\xBD\xA0\xE5\xA5\xBD", 10.0F) == 20.0F);
    std::filesystem::remove_all(dir);
}

int main() {
    svg_export_draws_every_overlay();
    using namespace pasteit;

    {
        AnnotationDocument doc("/tmp/original.png");
        doc.add_line({0.0F, 0.0F}, {10.0F, 10.0F});
        doc.add_comment("note", {5.0F, 5.0F});
        assert(doc.overlays().size() == 2);
        assert(doc.undo());
        assert(doc.comments().empty());
        assert(doc.strokes().size() == 1);
        assert(doc.strokes().front().tool == AnnotationTool::Line);
        assert(is_red(doc.strokes().front().color));
    }

    {
        AnnotationDocument doc("/tmp/original.png");
        doc.add_pen_stroke({{0.0F, 0.0F}, {1.0F, 1.0F}, {2.0F, 1.0F}});
        doc.add_rectangle({1.0F, 2.0F}, {20.0F, 30.0F});
        doc.add_arrow({2.0F, 3.0F}, {40.0F, 50.0F});
        doc.add_comment("callout", {3.0F, 4.0F});

        const auto request = doc.export_request("/tmp/annotated.png", "png");
        assert(request.original_image == "/tmp/original.png");
        assert(request.output_image == "/tmp/annotated.png");
        assert(request.format == "png");
        assert(request.overlays.size() == 4);
        assert(request.overlays.back().kind == AnnotationOverlayKind::Comment);
        assert(is_white(request.overlays.back().comment.background));
        assert(request.overlays.back().comment.text == "callout");
        assert(doc.strokes().size() == 3);
        for (const auto& stroke : doc.strokes()) {
            assert(is_red(stroke.color));
        }

        doc.clear();
        assert(doc.overlays().empty());
        assert(doc.strokes().empty());
        assert(doc.comments().empty());
        assert(!doc.undo());
    }
}
