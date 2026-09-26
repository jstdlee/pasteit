#include "history/clipboard_history_store.hpp"
#include "storage/clipboard_store.hpp"

#include <cassert>
#include <filesystem>

int main() {
    using namespace pasteit;
    const auto root = std::filesystem::temp_directory_path() / "pasteit-clipboard-prune-test";
    std::filesystem::remove_all(root);
    ClipboardStore store(root);
    for (int index = 0; index < 12; ++index) {
        store.put({.mime_types={"text/plain"},
                   .bytes={std::byte{static_cast<unsigned char>('a' + index)}},
                   .kind=ContentKind::Text, .source_app="test", .captured_at_ms=100 + index});
    }
    store.retain_latest(10);
    assert(store.items_newest_first(50).size() == 10);
    store.clear();
    assert(store.items_newest_first(50).empty());
    std::filesystem::remove_all(root);
}
