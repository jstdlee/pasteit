#include "annotation/annotation_export.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace pasteit {
namespace {

struct ImageSize {
    float width = 0.0F;
    float height = 0.0F;
};

std::string xml_escape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&apos;"; break;
            default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

std::string base64_encode(const std::vector<unsigned char>& bytes) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t index = 0; index < bytes.size(); index += 3) {
        const std::uint32_t octet_a = bytes[index];
        const std::uint32_t octet_b = index + 1 < bytes.size() ? bytes[index + 1] : 0U;
        const std::uint32_t octet_c = index + 2 < bytes.size() ? bytes[index + 2] : 0U;
        const auto triple = (octet_a << 16U) | (octet_b << 8U) | octet_c;
        encoded.push_back(table[(triple >> 18U) & 0x3FU]);
        encoded.push_back(table[(triple >> 12U) & 0x3FU]);
        encoded.push_back(index + 1 < bytes.size() ? table[(triple >> 6U) & 0x3FU] : '=');
        encoded.push_back(index + 2 < bytes.size() ? table[triple & 0x3FU] : '=');
    }
    return encoded;
}

std::string mime_type_for(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (extension == ".jpg" || extension == ".jpeg") {
        return "image/jpeg";
    }
    if (extension == ".gif") {
        return "image/gif";
    }
    if (extension == ".webp") {
        return "image/webp";
    }
    return "image/png";
}

std::uint32_t read_be32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) {
        return 0;
    }
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

ImageSize image_size_from_png(const std::vector<unsigned char>& bytes) {
    static constexpr std::array<unsigned char, 8> signature{0x89U, 'P', 'N', 'G', '\r', '\n', 0x1AU, '\n'};
    if (bytes.size() >= 24 && std::equal(signature.begin(), signature.end(), bytes.begin())) {
        const auto width = read_be32(bytes, 16);
        const auto height = read_be32(bytes, 20);
        if (width > 0 && height > 0) {
            return {static_cast<float>(width), static_cast<float>(height)};
        }
    }
    return {};
}

ImageSize image_size_from_jpeg(const std::vector<unsigned char>& bytes) {
    if (bytes.size() < 4 || bytes[0] != 0xFFU || bytes[1] != 0xD8U) {
        return {};
    }
    for (std::size_t index = 2; index + 9 < bytes.size();) {
        if (bytes[index] != 0xFFU) {
            ++index;
            continue;
        }
        const auto marker = bytes[index + 1];
        index += 2;
        if (marker == 0xD9U || marker == 0xDAU) {
            break;
        }
        if (index + 2 > bytes.size()) {
            break;
        }
        const auto segment_length = static_cast<std::uint16_t>((bytes[index] << 8U) | bytes[index + 1]);
        if (segment_length < 2 || index + segment_length > bytes.size()) {
            break;
        }
        const bool start_of_frame = (marker >= 0xC0U && marker <= 0xC3U) ||
                                    (marker >= 0xC5U && marker <= 0xC7U) ||
                                    (marker >= 0xC9U && marker <= 0xCBU) ||
                                    (marker >= 0xCDU && marker <= 0xCFU);
        if (start_of_frame && index + 7 < bytes.size()) {
            const auto height = static_cast<std::uint16_t>((bytes[index + 3] << 8U) | bytes[index + 4]);
            const auto width = static_cast<std::uint16_t>((bytes[index + 5] << 8U) | bytes[index + 6]);
            if (width > 0 && height > 0) {
                return {static_cast<float>(width), static_cast<float>(height)};
            }
        }
        index += segment_length;
    }
    return {};
}

ImageSize image_size_from_bytes(const std::vector<unsigned char>& bytes) {
    auto size = image_size_from_png(bytes);
    if (size.width > 0.0F && size.height > 0.0F) {
        return size;
    }
    size = image_size_from_jpeg(bytes);
    if (size.width > 0.0F && size.height > 0.0F) {
        return size;
    }
    return {1024.0F, 768.0F};
}

std::string color_hex(const AnnotationColor& color) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string value = "#000000";
    value[1] = digits[(color.r >> 4U) & 0x0FU];
    value[2] = digits[color.r & 0x0FU];
    value[3] = digits[(color.g >> 4U) & 0x0FU];
    value[4] = digits[color.g & 0x0FU];
    value[5] = digits[(color.b >> 4U) & 0x0FU];
    value[6] = digits[color.b & 0x0FU];
    return value;
}

