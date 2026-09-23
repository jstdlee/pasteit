#include "storage/clipboard_store.hpp"

#include "platform/app_paths.hpp"
#include "util/utf8.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace pastit {
namespace {

constexpr std::size_t kPreviewLimit = 160;

std::string make_ref(std::int64_t captured_at_ms, std::uint64_t serial) {
    std::ostringstream out;
    out << "clip_" << captured_at_ms << '_' << serial;
    return out.str();
}

std::uint64_t serial_from_ref(const std::string& ref) {
    constexpr std::string_view prefix = "clip_";
    if (ref.rfind(prefix, 0) != 0) {
        throw std::invalid_argument("clipboard ref has invalid prefix");
    }
    const auto separator = ref.rfind('_');
    if (separator == std::string::npos || separator <= prefix.size() || separator + 1 >= ref.size()) {
        throw std::invalid_argument("clipboard ref has invalid serial");
    }
    std::size_t timestamp_consumed = 0;
    const auto timestamp = ref.substr(prefix.size(), separator - prefix.size());
    (void)std::stoll(timestamp, &timestamp_consumed);
    if (timestamp_consumed != timestamp.size()) {
        throw std::invalid_argument("clipboard ref has invalid timestamp");
    }
    std::size_t consumed = 0;
    const auto serial = std::stoull(ref.substr(separator + 1), &consumed);
    if (consumed != ref.size() - separator - 1 || serial == 0) {
        throw std::invalid_argument("clipboard ref has invalid serial");
    }
    return serial;
}

std::string preview_for(const std::vector<std::byte>& bytes, ContentKind kind) {
    if (kind == ContentKind::Image) {
        std::ostringstream out;
        out << "image bytes (" << bytes.size() << " bytes)";
        return out.str();
    }
    std::string text;
    text.reserve(bytes.size());
    for (const auto byte : bytes) {
        text.push_back(static_cast<char>(byte));
    }
    text = sanitize_utf8(text);
    for (char& ch : text) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte < 32 || byte == 127) ch = ' ';
    }
    auto preview = utf8_prefix_bytes(text, kPreviewLimit);
    if (preview.size() < text.size()) {
        preview += "...";
    }
    return preview;
}

}  // namespace

std::string content_hash_hex(const std::vector<std::byte>& bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto byte : bytes) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ULL;
    }
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

ClipboardStore::ClipboardStore(std::filesystem::path data_dir)
    : data_dir_(std::move(data_dir)) {
    std::filesystem::create_directories(data_dir_ / "blobs");
}

ClipboardItem ClipboardStore::put(const ClipboardData& data) {
    const auto hash = content_hash_hex(data.bytes);
    const auto blob_path = data_dir_ / "blobs" / (hash + ".bin");
    if (!std::filesystem::exists(blob_path)) {
        std::ofstream out(blob_path, std::ios::binary);
        if (!out) {
            throw std::runtime_error("failed to create clipboard blob");
        }
        out.write(reinterpret_cast<const char*>(data.bytes.data()), static_cast<std::streamsize>(data.bytes.size()));
    }

    ClipboardItem item;
    item.ref = make_ref(data.captured_at_ms, next_ref_++);
    item.content_hash = hash;
    item.mime_types = data.mime_types;
    item.kind = data.kind;
    item.blob_path = blob_path;
    item.preview = preview_for(data.bytes, data.kind);
    item.size_bytes = data.bytes.size();
    item.captured_at_ms = data.captured_at_ms;
    item.source_app = data.source_app;

    switch (data.kind) {
        case ContentKind::Text:
            item.tags.push_back(SemanticTag::PlainText);
            break;
        case ContentKind::Url:
            item.tags.push_back(SemanticTag::Url);
            break;
        case ContentKind::Email:
            item.tags.push_back(SemanticTag::Email);
            break;
        case ContentKind::Image:
            item.tags.push_back(SemanticTag::Image);
            break;
        case ContentKind::Path:
            item.tags.push_back(SemanticTag::Path);
            break;
        case ContentKind::Json:
            item.tags.push_back(SemanticTag::Json);
            break;
        case ContentKind::DateTime:
            item.tags.push_back(SemanticTag::DateTime);
            break;
        case ContentKind::Unknown:
            break;
    }

    items_[item.ref] = item;
    return item;
}

void ClipboardStore::restore(std::vector<ClipboardItem> items, std::uint64_t next_ref) {
    std::map<std::string, ClipboardItem> restored;
    std::set<std::string> refs;
    std::uint64_t max_serial = 0;
    for (auto& item : items) {
        if (!refs.insert(item.ref).second) {
            throw std::invalid_argument("duplicate clipboard ref");
        }
        max_serial = std::max(max_serial, serial_from_ref(item.ref));
        restored.insert_or_assign(item.ref, std::move(item));
    }
    items_ = std::move(restored);
    next_ref_ = std::max(next_ref, max_serial + 1);
    if (next_ref_ == 0) {
        next_ref_ = 1;
    }
}

std::vector<std::byte> ClipboardStore::read(const std::string& ref) const {
    const auto found = items_.find(ref);
    if (found == items_.end()) {
        throw std::out_of_range("unknown clipboard ref");
    }
    std::ifstream in(found->second.blob_path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to read clipboard blob");
    }
    std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::byte> out;
    out.reserve(chars.size());
    for (const char ch : chars) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::string ClipboardStore::read_text(const std::string& ref) const {
    const auto data = read(ref);
    std::string text;
    text.reserve(data.size());
    for (const auto byte : data) {
        text.push_back(static_cast<char>(byte));
    }
    return text;
}

std::optional<ClipboardItem> ClipboardStore::item(const std::string& ref) const {
    const auto found = items_.find(ref);
    if (found == items_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::vector<ClipboardItem> ClipboardStore::items_newest_first(std::size_t limit) const {
    std::vector<ClipboardItem> out;
    out.reserve(items_.size());
    for (const auto& [ref, item] : items_) {
        out.push_back(item);
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& left, const auto& right) {
        if (left.captured_at_ms != right.captured_at_ms) {
            return left.captured_at_ms > right.captured_at_ms;
        }
        return serial_from_ref(left.ref) > serial_from_ref(right.ref);
    });
    if (out.size() > limit) {
        out.resize(limit);
    }
    return out;
}

void ClipboardStore::retain_latest(std::size_t limit) {
    const auto retained = items_newest_first(limit);
    restore(retained, next_ref_);
}

void ClipboardStore::clear() {
    items_.clear();
}

std::filesystem::path ClipboardStore::default_data_dir() {
    return app_data_dir();
}

}  // namespace pastit
