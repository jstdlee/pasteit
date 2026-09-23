#include "history/clipboard_history_store.hpp"

#include "storage/clipboard_store.hpp"
#include "util/json.hpp"
#include "util/utf8.hpp"
#include "util/replace_file.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace pastit {
namespace {

constexpr int kSchemaVersion = 1;

std::string kind_name(ContentKind kind) {
    switch (kind) {
        case ContentKind::Text:
            return "text";
        case ContentKind::Url:
            return "url";
        case ContentKind::Email:
            return "email";
        case ContentKind::Image:
            return "image";
        case ContentKind::Path:
            return "path";
        case ContentKind::Json:
            return "json";
        case ContentKind::DateTime:
            return "datetime";
        case ContentKind::Unknown:
            return "unknown";
    }
    return "unknown";
}

std::optional<ContentKind> parse_kind(const std::string& value) {
    if (value == "text") return ContentKind::Text;
    if (value == "url") return ContentKind::Url;
    if (value == "email") return ContentKind::Email;
    if (value == "image") return ContentKind::Image;
    if (value == "path") return ContentKind::Path;
    if (value == "json") return ContentKind::Json;
    if (value == "datetime") return ContentKind::DateTime;
    if (value == "unknown") return ContentKind::Unknown;
    return std::nullopt;
}

std::string tag_name(SemanticTag tag) {
    switch (tag) {
        case SemanticTag::PlainText:
            return "plain_text";
        case SemanticTag::Url:
            return "url";
        case SemanticTag::Email:
            return "email";
        case SemanticTag::Image:
            return "image";
        case SemanticTag::Path:
            return "path";
        case SemanticTag::FileUri:
            return "file_uri";
        case SemanticTag::Json:
            return "json";
        case SemanticTag::Resume:
            return "resume";
        case SemanticTag::DateTime:
            return "datetime";
    }
    return "plain_text";
}

std::optional<SemanticTag> parse_tag(const std::string& value) {
    if (value == "plain_text") return SemanticTag::PlainText;
    if (value == "url") return SemanticTag::Url;
    if (value == "email") return SemanticTag::Email;
    if (value == "image") return SemanticTag::Image;
    if (value == "path") return SemanticTag::Path;
    if (value == "file_uri") return SemanticTag::FileUri;
    if (value == "json") return SemanticTag::Json;
    if (value == "resume") return SemanticTag::Resume;
    if (value == "datetime") return SemanticTag::DateTime;
    return std::nullopt;
}

bool is_hash_text(std::string_view value) {
    if (value.size() < 16 || value.size() > 128) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const char ch) {
        return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
    });
}

std::uint64_t serial_from_ref(const std::string& ref) {
    constexpr std::string_view prefix = "clip_";
    if (ref.rfind(prefix, 0) != 0) {
        throw std::invalid_argument("invalid clipboard ref");
    }
    const auto separator = ref.rfind('_');
    if (separator == std::string::npos || separator <= prefix.size() || separator + 1 >= ref.size()) {
        throw std::invalid_argument("invalid clipboard ref");
    }
    std::size_t timestamp_consumed = 0;
    const auto timestamp = ref.substr(prefix.size(), separator - prefix.size());
    (void)std::stoll(timestamp, &timestamp_consumed);
    if (timestamp_consumed != timestamp.size()) {
        throw std::invalid_argument("invalid clipboard ref");
    }
    std::size_t consumed = 0;
    const auto serial = std::stoull(ref.substr(separator + 1), &consumed);
    if (consumed != ref.size() - separator - 1 || serial == 0) {
        throw std::invalid_argument("invalid clipboard ref");
    }
    return serial;
}

std::uint64_t advanced_next_ref(const std::vector<ClipboardItem>& items, std::uint64_t requested) {
    std::uint64_t next = std::max<std::uint64_t>(requested, 1);
    for (const auto& item : items) {
        next = std::max(next, serial_from_ref(item.ref) + 1);
    }
    return next;
}

