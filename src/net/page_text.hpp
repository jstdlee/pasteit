#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace pastit {

struct PageText {
    std::string title;
    std::string text;
    bool truncated = false;
};

// Readable text of a fetched page: the <article> or <main> element when
// present, without scripts, styles, navigation or markup, with entities
// decoded and block elements on their own lines. Plain text passes through.
PageText extract_page_text(std::string_view body, std::string_view content_type, std::size_t max_chars = 12000);

}  // namespace pastit
