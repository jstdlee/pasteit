#include "render/renderer_service.hpp"
#include "platform/app_paths.hpp"
#include "util/path_utf8.hpp"
#include "qrcodegen.hpp"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pasteit {
namespace {

std::string trim_copy(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }
    return std::string{value.substr(first, last - first)};
}

bool starts_with(std::string_view value, std::string_view prefix) {
    return value.substr(0, prefix.size()) == prefix;
}

bool is_mermaid_header(std::string_view line) {
    const auto trimmed = trim_copy(line);
    static constexpr std::string_view headers[] = {
        "flowchart", "graph", "sequenceDiagram", "classDiagram", "stateDiagram", "stateDiagram-v2",
        "erDiagram", "journey", "gantt", "pie", "mindmap", "timeline", "gitGraph",
        "quadrantChart", "requirementDiagram", "C4Context",
    };
    return std::any_of(std::begin(headers), std::end(headers), [&](std::string_view header) {
        return starts_with(trimmed, header);
    });
}

std::string ensure_trailing_newline(std::string value) {
    if (!value.empty() && value.back() != '\n') {
        value.push_back('\n');
    }
    return value;
}

std::string after_first_header(std::string_view source) {
    std::istringstream lines(std::string{source});
    std::string line;
    std::string output;
    bool found_header = false;
    while (std::getline(lines, line)) {
        if (!found_header) {
            if (!is_mermaid_header(line)) {
                continue;
            }
            found_header = true;
        }
        output += line;
        output.push_back('\n');
    }
    return found_header ? output : ensure_trailing_newline(trim_copy(source));
}

bool normalized_starts_with_header(std::string_view source) {
    std::istringstream lines(std::string{source});
    std::string first_line;
    return std::getline(lines, first_line) && is_mermaid_header(first_line);
}

std::string fenced_body(std::string_view source) {
    std::size_t search = 0;
    while (true) {
        const auto fence = source.find("```", search);
        if (fence == std::string_view::npos) {
            return {};
        }
        const auto info_start = fence + 3;
        const auto body_start = source.find('\n', info_start);
        if (body_start == std::string_view::npos) {
            return {};
        }
        const auto info = trim_copy(source.substr(info_start, body_start - info_start));
        const auto body_end = source.find("```", body_start + 1);
        const auto body = source.substr(body_start + 1, body_end == std::string_view::npos
                                                        ? std::string_view::npos
                                                        : body_end - body_start - 1);
        if ((info.empty() || info == "mermaid" || normalized_starts_with_header(body)) &&
            !trim_copy(body).empty()) {
            return ensure_trailing_newline(std::string{body});
        }
        if (body_end == std::string_view::npos) {
            return {};
        }
        search = body_end + 3;
    }
}

void create_parent_directory(const std::filesystem::path& output) {
    const auto parent = output.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
    }
}

std::string html_escape(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&#39;"; break;
        default: result.push_back(ch); break;
        }
    }
    return result;
}

std::string inline_script(std::string script) {
    // A literal closing tag inside the bundle must not terminate the HTML script element.
    for (std::size_t pos = 0; (pos = script.find("</script", pos)) != std::string::npos; pos += 9) {
        script.replace(pos, 8, "<\\/script");
    }
    return script;
}

qrcodegen::QrCode::Ecc qr_ecc(std::string_view value) {
    if (value == "L") return qrcodegen::QrCode::Ecc::LOW;
    if (value == "Q") return qrcodegen::QrCode::Ecc::QUARTILE;
    if (value == "H") return qrcodegen::QrCode::Ecc::HIGH;
    return qrcodegen::QrCode::Ecc::MEDIUM;
}

void png_write_callback(void* context, void* data, int size) {
    static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
}

}  // namespace

ExternalRendererService::ExternalRendererService(FastActionServices& services, RendererSettings settings)
    : services_(services), settings_(std::move(settings)) {}

std::string ExternalRendererService::normalize_mermaid_source(std::string_view source) {
    const auto fenced = fenced_body(source);
    if (!fenced.empty()) {
        return fenced;
    }
    return after_first_header(source);
}