std::vector<std::string_view> integer_lexemes_for_key(std::string_view json, std::string_view key) {
    std::vector<std::string_view> lexemes;
    for (std::size_t position = 0; position < json.size();) {
        if (json[position] != '"') {
            ++position;
            continue;
        }

        const auto content_start = position + 1;
        auto cursor = content_start;
        bool escaped = false;
        while (cursor < json.size() && json[cursor] != '"') {
            if (json[cursor] == '\\') {
                escaped = true;
                cursor += 2;
            } else {
                ++cursor;
            }
        }
        if (cursor >= json.size()) {
            return {};
        }

        const auto raw_text = json.substr(content_start, cursor - content_start);
        position = cursor + 1;
        if (escaped || raw_text != key) {
            continue;
        }

        auto value_start = position;
        while (value_start < json.size() && std::isspace(static_cast<unsigned char>(json[value_start]))) {
            ++value_start;
        }
        if (value_start >= json.size() || json[value_start] != ':') {
            continue;
        }
        ++value_start;
        while (value_start < json.size() && std::isspace(static_cast<unsigned char>(json[value_start]))) {
            ++value_start;
        }
        if (value_start >= json.size() || (json[value_start] != '-' &&
                                           !std::isdigit(static_cast<unsigned char>(json[value_start])))) {
            continue;
        }

        auto value_end = value_start;
        while (value_end < json.size() && json[value_end] != ',' && json[value_end] != '}' &&
               json[value_end] != ']' && !std::isspace(static_cast<unsigned char>(json[value_end]))) {
            ++value_end;
        }
        lexemes.push_back(json.substr(value_start, value_end - value_start));
    }
    return lexemes;
}

