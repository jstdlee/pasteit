#include "test_env.hpp"
#include "history/clipboard_history_store.hpp"
#include "storage/clipboard_store.hpp"

#include <cassert>
#include <filesystem>

int main() {
    using namespace pasteit;
    const auto root = std::filesystem::temp_directory_path() / "pasteit-clipboard-prune-test";
    pasteit_test::remove_tree(root);
    ClipboardStore store(root);
    for (int index = 0; index < 12; ++index) {
        store.put({.mime_types={"text/plain"},
                   .bytes={std::byte{static_cast<unsigned char>('a' + index)}},
                   .kind=ContentKind::Text, .source_app="test", .captured_at_ms=100 + index});
    }
    store.retain_latest(10);
    assert(store.items_newest_first(50).size() == 10);

    // History coalesces a repeated copy into the newest ref; the dropped ref
    // (still held by an open action) must keep reading the same text.
    const auto first = store.put({.mime_types={"text/plain"}, .bytes={std::byte{'x'}},
                                  .kind=ContentKind::Text, .source_app="test", .captured_at_ms=200});
    const auto second = store.put({.mime_types={"text/plain"}, .bytes={std::byte{'x'}},
                                   .kind=ContentKind::Text, .source_app="test", .captured_at_ms=201});
    auto retained = store.items_newest_first(50);
    std::erase_if(retained, [&](const ClipboardItem& item) { return item.ref == first.ref; });
    store.restore(retained, store.next_ref());
    assert(store.read_text(first.ref) == "x");
    assert(store.item(first.ref)->ref == second.ref);
    // The alias survives later restores while its target does.
    store.restore(store.items_newest_first(50), store.next_ref());
    assert(store.read_text(first.ref) == "x");

    store.clear();
    assert(store.items_newest_first(50).empty());
    assert(!store.item(first.ref).has_value());
    pasteit_test::remove_tree(root);
}
