#include "test_env.hpp"
#include "history/clipboard_history_store.hpp"
#include "storage/clipboard_store.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (const unsigned char ch : text) {
        out.push_back(static_cast<std::byte>(ch));
    }
    return out;
}

void write_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::size_t count_hash(const std::vector<pasteit::ClipboardItem>& items, const std::string& hash) {
    return static_cast<std::size_t>(std::count_if(items.begin(), items.end(), [&](const auto& item) {
        return item.content_hash == hash;
    }));
}

}  // namespace

int main() {
    using namespace pasteit;

    const auto root = std::filesystem::temp_directory_path() / "pasteit_clipboard_orphan_cleanup_test";
    pasteit_test::remove_tree(root);
    std::filesystem::create_directories(root / "blobs");

    const auto malformed_root = root / "malformed";
    std::filesystem::create_directories(malformed_root / "blobs");
    const auto malformed_orphan = malformed_root / "blobs" / "1111111111111111.bin";
    write_file(malformed_root / "clipboard-history.json", "{not valid json");
    write_file(malformed_orphan, "orphan");

    ClipboardHistoryStore malformed_history(malformed_root);
    const auto malformed = malformed_history.load();
    assert(!malformed.warning.empty());
    assert(malformed.items.empty());

    std::string cleanup_error;
    assert(!malformed_history.cleanup_orphan_blobs(ClipboardHistorySaveResult{}, cleanup_error));
    assert(std::filesystem::exists(malformed_orphan));

    ClipboardStore malformed_live_store(malformed_root);
    malformed_live_store.put(ClipboardData{
        .mime_types = {"text/plain"},
        .bytes = bytes("valid capture after malformed load"),
        .kind = ContentKind::Text,
        .source_app = "recovery-attempt",
        .captured_at_ms = 50,
    });
    const auto save_after_malformed = malformed_history.save(
        malformed_live_store.items_newest_first(10), malformed_live_store.next_ref());
    assert(save_after_malformed.success);
    assert(!save_after_malformed.cleanup_authorized);
    assert(!malformed_history.cleanup_orphan_blobs(save_after_malformed, cleanup_error));
    assert(std::filesystem::exists(malformed_orphan));

    ClipboardStore live_store(root);
    const auto shared_payload = bytes("shared non adjacent payload");
    const auto first_shared = live_store.put(ClipboardData{
        .mime_types = {"text/plain"},
        .bytes = shared_payload,
        .kind = ContentKind::Text,
        .source_app = "first",
        .captured_at_ms = 100,
    });
    const auto middle = live_store.put(ClipboardData{
        .mime_types = {"text/plain"},
        .bytes = bytes("middle payload"),
        .kind = ContentKind::Text,
        .source_app = "middle",
        .captured_at_ms = 200,
    });
    const auto second_shared = live_store.put(ClipboardData{
        .mime_types = {"text/plain"},
        .bytes = shared_payload,
        .kind = ContentKind::Text,
        .source_app = "second",
        .captured_at_ms = 300,
    });
    assert(first_shared.content_hash == second_shared.content_hash);
    assert(first_shared.content_hash != middle.content_hash);

    const auto hash_named_orphan = root / "blobs" / "abcdefabcdefabcd.bin";
    const auto non_hash_bin = root / "blobs" / "not-a-content-hash.bin";
    const auto hash_named_txt = root / "blobs" / "2222222222222222.txt";
    write_file(hash_named_orphan, "delete me after save");
    write_file(non_hash_bin, "must stay");
    write_file(hash_named_txt, "must stay");

    ClipboardHistoryStore history(root);
    const auto saved = history.save(live_store.items_newest_first(10), live_store.next_ref());
    assert(saved.success);
    assert(saved.retained_items.size() == 3);
    assert(count_hash(saved.retained_items, first_shared.content_hash) == 2);

    const auto loaded = history.load();
    assert(loaded.warning.empty());
    assert(loaded.items.size() == 3);
    assert(count_hash(loaded.items, first_shared.content_hash) == 2);

    assert(history.cleanup_orphan_blobs(saved, cleanup_error));
    assert(cleanup_error.empty());
    assert(!std::filesystem::exists(hash_named_orphan));
    assert(std::filesystem::exists(malformed_orphan));
    assert(std::filesystem::exists(non_hash_bin));
    assert(std::filesystem::exists(hash_named_txt));
    assert(std::filesystem::exists(root / "blobs" / (first_shared.content_hash + ".bin")));
    assert(std::filesystem::exists(root / "blobs" / (middle.content_hash + ".bin")));

    pasteit_test::remove_tree(root);
}
