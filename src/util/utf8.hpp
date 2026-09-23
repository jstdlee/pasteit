#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace pastit {

namespace utf8_detail {

inline bool continuation(unsigned char byte) {
    return (byte & 0xC0U) == 0x80U;
}

inline std::size_t sequence_width(std::string_view value, std::size_t offset) {
    if (offset >= value.size()) return 0;
    const auto first = static_cast<unsigned char>(value[offset]);
    if (first <= 0x7FU) return 1;
    if (first >= 0xC2U && first <= 0xDFU) {
        return offset + 1 < value.size() && continuation(static_cast<unsigned char>(value[offset + 1])) ? 2 : 0;
    }
    if (first >= 0xE0U && first <= 0xEFU) {
        if (offset + 2 >= value.size()) return 0;
        const auto second = static_cast<unsigned char>(value[offset + 1]);
        const auto third = static_cast<unsigned char>(value[offset + 2]);
        const bool second_valid = first == 0xE0U ? second >= 0xA0U && second <= 0xBFU
                                : first == 0xEDU ? second >= 0x80U && second <= 0x9FU
                                                 : continuation(second);
        return second_valid && continuation(third) ? 3 : 0;
    }
    if (first >= 0xF0U && first <= 0xF4U) {
        if (offset + 3 >= value.size()) return 0;
        const auto second = static_cast<unsigned char>(value[offset + 1]);
        const auto third = static_cast<unsigned char>(value[offset + 2]);
        const auto fourth = static_cast<unsigned char>(value[offset + 3]);
        const bool second_valid = first == 0xF0U ? second >= 0x90U && second <= 0xBFU
                                : first == 0xF4U ? second >= 0x80U && second <= 0x8FU
                                                 : continuation(second);
        return second_valid && continuation(third) && continuation(fourth) ? 4 : 0;
    }
    return 0;
}

}  // namespace utf8_detail

inline std::string sanitize_utf8(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (std::size_t offset = 0; offset < value.size();) {
        const auto width = utf8_detail::sequence_width(value, offset);
        if (width == 0) {
            output.append("\xEF\xBF\xBD");
            ++offset;
            continue;
        }
        output.append(value.substr(offset, width));
        offset += width;
    }
    return output;
}

inline std::string utf8_prefix_bytes(std::string_view value, std::size_t max_bytes) {
    std::size_t offset = 0;
    while (offset < value.size()) {
        const auto width = utf8_detail::sequence_width(value, offset);
        if (width == 0 || offset + width > max_bytes) break;
        offset += width;
    }
    return std::string{value.substr(0, offset)};
}

}  // namespace pastit
