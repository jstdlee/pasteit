#include "app/renderer_result_state.hpp"

#include <utility>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>
#include <vector>
#if defined(PASTEIT_HAS_ZLIB)
#include <zlib.h>
#endif

namespace pasteit {

void RendererResultState::prepare(RendererResult result) {
    active_ = std::move(result);
}

void RendererResultState::fail(std::string action_id, RendererResultKind kind, std::string source, std::string error) {
    active_ = RendererResult{
        .action_id = std::move(action_id),
        .kind = kind,
        .source = std::move(source),
        .payload = {},
        .available = false,
        .status = RendererResultStatus::Failed,
        .output_path = std::nullopt,
        .error = std::move(error),
    };
}

std::string RendererResultState::copy_text() const {
    if (!active_.payload.empty()) return active_.payload;
    if (!active_.source.empty()) return active_.source;
    return active_.generated_mermaid ? active_.generated_mermaid->original_clipboard_source : std::string{};
}

namespace {

std::uint32_t be32(const unsigned char* value) {
    return (std::uint32_t(value[0]) << 24U) | (std::uint32_t(value[1]) << 16U) |
           (std::uint32_t(value[2]) << 8U) | std::uint32_t(value[3]);
}

bool png_decodes(const std::vector<unsigned char>& data) {
    constexpr std::array<unsigned char, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (data.size() < 45 || !std::equal(signature.begin(), signature.end(), data.begin())) return false;
#if !defined(PASTEIT_HAS_ZLIB)
    if (be32(data.data() + 8) != 13 || !std::equal(data.data() + 12, data.data() + 16, "IHDR")) {
        return false;
    }
    const auto width = be32(data.data() + 16);
    const auto height = be32(data.data() + 20);
    return width != 0 && height != 0;
#else
    std::size_t offset = 8;
    std::uint32_t width = 0, height = 0;
    unsigned bit_depth = 0, channels = 0;
    bool ended = false;
    std::vector<unsigned char> compressed;
    while (offset + 12 <= data.size()) {
        const auto length = be32(data.data() + offset);
        if (length > data.size() - offset - 12) return false;
        const auto* type = data.data() + offset + 4;
        const auto* payload = type + 4;
        const auto stored_crc = be32(payload + length);
        const auto actual_crc = crc32(0, type, 4 + length);
        if (stored_crc != actual_crc) return false;
        if (std::equal(type, type + 4, "IHDR")) {
            if (offset != 8 || length != 13) return false;
            width = be32(payload);
            height = be32(payload + 4);
            bit_depth = payload[8];
            switch (payload[9]) {
                case 0: channels = 1; break;
                case 2: channels = 3; break;
                case 3: channels = 1; break;
                case 4: channels = 2; break;
                case 6: channels = 4; break;
                default: return false;
            }
            if (payload[12] != 0 || payload[10] != 0 || payload[11] != 0) return false;
        } else if (std::equal(type, type + 4, "IDAT")) {
            compressed.insert(compressed.end(), payload, payload + length);
        } else if (std::equal(type, type + 4, "IEND")) {
            ended = length == 0;
            break;
        }
        offset += 12 + length;
    }
    if (!ended || !width || !height || compressed.empty() || !channels ||
        (bit_depth != 1 && bit_depth != 2 && bit_depth != 4 && bit_depth != 8 && bit_depth != 16)) return false;
    const auto row_bytes = (std::uint64_t(width) * channels * bit_depth + 7) / 8;
    const auto expected = std::uint64_t(height) * (row_bytes + 1);
    if (expected > 256U * 1024U * 1024U || expected > std::numeric_limits<uLongf>::max()) return false;
    std::vector<unsigned char> decoded(static_cast<std::size_t>(expected));
    uLongf decoded_size = static_cast<uLongf>(decoded.size());
    if (uncompress(decoded.data(), &decoded_size, compressed.data(), compressed.size()) != Z_OK ||
        decoded_size != expected) return false;
    for (std::uint32_t row = 0; row < height; ++row) {
        if (decoded[static_cast<std::size_t>(row) * static_cast<std::size_t>(row_bytes + 1)] > 4) return false;
    }
    return true;
#endif
}

}  // namespace

bool rendered_output_decodes(RendererResultKind kind, const std::filesystem::path& path) {
    try {
        if (!std::filesystem::is_regular_file(path)) return false;
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        const std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)), {});
        if (kind == RendererResultKind::Qr ||
            (kind == RendererResultKind::Mermaid && path.extension() == ".png")) return png_decodes(data);
        if (kind == RendererResultKind::Mermaid) {
            const std::string text(data.begin(), data.end());
            return text.starts_with("<!doctype html>") &&
                   text.find("<pre class=\"mermaid\">") != std::string::npos &&
                   text.find("mermaid.initialize") != std::string::npos &&
                   text.find("mermaid.run()") != std::string::npos &&
                   text.find("</html>") != std::string::npos &&
                   text.find("<script src=") == std::string::npos;
        }
    } catch (const std::exception&) {
        return false;
    }
    return false;
}

}  // namespace pasteit
