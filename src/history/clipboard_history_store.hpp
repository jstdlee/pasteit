#pragma once

#include "core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pastit {

struct ClipboardHistoryLoadResult {
    std::vector<ClipboardItem> items;
    std::uint64_t next_ref = 1;
    std::string warning;
};

struct ClipboardHistorySaveResult {
    bool success = false;
    std::string error;
    std::vector<ClipboardItem> retained_items;
    std::uint64_t next_ref = 1;
    std::vector<std::string> retained_hashes;
    std::filesystem::path blob_dir;
    bool cleanup_authorized = false;
};

class ClipboardHistoryStore {
public:
    explicit ClipboardHistoryStore(std::filesystem::path data_dir, std::size_t retention = 50);

    ClipboardHistoryLoadResult load() const;
    ClipboardHistorySaveResult save(std::vector<ClipboardItem> items_newest_first, std::uint64_t next_ref) const;
    bool cleanup_orphan_blobs(const ClipboardHistorySaveResult& save_result, std::string& error) const;

private:
    std::filesystem::path data_dir_;
    std::filesystem::path manifest_path_;
    std::filesystem::path blob_dir_;
    std::size_t retention_ = 50;
    mutable bool cleanup_poisoned_ = false;
};

}  // namespace pastit
