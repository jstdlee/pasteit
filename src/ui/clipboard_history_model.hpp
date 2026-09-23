#pragma once

#include "core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pastit {

enum class ClipboardHistoryCommandKind {
    None,
    View,
    Copy,
    Use,
    Save,
    CloseDetail,
};

struct ClipboardHistoryCommand {
    ClipboardHistoryCommandKind kind = ClipboardHistoryCommandKind::None;
    std::string ref;
};

struct ClipboardHistoryState {
    std::string detail_ref;
};

struct ClipboardHistoryRow {
    std::string ref;
    ContentKind kind = ContentKind::Unknown;
    std::string type_icon;
    std::string type_label;
    std::string preview;
    std::string source;
    std::string size_label;
    std::string captured_label;
    std::string mime_summary;
    std::uint64_t size_bytes = 0;
    std::int64_t captured_at_ms = 0;
    bool is_image = false;
    bool has_blob = false;
    bool can_use = false;
    bool can_save = false;
    bool selected = false;
};

struct ClipboardHistoryDetail {
    std::string ref;
    std::string type_label;
    std::string preview;
    std::string source;
    std::string size_label;
    std::string captured_label;
    std::string mime_summary;
    std::string blob_path;
    std::vector<std::string> mime_types;
    std::vector<SemanticTag> tags;
    std::uint64_t size_bytes = 0;
    std::int64_t captured_at_ms = 0;
    bool is_image = false;
    bool has_blob = false;
    bool can_use = false;
    bool can_save = false;
};

struct ClipboardHistoryModel {
    std::vector<ClipboardHistoryRow> rows;
    std::optional<ClipboardHistoryDetail> detail;
};

ClipboardHistoryModel build_clipboard_history_model(const std::vector<ClipboardItem>& items,
                                                    ClipboardHistoryState& state,
                                                    std::size_t limit = 50);
ClipboardHistoryCommand view_clipboard_history_item(ClipboardHistoryState& state, std::string_view ref);
ClipboardHistoryCommand copy_clipboard_history_item(std::string_view ref);
ClipboardHistoryCommand use_clipboard_history_item(std::string_view ref);
ClipboardHistoryCommand save_clipboard_history_item(std::string_view ref);
ClipboardHistoryCommand close_clipboard_history_detail(ClipboardHistoryState& state);

}  // namespace pastit
