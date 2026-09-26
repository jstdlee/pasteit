#pragma once

#include <string_view>

namespace pasteit {

// Bundled word lists for person-name detection. English lists are common
// given names and surnames (public-domain census frequency lists, trimmed);
// the Chinese list is the most frequent family names.
bool is_common_given_name(std::string_view word);
bool is_common_surname(std::string_view word);
bool is_chinese_surname(std::string_view character);

}  // namespace pasteit
