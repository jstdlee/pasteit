#include "util/text_diff.hpp"

#include <cassert>
#include <string>

using namespace pasteit;

int main() {
    // Identical texts: every row is the same, nothing counted.
    const auto same = diff_texts("a\nb\n", "a\nb\n");
    assert(same.rows.size() == 2 && same.added == 0 && same.removed == 0);
    assert(same.rows[1].kind == DiffRowKind::Same && same.rows[1].left_number == 2 && same.rows[1].right_number == 2);

    // A changed line pairs left and right and marks only the differing characters.
    const auto changed = diff_texts("name: Ada\nrole: dev", "name: Ada\nrole: admin");
    assert(changed.rows.size() == 2 && changed.rows[1].kind == DiffRowKind::Changed);
    const auto& row = changed.rows[1];
    assert(!row.left_spans.empty() && !row.right_spans.empty());
    assert(row.left_spans.front().first >= 6 && row.right_spans.front().first >= 6);  // "role: " is common
    assert(row.right.substr(row.right_spans.back().first, row.right_spans.back().second - row.right_spans.back().first).find('m') !=
           std::string::npos);

    // Pure insertions and deletions keep their own side empty.
    const auto inserted = diff_texts("a\nc", "a\nb\nc");
    assert(inserted.rows.size() == 3 && inserted.rows[1].kind == DiffRowKind::Added);
    assert(inserted.rows[1].left_number == 0 && inserted.rows[1].right == "b" && inserted.added == 1);
    const auto deleted = diff_texts("a\nb\nc", "a\nc");
    assert(deleted.rows[1].kind == DiffRowKind::Removed && deleted.rows[1].left == "b" && deleted.removed == 1);

    // Pretty-printing: one line becomes several; the first pairs as changed.
    const auto pretty = diff_texts("{\"a\":1}", "{\n  \"a\": 1\n}");
    assert(pretty.rows.size() == 3 && pretty.rows[0].kind == DiffRowKind::Changed);
    assert(pretty.rows[1].kind == DiffRowKind::Added && pretty.added == 3 && pretty.removed == 1);

    // Spans stay on UTF-8 boundaries.
    const auto utf8 = diff_texts("caf\xC3\xA9 au lait", "caf\xC3\xA8 au lait");
    const auto& span = utf8.rows[0].left_spans.front();
    assert(span.first == 3 && span.second == 5);

    // Empty sides and CRLF line ends.
    assert(diff_texts("", "x").rows.size() == 1 && diff_texts("", "x").rows[0].kind == DiffRowKind::Added);
    assert(diff_texts("a\r\nb", "a\nb").added == 0);
}