RenderResult ExternalRendererService::render_mermaid(std::string_view source, const std::filesystem::path& output) {
    create_parent_directory(output);
    auto input_path = output;
    input_path.replace_extension(".mmd");
    auto png_path = output;
    png_path.replace_extension(".png");
    {
        std::ofstream input(input_path, std::ios::binary | std::ios::trunc);
        if (input) {
            input << normalize_mermaid_source(source);
            input.close();
            std::vector<std::string> argv{
                settings_.mermaid_cli_path.empty() ? "mmdc" : path_to_utf8_string(settings_.mermaid_cli_path),
                "-i", path_to_utf8_string(input_path), "-o", path_to_utf8_string(png_path),
            };
            argv.insert(argv.end(), settings_.mermaid_arguments.begin(), settings_.mermaid_arguments.end());
            const auto process = services_.run_argv(argv);
            std::error_code ignored;
            std::filesystem::remove(input_path, ignored);
            if (process.exit_code == 0 && std::filesystem::is_regular_file(png_path)) {
                return {.available = true, .success = true, .output = png_path,
                        .status = "Mermaid PNG ready"};
            }
            std::filesystem::remove(png_path, ignored);
        }
    }
    std::ifstream bundle(executable_directory() / "mermaid.min.js", std::ios::binary);
    if (!bundle) return {.available = false, .success = false, .output = output,
                         .status = "Bundled Mermaid JavaScript unavailable"};
    const std::string script((std::istreambuf_iterator<char>(bundle)), {});
    if (script.empty() || bundle.bad()) return {.available = false, .success = false, .output = output,
                                                .status = "Bundled Mermaid JavaScript could not be read"};
    std::ofstream page(output, std::ios::binary | std::ios::trunc);
    if (!page) return {.available = true, .success = false, .output = output,
                       .status = "Could not open Mermaid HTML output"};
    page << "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
            "<title>PasteIt Mermaid</title><style>body{font-family:sans-serif;margin:2rem}"
            "#error{color:#b00020;white-space:pre-wrap}</style></head><body>"
            "<pre class=\"mermaid\">" << html_escape(normalize_mermaid_source(source)) << "</pre>"
            "<div id=\"error\" role=\"alert\"></div><script>" << inline_script(script)
         << "</script><script>mermaid.initialize({startOnLoad:false,securityLevel:'strict'});"
            "mermaid.run().catch(e=>{document.getElementById('error').textContent=String(e)})"
            "</script></body></html>";
    page.close();
    return {.available = true, .success = static_cast<bool>(page), .output = output,
            .status = page ? "Offline Mermaid HTML ready" : "Could not write Mermaid HTML"};
}

RenderResult ExternalRendererService::render_qr(std::string_view payload, const std::filesystem::path& output) {
    create_parent_directory(output);
    try {
        if (payload.empty()) return {.available = true, .success = false, .output = output,
                                     .status = "QR payload is empty"};
        const std::vector<std::uint8_t> bytes(payload.begin(), payload.end());
        const auto code = qrcodegen::QrCode::encodeBinary(bytes, qr_ecc(settings_.qr_error_correction));
        const int margin = std::clamp(settings_.qr_margin, 0, 64);
        const int scale = std::clamp(settings_.qr_scale, 1, 64);
        const int pixels = (code.getSize() + margin * 2) * scale;
        if (pixels > 8192) return {.available = true, .success = false, .output = output,
                                   .status = "QR image would exceed 8192 pixels; lower the scale"};
        std::vector<std::uint8_t> raster(static_cast<std::size_t>(pixels) * pixels, 255);
        for (int y = 0; y < pixels; ++y) {
            for (int x = 0; x < pixels; ++x) {
                const int module_x = x / scale - margin;
                const int module_y = y / scale - margin;
                if (module_x >= 0 && module_x < code.getSize() &&
                    module_y >= 0 && module_y < code.getSize() && code.getModule(module_x, module_y)) {
                    raster[static_cast<std::size_t>(y) * pixels + x] = 0;
                }
            }
        }
        std::ofstream image(output, std::ios::binary | std::ios::trunc);
        if (!image) return {.available = true, .success = false, .output = output,
                            .status = "Could not open QR PNG output"};
        const bool encoded = stbi_write_png_to_func(png_write_callback, &image, pixels, pixels, 1,
                                                    raster.data(), pixels) != 0;
        image.close();
        return {.available = true, .success = encoded && static_cast<bool>(image), .output = output,
                .status = encoded && image ? "QR PNG ready" : "Could not write QR PNG"};
    } catch (const std::exception& error) {
        return {.available = true, .success = false, .output = output,
                .status = std::string{"QR encoding failed: "} + error.what()};
    }
}

}  // namespace pasteit
