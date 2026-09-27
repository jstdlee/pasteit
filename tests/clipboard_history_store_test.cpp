#include "test_env.hpp"
#include "history/clipboard_history_store.hpp"
#include "storage/clipboard_store.hpp"
#include "util/json.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
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

std::vector<std::byte> png_like_bytes(int value) {
    return {
        std::byte{0x89}, std::byte{'P'}, std::byte{'N'}, std::byte{'G'},
        std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a},
        static_cast<std::byte>(value & 0xff),
    };
}

std::vector<std::byte> payload_for(int index) {
    if (index == 30 || index == 31) {
        return bytes("adjacent duplicate payload");
    }
    if (index % 5 == 0) {
        return png_like_bytes(index);
    }
    return bytes("clipboard text item " + std::to_string(index));
}

pasteit::ContentKind kind_for(int index) {
    return index % 5 == 0 ? pasteit::ContentKind::Image : pasteit::ContentKind::Text;
}

std::size_t count_hash(const std::vector<pasteit::ClipboardItem>& items, const std::string& hash) {
    return static_cast<std::size_t>(std::count_if(items.begin(), items.end(), [&](const auto& item) {
        return item.content_hash == hash;
    }));
}

void write_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string manifest_fixture(std::string_view schema_value,
                             std::string_view next_ref_field,
                             std::string_view blob_filename_field,
                             std::string_view size_bytes_field,
                             std::string_view captured_at_field) {
    return std::string{"{\"schema_version\":"} + std::string{schema_value} + ',' +
           std::string{next_ref_field} +
           "\"items\":[{\"ref\":\"clip_100_1\",\"content_hash\":\"1111111111111111\"," +
           "\"mime_types\":[\"text/plain\"],\"kind\":\"text\"," + std::string{blob_filename_field} +
           "\"preview\":\"x\"," + std::string{size_bytes_field} + std::string{captured_at_field} +
           "\"source_app\":\"fixture\",\"tags\":[\"plain_text\"]}]}";
}

}  // namespace

