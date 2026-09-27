#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pasteit {

struct EmailMatch {
    std::size_t position = 0;
    std::string value;
};

// Linear-time equivalent of the regex
//   [A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}
// (non-overlapping, left to right). The regex backtracks quadratically on
// long runs of letters, which MSVC's std::regex rejects with error_complexity.
std::vector<EmailMatch> find_emails(std::string_view text, std::size_t limit = static_cast<std::size_t>(-1));
bool contains_email(std::string_view text);

}  // namespace pasteit
