#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pasteit {

enum class DiffRowKind { Same, Changed, Removed, Added };

// Byte range [first, second) inside a line that differs from the other side.
using DiffSpan = std::pair<std::size_t, std::size_t>;

// One row of a side-by-side diff. Removed rows have no right line, Added
// rows no left line; Changed rows pair a removed line with an added one and
// mark the differing characters on both sides.
struct DiffRow {
    DiffRowKind kind = DiffRowKind::Same;
    int left_number = 0;   // 1-based, 0 when absent
    int right_number = 0;
    std::string left;
    std::string right;
    std::vector<DiffSpan> left_spans;
    std::vector<DiffSpan> right_spans;
};

struct TextDiff {
    std::vector<DiffRow> rows;
    std::size_t added = 0;    // lines only on the right (including the right half of changed rows)
    std::size_t removed = 0;  // lines only on the left
    bool approximate = false;  // inputs too large for a full line diff
};

// Line diff (longest common subsequence) laid out side by side. Runs of
// removed and added lines are paired into Changed rows, which get a
// character-level diff on UTF-8 boundaries.
TextDiff diff_texts(std::string_view left, std::string_view right);

}  // namespace pasteit