int main() {
    using namespace pasteit;

    const auto root = std::filesystem::temp_directory_path() / "pasteit_clipboard_history_store_test";
    pasteit_test::remove_tree(root);
    std::filesystem::create_directories(root);

    const auto utf8_root = root / "utf8-preview";
    ClipboardStore utf8_store(utf8_root);
    const std::string utf8_boundary_text = std::string(158, 'a') + "骋" + "tail";
    const auto utf8_item = utf8_store.put(ClipboardData{
        .mime_types = {"text/plain;charset=utf-8"},
        .bytes = bytes(utf8_boundary_text),
        .kind = ContentKind::Text,
        .source_app = "utf8-boundary",
        .captured_at_ms = 1726799999000,
    });
    assert(utf8_item.preview == std::string(158, 'a') + "...");

    const auto legacy_utf8_root = root / "legacy-invalid-utf8";
    std::filesystem::create_directories(legacy_utf8_root / "blobs");
    write_file(legacy_utf8_root / "blobs" / "1111111111111111.bin", "x");
    auto legacy_manifest = manifest_fixture(
        "1", "\"next_ref\":2,", "\"blob_filename\":\"1111111111111111.bin\",",
        "\"size_bytes\":1,", "\"captured_at_ms\":100,");
    std::string invalid_preview;
    invalid_preview.push_back(static_cast<char>(0xE9));
    invalid_preview.push_back(static_cast<char>(0xAA));
    invalid_preview.push_back('.');
    legacy_manifest.replace(legacy_manifest.find("\"preview\":\"x\""),
                            std::string{"\"preview\":\"x\""}.size(),
                            "\"preview\":\"" + invalid_preview + "\"");
    write_file(legacy_utf8_root / "clipboard-history.json", legacy_manifest);
    const auto legacy_loaded = ClipboardHistoryStore(legacy_utf8_root).load();
    assert(legacy_loaded.items.size() == 1);
    assert(legacy_loaded.items.front().preview == "\xEF\xBF\xBD\xEF\xBF\xBD.");

    const auto tie_root = root / "tie-order";
    ClipboardStore tie_store(tie_root);
    constexpr std::int64_t tie_ms = 1726799999999;
    for (int serial = 1; serial <= 10; ++serial) {
        tie_store.put(ClipboardData{
            .mime_types = {"text/plain"},
            .bytes = bytes("tie item " + std::to_string(serial)),
            .kind = ContentKind::Text,
            .source_app = "tie-capture",
            .captured_at_ms = tie_ms,
        });
    }
    assert(tie_store.items_newest_first(1).front().ref ==
           "clip_" + std::to_string(tie_ms) + "_10");

    ClipboardHistoryStore tie_history(tie_root, 1);
    const auto tie_saved = tie_history.save(tie_store.items_newest_first(100), tie_store.next_ref());
    assert(tie_saved.success);
    assert(tie_saved.retained_items.size() == 1);
    assert(tie_saved.retained_items.front().ref == "clip_" + std::to_string(tie_ms) + "_10");

    ClipboardStore live_store(root);
    constexpr std::int64_t base_ms = 1726800000000;
    std::vector<std::vector<std::byte>> original_payloads;
    original_payloads.reserve(52);

    for (int index = 0; index < 52; ++index) {
        auto payload = payload_for(index);
        original_payloads.push_back(payload);
        const auto item = live_store.put(ClipboardData{
            .mime_types = {kind_for(index) == ContentKind::Image ? "image/png" : "text/plain"},
            .bytes = payload,
            .kind = kind_for(index),
            .source_app = "capture-" + std::to_string(index),
            .captured_at_ms = base_ms + index,
        });
        assert(item.ref == "clip_" + std::to_string(base_ms + index) + "_" + std::to_string(index + 1));
        assert(item.content_hash == content_hash_hex(payload));
    }

    ClipboardHistoryStore history(root);
    const auto saved = history.save(live_store.items_newest_first(100), live_store.next_ref());
    assert(saved.success);
    assert(saved.retained_items.size() == 50);
    assert(saved.next_ref == 53);

    const auto duplicate_hash = content_hash_hex(bytes("adjacent duplicate payload"));
    assert(count_hash(saved.retained_items, duplicate_hash) == 1);
    assert(saved.retained_items.front().captured_at_ms == base_ms + 51);
    assert(saved.retained_items.front().ref == "clip_" + std::to_string(base_ms + 51) + "_52");
    assert(saved.retained_items.back().captured_at_ms == base_ms + 1);

    const auto manifest_path = root / "clipboard-history.json";
    assert(std::filesystem::exists(manifest_path));
    assert(!std::filesystem::exists(root / "clipboard-history.json.tmp"));
    std::ifstream manifest_in(manifest_path);
    const auto manifest = parse_json(std::string{std::istreambuf_iterator<char>(manifest_in),
                                                std::istreambuf_iterator<char>()});
    assert(manifest);
    assert(manifest->get("schema_version")->number() == 1.0);
    assert(manifest->get("next_ref")->number() == 53.0);
    assert(manifest->get("items")->array()->size() == 50);

    const auto loaded = history.load();
    assert(loaded.warning.empty());
    assert(loaded.next_ref == 53);
    assert(loaded.items.size() == 50);
    assert(count_hash(loaded.items, duplicate_hash) == 1);

    for (std::size_t index = 1; index < loaded.items.size(); ++index) {
        assert(loaded.items[index - 1].captured_at_ms >= loaded.items[index].captured_at_ms);
    }

    const auto image_hash = content_hash_hex(original_payloads[50]);
    const auto image = std::find_if(loaded.items.begin(), loaded.items.end(), [&](const auto& item) {
        return item.content_hash == image_hash;
    });
    assert(image != loaded.items.end());
    assert(image->kind == ContentKind::Image);
    assert(image->blob_path == root / "blobs" / (image_hash + ".bin"));

    ClipboardStore restored(root);
    restored.restore(loaded.items, loaded.next_ref);
    assert(restored.items_newest_first(100).size() == 50);
    assert(restored.read(image->ref) == original_payloads[50]);

    std::set<std::string> restored_refs;
    for (const auto& item : restored.items_newest_first(100)) {
        restored_refs.insert(item.ref);
    }
    const auto after_restart = restored.put(ClipboardData{
        .mime_types = {"text/plain"},
        .bytes = bytes("after restart"),
        .kind = ContentKind::Text,
        .source_app = "restart-capture",
        .captured_at_ms = base_ms + 6000,
    });
    assert(after_restart.ref == "clip_" + std::to_string(base_ms + 6000) + "_53");
    assert(restored_refs.count(after_restart.ref) == 0);
    assert(restored.next_ref() == 54);

    const auto repeated_load = history.load();
    assert(repeated_load.warning.empty());
    ClipboardStore repeated(root);
    repeated.restore(repeated_load.items, repeated_load.next_ref);
    assert(repeated.items_newest_first(100).size() == 50);
    assert(repeated.put(ClipboardData{
        .mime_types = {"text/plain"},
        .bytes = bytes("second restart"),
        .kind = ContentKind::Text,
        .source_app = "restart-capture",
        .captured_at_ms = base_ms + 7000,
    }).ref == "clip_" + std::to_string(base_ms + 7000) + "_53");

    const auto validation_root = root / "manifest-validation";
    std::filesystem::create_directories(validation_root / "blobs");
    write_file(validation_root / "blobs" / "1111111111111111.bin", "x");
    const auto valid_manifest = manifest_fixture(
        "1", "\"next_ref\":2,", "\"blob_filename\":\"1111111111111111.bin\",",
        "\"size_bytes\":1,", "\"captured_at_ms\":100,");
    write_file(validation_root / "clipboard-history.json", valid_manifest);
    const auto valid_loaded = ClipboardHistoryStore(validation_root).load();
    assert(valid_loaded.warning.empty());
    assert(valid_loaded.items.size() == 1);

    ClipboardItem validation_item{
        .ref = "clip_100_1",
        .content_hash = "1111111111111111",
        .mime_types = {"text/plain"},
        .kind = ContentKind::Text,
        .blob_path = validation_root / "blobs" / "1111111111111111.bin",
        .preview = "x",
        .size_bytes = 1,
        .captured_at_ms = 100,
        .source_app = "fixture",
        .tags = {SemanticTag::PlainText},
    };
    write_file(validation_root / "clipboard-history.json",
               manifest_fixture("1.", "\"next_ref\":2,",
                                "\"blob_filename\":\"1111111111111111.bin\",",
                                "\"size_bytes\":1,", "\"captured_at_ms\":100,"));
    ClipboardHistoryStore malformed_numeric_history(validation_root);
    const auto malformed_numeric = malformed_numeric_history.load();
    assert(!malformed_numeric.warning.empty());
    assert(malformed_numeric.items.empty());
    const auto save_after_malformed_numeric = malformed_numeric_history.save({validation_item}, 2);
    assert(save_after_malformed_numeric.success);
    assert(!save_after_malformed_numeric.cleanup_authorized);

    write_file(validation_root / "clipboard-history.json",
               manifest_fixture("1", "\"next_ref\":2,",
                                "\"blob_filename\":\"1111111111111111.bin\",",
                                "\"size_bytes\":1.0000000000000001,", "\"captured_at_ms\":100,"));
    ClipboardHistoryStore rounded_numeric_history(validation_root);
    const auto rounded_numeric = rounded_numeric_history.load();
    assert(!rounded_numeric.warning.empty());
    assert(rounded_numeric.items.empty());
    const auto save_after_rounded_numeric = rounded_numeric_history.save({validation_item}, 2);
    assert(save_after_rounded_numeric.success);
    assert(!save_after_rounded_numeric.cleanup_authorized);

    const std::vector<std::string> invalid_manifests = {
        manifest_fixture("1.5", "\"next_ref\":2,", "\"blob_filename\":\"1111111111111111.bin\",",
                         "\"size_bytes\":1,", "\"captured_at_ms\":100,"),
        manifest_fixture("1", "", "\"blob_filename\":\"1111111111111111.bin\",",
                         "\"size_bytes\":1,", "\"captured_at_ms\":100,"),
        manifest_fixture("1", "\"next_ref\":\"2\",", "\"blob_filename\":\"1111111111111111.bin\",",
                         "\"size_bytes\":1,", "\"captured_at_ms\":100,"),
        manifest_fixture("1", "\"next_ref\":2.5,", "\"blob_filename\":\"1111111111111111.bin\",",
                         "\"size_bytes\":1,", "\"captured_at_ms\":100,"),
        manifest_fixture("1", "\"next_ref\":2,", "", "\"size_bytes\":1,", "\"captured_at_ms\":100,"),
        manifest_fixture("1", "\"next_ref\":2,", "\"blob_filename\":\"2222222222222222.bin\",",
                         "\"size_bytes\":1,", "\"captured_at_ms\":100,"),
        manifest_fixture("1", "\"next_ref\":2,", "\"blob_filename\":7,",
                         "\"size_bytes\":1,", "\"captured_at_ms\":100,"),
        manifest_fixture("1", "\"next_ref\":2,", "\"blob_filename\":\"1111111111111111.bin\",",
                         "", "\"captured_at_ms\":100,"),
        manifest_fixture("1", "\"next_ref\":2,", "\"blob_filename\":\"1111111111111111.bin\",",
                         "\"size_bytes\":\"1\",", "\"captured_at_ms\":100,"),
        manifest_fixture("1", "\"next_ref\":2,", "\"blob_filename\":\"1111111111111111.bin\",",
                         "\"size_bytes\":1.5,", "\"captured_at_ms\":100,"),
        manifest_fixture("1", "\"next_ref\":2,", "\"blob_filename\":\"1111111111111111.bin\",",
                         "\"size_bytes\":-1,", "\"captured_at_ms\":100,"),
        manifest_fixture("1", "\"next_ref\":2,", "\"blob_filename\":\"1111111111111111.bin\",",
                         "\"size_bytes\":1,", ""),
        manifest_fixture("1", "\"next_ref\":2,", "\"blob_filename\":\"1111111111111111.bin\",",
                         "\"size_bytes\":1,", "\"captured_at_ms\":\"100\","),
        manifest_fixture("1", "\"next_ref\":2,", "\"blob_filename\":\"1111111111111111.bin\",",
                         "\"size_bytes\":1,", "\"captured_at_ms\":100.5,"),
    };
    for (const auto& invalid_manifest : invalid_manifests) {
        write_file(validation_root / "clipboard-history.json", invalid_manifest);
        const auto rejected = ClipboardHistoryStore(validation_root).load();
        assert(!rejected.warning.empty());
        assert(rejected.items.empty());
    }

    const auto blob_check_root = root / "blob-write-check";
    const auto blob_source = root / "blob-write-source.bin";
    std::filesystem::create_directories(blob_check_root / "blobs");
    write_file(blob_source, "payload");
    write_file(blob_check_root / "blobs" / "3333333333333333.bin", "");
    ClipboardItem blob_check_item{
        .ref = "clip_200_1",
        .content_hash = "3333333333333333",
        .mime_types = {"text/plain"},
        .kind = ContentKind::Text,
        .blob_path = blob_source,
        .preview = "payload",
        .size_bytes = 7,
        .captured_at_ms = 200,
        .source_app = "fixture",
        .tags = {SemanticTag::PlainText},
    };
    const auto incomplete_blob_save = ClipboardHistoryStore(blob_check_root).save({blob_check_item}, 2);
    assert(!incomplete_blob_save.success);
    assert(!std::filesystem::exists(blob_check_root / "clipboard-history.json"));

    pasteit_test::remove_tree(root);
}
