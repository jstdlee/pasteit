#include "annotation/image_annotation.hpp"

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

int main() {
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
