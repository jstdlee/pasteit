#pragma once

#include "core/types.hpp"

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pastit {

struct ClipboardData {
    std::vector<std::string> mime_types;
    std::vector<std::byte> bytes;
    ContentKind kind = ContentKind::Unknown;
    std::string source_app;
    std::int64_t captured_at_ms = 0;
};

class ClipboardStore {
public:
    explicit ClipboardStore(std::filesystem::path data_dir = default_data_dir());

    ClipboardItem put(const ClipboardData& data);
    void restore(std::vector<ClipboardItem> items, std::uint64_t next_ref);
    std::vector<std::byte> read(const std::string& ref) const;
    std::string read_text(const std::string& ref) const;
    std::optional<ClipboardItem> item(const std::string& ref) const;
    const std::map<std::string, ClipboardItem>& items() const { return items_; }
    std::vector<ClipboardItem> items_newest_first(std::size_t limit) const;
    void retain_latest(std::size_t limit);
    void clear();
    std::uint64_t next_ref() const { return next_ref_; }

    static std::filesystem::path default_data_dir();

private:
    std::filesystem::path data_dir_;
    std::uint64_t next_ref_ = 1;
    std::map<std::string, ClipboardItem> items_;
};

std::string content_hash_hex(const std::vector<std::byte>& bytes);

}  // namespace pastit