void write_stroke(std::ostream& output, const AnnotationStroke& stroke) {
    if (stroke.points.empty()) {
        return;
    }
    const auto color = color_hex(stroke.color);
    if (stroke.tool == AnnotationTool::Rectangle && stroke.points.size() >= 2) {
        const auto x = std::min(stroke.points[0].x, stroke.points[1].x);
        const auto y = std::min(stroke.points[0].y, stroke.points[1].y);
        const auto width = std::abs(stroke.points[1].x - stroke.points[0].x);
        const auto height = std::abs(stroke.points[1].y - stroke.points[0].y);
        output << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << width
               << "\" height=\"" << height << "\" fill=\"none\" stroke=\"" << color
               << "\" stroke-width=\"" << stroke.width << "\"/>\n";
        return;
    }
    if (stroke.tool == AnnotationTool::Circle && stroke.points.size() >= 2) {
        output << "<ellipse cx=\"" << (stroke.points[0].x + stroke.points[1].x) / 2.0F
               << "\" cy=\"" << (stroke.points[0].y + stroke.points[1].y) / 2.0F
               << "\" rx=\"" << std::abs(stroke.points[1].x - stroke.points[0].x) / 2.0F
               << "\" ry=\"" << std::abs(stroke.points[1].y - stroke.points[0].y) / 2.0F
               << "\" fill=\"none\" stroke=\"" << color << "\" stroke-width=\"" << stroke.width << "\"/>\n";
        return;
    }
    if ((stroke.tool == AnnotationTool::Line || stroke.tool == AnnotationTool::Arrow) && stroke.points.size() >= 2) {
        const auto& start = stroke.points[0];
        const auto& end = stroke.points[1];
        output << "<line x1=\"" << start.x << "\" y1=\"" << start.y << "\" x2=\"" << end.x << "\" y2=\"" << end.y
               << "\" stroke=\"" << color << "\" stroke-width=\"" << stroke.width << "\" stroke-linecap=\"round\"/>\n";
        if (stroke.tool == AnnotationTool::Arrow) {
            // Arrowhead as a polygon in the stroke's colour, sized with the line width.
            const float angle = std::atan2(end.y - start.y, end.x - start.x);
            const float size = std::max(12.0F, stroke.width * 4.0F);
            output << "<polygon points=\"" << end.x << ',' << end.y << ' '
                   << end.x - std::cos(angle - 0.45F) * size << ',' << end.y - std::sin(angle - 0.45F) * size << ' '
                   << end.x - std::cos(angle + 0.45F) * size << ',' << end.y - std::sin(angle + 0.45F) * size
                   << "\" fill=\"" << color << "\"/>\n";
        }
        return;
    }
    output << "<polyline points=\"";
    for (const auto& point : stroke.points) {
        output << point.x << ',' << point.y << ' ';
    }
    output << "\" fill=\"none\" stroke=\"" << color << "\" stroke-width=\"" << stroke.width
           << "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
}

void write_comment(std::ostream& output, const AnnotationComment& comment) {
    // A common UI font stack so the text looks the same in browsers and
    // image viewers rather than falling back to a terminal/serif face.
    constexpr const char* kFontStack = "'Noto Sans', 'Noto Sans CJK SC', 'Segoe UI', 'Microsoft YaHei', 'PingFang SC', Helvetica, Arial, sans-serif";
    std::vector<std::string> lines;
    std::stringstream stream(comment.text);
    for (std::string line; std::getline(stream, line);) lines.push_back(line);
    if (lines.empty()) return;
    const float size = comment.font_size;
    const float padding = size * 0.45F;
    const float line_height = size * 1.3F;
    float width = 0.0F;
    for (const auto& line : lines) width = std::max(width, annotation_text_width(line, size));
    width += padding * 2.0F;
    const float height = line_height * static_cast<float>(lines.size()) + padding * 2.0F - (line_height - size);
    output << "<g>\n<rect x=\"" << comment.anchor.x << "\" y=\"" << comment.anchor.y
           << "\" width=\"" << width << "\" height=\"" << height
           << "\" rx=\"" << size * 0.25F << "\" fill=\"" << color_hex(comment.background) << "\" fill-opacity=\"0.92\""
           << " stroke=\"" << color_hex(comment.foreground) << "\" stroke-width=\"1.5\"/>\n";
    output << "<text font-family=\"" << kFontStack << "\" font-size=\"" << size
           << "\" fill=\"" << color_hex(comment.foreground) << "\">";
    for (std::size_t index = 0; index < lines.size(); ++index) {
        output << "<tspan x=\"" << comment.anchor.x + padding << "\" y=\""
               << comment.anchor.y + padding + size * 0.85F + line_height * static_cast<float>(index) << "\">"
               << xml_escape(lines[index]) << "</tspan>";
    }
    output << "</text>\n</g>\n";
}

}  // namespace

AnnotationExportResult export_annotation_svg(const AnnotationDocument& document,
                                             const std::filesystem::path& output_path) {
    std::ifstream input(document.original_image(), std::ios::binary);
    if (!input) {
        return {.success = false, .output_path = output_path, .format = "svg", .error = "original image unavailable"};
    }
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return {.success = false, .output_path = output_path, .format = "svg", .error = "original image is empty"};
    }
    const auto size = image_size_from_bytes(bytes);
    const auto parent = output_path.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
    }
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return {.success = false, .output_path = output_path, .format = "svg", .error = "failed to open export file"};
    }

    output << "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"" << size.width << "\" height=\""
           << size.height << "\" viewBox=\"0 0 " << size.width << ' ' << size.height << "\">\n";
    output << "<image x=\"0\" y=\"0\" width=\"" << size.width << "\" height=\"" << size.height
           << "\" xlink:href=\"data:" << mime_type_for(document.original_image()) << ";base64,"
           << base64_encode(bytes) << "\"/>\n";
    for (const auto& overlay : document.overlays()) {
        if (overlay.kind == AnnotationOverlayKind::Stroke) {
            write_stroke(output, overlay.stroke);
        } else {
            write_comment(output, overlay.comment);
        }
    }
    output << "</svg>\n";
    output.close();
    if (!output) {
        return {.success = false, .output_path = output_path, .format = "svg", .error = "failed to write export file"};
    }
    return {.success = true, .output_path = output_path, .format = "svg", .error = {}};
}

}  // namespace pasteit