std::optional<std::uint64_t> parse_uint64_lexeme(std::string_view lexeme) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(lexeme.data(), lexeme.data() + lexeme.size(), value);
    if (error != std::errc{} || end != lexeme.data() + lexeme.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::int64_t> parse_int64_lexeme(std::string_view lexeme) {
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(lexeme.data(), lexeme.data() + lexeme.size(), value);
    if (error != std::errc{} || end != lexeme.data() + lexeme.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::vector<std::string>> parse_string_array(const JsonValue* value) {
    const auto* array = value ? value->array() : nullptr;
    if (array == nullptr) {
        return std::nullopt;
    }
    std::vector<std::string> out;
    out.reserve(array->size());
    for (const auto& item : *array) {
        const auto* text = item.string();
        if (text == nullptr) {
            return std::nullopt;
        }
        out.push_back(*text);
    }
    return out;
}

std::optional<std::vector<SemanticTag>> parse_tag_array(const JsonValue* value) {
    const auto tags = parse_string_array(value);
    if (!tags) {
        return std::nullopt;
    }
    std::vector<SemanticTag> out;
    out.reserve(tags->size());
    for (const auto& tag : *tags) {
        const auto parsed = parse_tag(tag);
        if (!parsed) {
            return std::nullopt;
        }
        out.push_back(*parsed);
    }
    return out;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<std::byte> read_blob(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::vector<char> chars{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes;
    bytes.reserve(chars.size());
    for (const char ch : chars) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return bytes;
}

std::vector<ClipboardItem> sorted_newest_first(std::vector<ClipboardItem> items) {
    std::stable_sort(items.begin(), items.end(), [](const auto& left, const auto& right) {
        if (left.captured_at_ms != right.captured_at_ms) {
            return left.captured_at_ms > right.captured_at_ms;
        }
        return serial_from_ref(left.ref) > serial_from_ref(right.ref);
    });
    return items;
}

std::vector<ClipboardItem> coalesced_adjacent(std::vector<ClipboardItem> items) {
    std::vector<ClipboardItem> out;
    out.reserve(items.size());
    for (auto& item : items) {
        if (!out.empty() && !item.content_hash.empty() && out.back().content_hash == item.content_hash) {
            continue;
        }
        out.push_back(std::move(item));
    }
    return out;
}

void write_string_array(std::ostream& out, const std::vector<std::string>& values) {
    out << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        out << json_quote(values[index]);
    }
    out << ']';
}

void write_tag_array(std::ostream& out, const std::vector<SemanticTag>& tags) {
    out << '[';
    for (std::size_t index = 0; index < tags.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        out << json_quote(tag_name(tags[index]));
    }
    out << ']';
}

std::set<std::string> retained_hash_set(const std::vector<ClipboardItem>& items) {
    std::set<std::string> hashes;
    for (const auto& item : items) {
        hashes.insert(item.content_hash);
    }
    return hashes;
}

}  // namespace

ClipboardHistoryStore::ClipboardHistoryStore(std::filesystem::path data_dir, std::size_t retention)
    : data_dir_(std::move(data_dir)),
      manifest_path_(data_dir_ / "clipboard-history.json"),
      blob_dir_(data_dir_ / "blobs"),
      retention_(retention) {}

ClipboardHistoryLoadResult ClipboardHistoryStore::load() const {
    ClipboardHistoryLoadResult result;
    if (!std::filesystem::exists(manifest_path_)) {
        return result;
    }

    const auto manifest_text = read_text_file(manifest_path_);
    if (!is_valid_json(manifest_text)) {
        cleanup_poisoned_ = true;
        result.warning = "Clipboard history manifest is malformed; history was not loaded.";
        return result;
    }

    const auto root = parse_json(manifest_text);
    const auto* object = root ? root->object() : nullptr;
    if (object == nullptr) {
        cleanup_poisoned_ = true;
        result.warning = "Clipboard history manifest is malformed; history was not loaded.";
        return result;
    }

    const auto schema_lexemes = integer_lexemes_for_key(manifest_text, "schema_version");
    const auto next_ref_lexemes = integer_lexemes_for_key(manifest_text, "next_ref");
    const auto size_lexemes = integer_lexemes_for_key(manifest_text, "size_bytes");
    const auto captured_at_lexemes = integer_lexemes_for_key(manifest_text, "captured_at_ms");
    const auto schema = schema_lexemes.size() == 1 ? parse_uint64_lexeme(schema_lexemes.front()) : std::nullopt;
    const auto manifest_next_ref =
        next_ref_lexemes.size() == 1 ? parse_uint64_lexeme(next_ref_lexemes.front()) : std::nullopt;
    const auto* schema_value = root->get("schema_version");
    const auto* next_ref_value = root->get("next_ref");
    const auto* items = root->get("items") ? root->get("items")->array() : nullptr;
    if (!schema || *schema != kSchemaVersion || !manifest_next_ref || *manifest_next_ref == 0 || items == nullptr ||
        size_lexemes.size() != items->size() || captured_at_lexemes.size() != items->size() ||
        schema_value == nullptr || schema_value->number() == std::nullopt || next_ref_value == nullptr ||
        next_ref_value->number() == std::nullopt) {
        cleanup_poisoned_ = true;
        result.warning = "Clipboard history manifest is malformed; history was not loaded.";
        return result;
    }

    std::set<std::string> refs;
    std::vector<ClipboardItem> loaded;
    loaded.reserve(items->size());
    bool skipped_missing_blob = false;
    std::uint64_t max_manifest_serial = 0;
    try {
        for (std::size_t index = 0; index < items->size(); ++index) {
            const auto& value = (*items)[index];
            const auto* ref = value.get("ref");
            const auto* hash = value.get("content_hash");
            const auto* kind = value.get("kind");
            const auto* blob_filename = value.get("blob_filename");
            const auto* preview = value.get("preview");
            const auto* source_app = value.get("source_app");
            const auto* size_value = value.get("size_bytes");
            const auto* captured_at_value = value.get("captured_at_ms");
            const auto mime_types = parse_string_array(value.get("mime_types"));
            const auto tags = parse_tag_array(value.get("tags"));
            const auto size_bytes = parse_uint64_lexeme(size_lexemes[index]);
            const auto captured_at_ms = parse_int64_lexeme(captured_at_lexemes[index]);
            if (ref == nullptr || ref->string() == nullptr || hash == nullptr || hash->string() == nullptr ||
                kind == nullptr || kind->string() == nullptr || preview == nullptr || preview->string() == nullptr ||
                blob_filename == nullptr || blob_filename->string() == nullptr ||
                source_app == nullptr || source_app->string() == nullptr || !mime_types || !tags || !size_bytes ||
                !captured_at_ms || size_value == nullptr || size_value->number() == std::nullopt ||
                captured_at_value == nullptr || captured_at_value->number() == std::nullopt ||
                !is_hash_text(*hash->string()) ||
                *blob_filename->string() != *hash->string() + ".bin") {
                cleanup_poisoned_ = true;
                result.warning = "Clipboard history manifest is malformed; history was not loaded.";
                result.items.clear();
                return result;
            }
            const auto parsed_kind = parse_kind(*kind->string());
            if (!parsed_kind || !refs.insert(*ref->string()).second) {
                cleanup_poisoned_ = true;
                result.warning = "Clipboard history manifest is malformed; history was not loaded.";
                result.items.clear();
                return result;
            }
            max_manifest_serial = std::max(max_manifest_serial, serial_from_ref(*ref->string()));

            const auto blob_path = blob_dir_ / (*hash->string() + ".bin");
            if (!std::filesystem::exists(blob_path)) {
                skipped_missing_blob = true;
                continue;
            }

            ClipboardItem item;
            item.ref = *ref->string();
            item.content_hash = *hash->string();
            item.mime_types = *mime_types;
            for (auto& mime_type : item.mime_types) mime_type = sanitize_utf8(mime_type);
            item.kind = *parsed_kind;
            item.blob_path = blob_path;
            item.preview = sanitize_utf8(*preview->string());
            item.size_bytes = *size_bytes;
            item.captured_at_ms = *captured_at_ms;
            item.source_app = sanitize_utf8(*source_app->string());
            item.tags = *tags;
            loaded.push_back(std::move(item));
        }
        result.items = sorted_newest_first(std::move(loaded));
        if (result.items.size() > retention_) {
            result.items.resize(retention_);
        }
        result.next_ref = std::max(*manifest_next_ref, max_manifest_serial + 1);
    } catch (const std::exception&) {
        result.items.clear();
        result.next_ref = 1;
        cleanup_poisoned_ = true;
        result.warning = "Clipboard history manifest is malformed; history was not loaded.";
        return result;
    }

    if (skipped_missing_blob) {
        result.warning = "Clipboard history skipped records with missing blob data.";
    }
    return result;
}

ClipboardHistorySaveResult ClipboardHistoryStore::save(std::vector<ClipboardItem> items_newest_first,
                                                       std::uint64_t next_ref) const {
    ClipboardHistorySaveResult result;
    result.next_ref = std::max<std::uint64_t>(next_ref, 1);
    result.blob_dir = blob_dir_;

    std::error_code ec;
    std::filesystem::create_directories(blob_dir_, ec);
    if (ec) {
        result.error = ec.message();
        return result;
    }

    std::vector<ClipboardItem> retained;
    try {
        retained = coalesced_adjacent(sorted_newest_first(std::move(items_newest_first)));
        if (retained.size() > retention_) {
            retained.resize(retention_);
        }
        result.next_ref = advanced_next_ref(retained, result.next_ref);
    } catch (const std::exception& error) {
        result.error = error.what();
        return result;
    }

    for (auto& item : retained) {
        item.preview = sanitize_utf8(item.preview);
        item.source_app = sanitize_utf8(item.source_app);
        for (auto& mime_type : item.mime_types) mime_type = sanitize_utf8(mime_type);
        const auto source_size = std::filesystem::file_size(item.blob_path, ec);
        if (ec || source_size != item.size_bytes) {
            result.error = ec ? ec.message() : "Clipboard history source blob size does not match its metadata.";
            return result;
        }
        if (item.content_hash.empty() && !item.blob_path.empty() && std::filesystem::exists(item.blob_path)) {
            item.content_hash = content_hash_hex(read_blob(item.blob_path));
        }
        if (!is_hash_text(item.content_hash)) {
            result.error = "Clipboard history item has invalid content hash.";
            return result;
        }
        const auto blob_path = blob_dir_ / (item.content_hash + ".bin");
        if (!std::filesystem::exists(blob_path)) {
            std::filesystem::copy_file(item.blob_path, blob_path, std::filesystem::copy_options::none, ec);
            if (ec) {
                result.error = ec.message();
                return result;
            }
        }
        const auto stored_size = std::filesystem::file_size(blob_path, ec);
        if (ec || stored_size != item.size_bytes) {
            result.error = ec ? ec.message() : "Clipboard history blob write did not complete.";
            return result;
        }
        item.blob_path = blob_path;
    }

    auto tmp_path = manifest_path_;
    tmp_path += ".tmp";
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            result.error = "Could not open clipboard history manifest.";
            return result;
        }
        out << "{\n\"schema_version\":" << kSchemaVersion << ",\n\"next_ref\":" << result.next_ref << ",\n\"items\":[";
        for (std::size_t index = 0; index < retained.size(); ++index) {
            const auto& item = retained[index];
            if (index != 0) {
                out << ',';
            }
            out << "\n{\"ref\":" << json_quote(item.ref)
                << ",\"content_hash\":" << json_quote(item.content_hash)
                << ",\"mime_types\":";
            write_string_array(out, item.mime_types);
            out << ",\"kind\":" << json_quote(kind_name(item.kind))
                << ",\"blob_filename\":" << json_quote(item.content_hash + ".bin")
                << ",\"preview\":" << json_quote(item.preview)
                << ",\"size_bytes\":" << item.size_bytes
                << ",\"captured_at_ms\":" << item.captured_at_ms
                << ",\"source_app\":" << json_quote(item.source_app)
                << ",\"tags\":";
            write_tag_array(out, item.tags);
            out << '}';
        }
        out << "\n]}\n";
        out.flush();
        if (!out) {
            result.error = "Could not write clipboard history manifest.";
            return result;
        }
        out.close();
        if (!out) {
            result.error = "Could not close clipboard history manifest.";
            return result;
        }
    }

    replace_file(tmp_path, manifest_path_, ec);
    if (ec) {
        std::filesystem::remove(tmp_path);
        result.error = ec.message();
        return result;
    }

    result.retained_items = retained;
    const auto hashes = retained_hash_set(result.retained_items);
    result.retained_hashes.assign(hashes.begin(), hashes.end());
    result.success = true;
    result.cleanup_authorized = !cleanup_poisoned_;
    return result;
}

bool ClipboardHistoryStore::cleanup_orphan_blobs(const ClipboardHistorySaveResult& save_result, std::string& error) const {
    error.clear();
    if (cleanup_poisoned_ || !save_result.success || !save_result.cleanup_authorized) {
        error = "Clipboard history cleanup requires a successful save.";
        return false;
    }
    if (save_result.blob_dir != blob_dir_) {
        error = "Clipboard history cleanup result belongs to a different blob directory.";
        return false;
    }
    if (!std::filesystem::exists(blob_dir_)) {
        return true;
    }

    const std::set<std::string> retained(save_result.retained_hashes.begin(), save_result.retained_hashes.end());
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(blob_dir_, ec)) {
        if (ec) {
            error = ec.message();
            return false;
        }
        if (!entry.is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        const auto path = entry.path();
        if (path.extension() != ".bin") {
            continue;
        }
        const auto hash = path.stem().string();
        if (!is_hash_text(hash) || retained.count(hash) != 0) {
            continue;
        }
        std::filesystem::remove(path, ec);
        if (ec) {
            error = ec.message();
            return false;
        }
    }
    return true;
}

}  // namespace pastit
