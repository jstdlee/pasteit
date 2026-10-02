#include "util/text_diff.hpp"

#include <algorithm>
#include <cstdint>

namespace pasteit {
namespace {

// Above this many LCS cells the middle is shown as one removed/added block.
constexpr std::size_t kMaxLineCells = 4'000'000;
constexpr std::size_t kMaxCharCells = 250'000;

std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> lines;
    if (text.empty()) return lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        auto line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        if (end == std::string_view::npos) break;
        start = end + 1;
        if (start == text.size()) break;  // a trailing newline does not open another line
    }
    return lines;
}

enum class Op { Same, Remove, Add };

// Edit script between a and b by longest common subsequence, after trimming
// the common prefix and suffix. Falls back to remove-all / add-all for the
// middle when it is too large.
template <typename T>
std::vector<Op> edit_script(const std::vector<T>& a, const std::vector<T>& b, std::size_t max_cells, bool& approximate) {
    std::size_t prefix = 0;
    while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix]) ++prefix;
    std::size_t suffix = 0;
    while (suffix < a.size() - prefix && suffix < b.size() - prefix &&
           a[a.size() - 1 - suffix] == b[b.size() - 1 - suffix]) {
        ++suffix;
    }
    const std::size_t n = a.size() - prefix - suffix;
    const std::size_t m = b.size() - prefix - suffix;
    std::vector<Op> ops(prefix, Op::Same);
    if (n == 0 || m == 0 || (n + 1) * (m + 1) > max_cells) {
        if (n > 0 && m > 0) approximate = true;
        ops.insert(ops.end(), n, Op::Remove);
        ops.insert(ops.end(), m, Op::Add);
    } else {
        // table[i][j] = LCS length of a[i..] and b[j..] (middle part only).
        std::vector<std::uint32_t> table((n + 1) * (m + 1), 0);
        const auto at = [&](std::size_t i, std::size_t j) -> std::uint32_t& { return table[i * (m + 1) + j]; };
        for (std::size_t i = n; i-- > 0;) {
            for (std::size_t j = m; j-- > 0;) {
                at(i, j) = a[prefix + i] == b[prefix + j] ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));
            }
        }
        std::size_t i = 0;
        std::size_t j = 0;
        while (i < n && j < m) {
            if (a[prefix + i] == b[prefix + j]) {
                ops.push_back(Op::Same);
                ++i;
                ++j;
            } else if (at(i + 1, j) >= at(i, j + 1)) {
                ops.push_back(Op::Remove);
                ++i;
            } else {
                ops.push_back(Op::Add);
                ++j;
            }
        }
        ops.insert(ops.end(), n - i, Op::Remove);
        ops.insert(ops.end(), m - j, Op::Add);
    }
    ops.insert(ops.end(), suffix, Op::Same);
    return ops;
}

// Code points as byte offsets, so spans never split a UTF-8 sequence.
std::vector<std::string_view> code_points(std::string_view text) {
    std::vector<std::string_view> points;
    for (std::size_t index = 0; index < text.size();) {
        std::size_t length = 1;
        while (index + length < text.size() && (static_cast<unsigned char>(text[index + length]) & 0xC0U) == 0x80U) ++length;
        points.push_back(text.substr(index, length));
        index += length;
    }
    return points;
}

void append_span(std::vector<DiffSpan>& spans, std::size_t first, std::size_t last) {
    if (first >= last) return;
    if (!spans.empty() && spans.back().second == first) {
        spans.back().second = last;
    } else {
        spans.emplace_back(first, last);
    }
}

void mark_changes(DiffRow& row) {
    const auto left = code_points(row.left);
    const auto right = code_points(row.right);
    bool approximate = false;
    const auto ops = edit_script(left, right, kMaxCharCells, approximate);
    std::size_t left_byte = 0;
    std::size_t right_byte = 0;
    std::size_t li = 0;
    std::size_t ri = 0;
    for (const auto op : ops) {
        if (op == Op::Same) {
            left_byte += left[li++].size();
            right_byte += right[ri++].size();
        } else if (op == Op::Remove) {
            append_span(row.left_spans, left_byte, left_byte + left[li].size());
            left_byte += left[li++].size();
        } else {
            append_span(row.right_spans, right_byte, right_byte + right[ri].size());
            right_byte += right[ri++].size();
        }
    }
}

}  // namespace

TextDiff diff_texts(std::string_view left, std::string_view right) {
    TextDiff diff;
    const auto a = split_lines(left);
    const auto b = split_lines(right);
    const auto ops = edit_script(a, b, kMaxLineCells, diff.approximate);
    std::size_t i = 0;
    std::size_t j = 0;
    for (std::size_t index = 0; index < ops.size();) {
        if (ops[index] == Op::Same) {
            diff.rows.push_back({.kind = DiffRowKind::Same, .left_number = static_cast<int>(i + 1),
                                 .right_number = static_cast<int>(j + 1), .left = std::string{a[i]}, .right = std::string{b[j]}});
            ++i;
            ++j;
            ++index;
            continue;
        }
        // A run of removals and additions between two equal lines.
        std::vector<std::size_t> removed;
        std::vector<std::size_t> added;
        while (index < ops.size() && ops[index] != Op::Same) {
            if (ops[index] == Op::Remove) removed.push_back(i++);
            else added.push_back(j++);
            ++index;
        }
        const auto paired = std::min(removed.size(), added.size());
        for (std::size_t k = 0; k < std::max(removed.size(), added.size()); ++k) {
            DiffRow row;
            if (k < removed.size()) {
                row.left_number = static_cast<int>(removed[k] + 1);
                row.left = std::string{a[removed[k]]};
            }
            if (k < added.size()) {
                row.right_number = static_cast<int>(added[k] + 1);
                row.right = std::string{b[added[k]]};
            }
            if (k < paired) {
                row.kind = DiffRowKind::Changed;
                mark_changes(row);
            } else {
                row.kind = k < removed.size() ? DiffRowKind::Removed : DiffRowKind::Added;
                if (row.kind == DiffRowKind::Removed) row.left_spans.emplace_back(0, row.left.size());
                else row.right_spans.emplace_back(0, row.right.size());
            }
            diff.rows.push_back(std::move(row));
        }
        diff.removed += removed.size();
        diff.added += added.size();
    }
    return diff;
}

}  // namespace pasteit
