#include "ui/clipboard_history_model.hpp"
#include "util/path_utf8.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace pasteit {
namespace {

constexpr std::size_t kPreviewLimit = 80;

std::string type_label(ContentKind kind) {
    switch (kind) {
        case ContentKind::Text:
            return "Text";
        case ContentKind::Url:
            return "URL";
        case ContentKind::Email:
            return "Email";
        case ContentKind::Image:
            return "Image";
        case ContentKind::Path:
            return "Path";
        case ContentKind::Json:
            return "JSON";
        case ContentKind::DateTime:
            return "Date/time";
        case ContentKind::Unknown:
            return "Unknown";
    }
    return "Unknown";
}

std::string type_icon(ContentKind kind) {
    switch (kind) {
        case ContentKind::Text: return "T";
        case ContentKind::Url: return "↗";
        case ContentKind::Email: return "@";
        case ContentKind::Image: return "▧";
        case ContentKind::Path: return "⌂";
        case ContentKind::Json: return "{}";
        case ContentKind::DateTime: return "◷";
        case ContentKind::Unknown: return "?";
    }
    return "?";
}

std::string abbreviated(std::string_view value) {
    if (value.size() <= kPreviewLimit) {
        return std::string(value);
    }
    return std::string(value.substr(0, kPreviewLimit)) + "...";
}

std::string byte_size_label(std::uint64_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    }
    const auto kib = static_cast<double>(bytes) / 1024.0;
    if (kib < 1024.0) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(1) << kib << " KiB";
        return out.str();
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << (kib / 1024.0) << " MiB";
    return out.str();
}


std::string mime_summary(const std::vector<std::string>& mime_types) {
    std::string out;
    for (const auto& mime : mime_types) {
        if (!out.empty()) {
            out += ", ";
        }
        out += mime;
    }
    return out;
}

ClipboardHistoryRow row_from_item(const ClipboardItem& item, bool selected) {
    const bool has_blob = !item.blob_path.empty();
    return {
        .ref = item.ref,
        .kind = item.kind,
        .type_icon = type_icon(item.kind),
        .type_label = type_label(item.kind),
        .preview = abbreviated(item.preview),
        .source = item.source_app,
        .size_label = byte_size_label(item.size_bytes),
        .captured_label = timestamp_label(item.captured_at_ms),
        .mime_summary = mime_summary(item.mime_types),
        .size_bytes = item.size_bytes,
        .captured_at_ms = item.captured_at_ms,
        .is_image = item.kind == ContentKind::Image,
        .has_blob = has_blob,
        .can_use = has_blob,
        .can_save = has_blob,
        .selected = selected,
    };
}

ClipboardHistoryDetail detail_from_item(const ClipboardItem& item, const ClipboardHistoryRow& row) {
    return {
        .ref = item.ref,
        .type_label = row.type_label,
        .preview = item.preview,
        .source = row.source,
        .size_label = row.size_label,
        .captured_label = row.captured_label,
        .mime_summary = row.mime_summary,
        .blob_path = path_to_utf8_string(item.blob_path),
        .mime_types = item.mime_types,
        .tags = item.tags,
        .size_bytes = item.size_bytes,
        .captured_at_ms = item.captured_at_ms,
        .is_image = row.is_image,
        .has_blob = row.has_blob,
        .can_use = row.can_use,
        .can_save = row.can_save,
    };
}

ClipboardHistoryCommand command(ClipboardHistoryCommandKind kind, std::string_view ref) {
    return {.kind = kind, .ref = std::string(ref)};
}

}  // namespace

ClipboardHistoryModel build_clipboard_history_model(const std::vector<ClipboardItem>& items,
                                                    ClipboardHistoryState& state,
                                                    std::size_t limit) {
    std::vector<const ClipboardItem*> sorted;
    sorted.reserve(items.size());
    for (const auto& item : items) {
        sorted.push_back(&item);
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const ClipboardItem* left, const ClipboardItem* right) {
        return left->captured_at_ms > right->captured_at_ms;
    });
    if (sorted.size() > limit) {
        sorted.resize(limit);
    }

    ClipboardHistoryModel model;
    model.rows.reserve(sorted.size());
    const ClipboardItem* selected_item = nullptr;
    for (const auto* item : sorted) {
        const bool selected = !state.detail_ref.empty() && item->ref == state.detail_ref;
        model.rows.push_back(row_from_item(*item, selected));
        if (selected) {
            selected_item = item;
        }
    }

    if (!state.detail_ref.empty()) {
        if (selected_item == nullptr) {
            state.detail_ref.clear();
        } else {
            const auto row = std::find_if(model.rows.begin(), model.rows.end(), [&](const ClipboardHistoryRow& candidate) {
                return candidate.ref == selected_item->ref;
            });
            if (row != model.rows.end()) {
                model.detail = detail_from_item(*selected_item, *row);
            }
        }
    }

    return model;
}

ClipboardHistoryCommand view_clipboard_history_item(ClipboardHistoryState& state, std::string_view ref) {
    state.detail_ref = std::string(ref);
    return command(ClipboardHistoryCommandKind::View, ref);
}

ClipboardHistoryCommand copy_clipboard_history_item(std::string_view ref) {
    return command(ClipboardHistoryCommandKind::Copy, ref);
}

ClipboardHistoryCommand use_clipboard_history_item(std::string_view ref) {
    return command(ClipboardHistoryCommandKind::Use, ref);
}

ClipboardHistoryCommand save_clipboard_history_item(std::string_view ref) {
    return command(ClipboardHistoryCommandKind::Save, ref);
}

ClipboardHistoryCommand close_clipboard_history_detail(ClipboardHistoryState& state) {
    const auto ref = state.detail_ref;
    state.detail_ref.clear();
    return command(ClipboardHistoryCommandKind::CloseDetail, ref);
}

std::string timestamp_label(std::int64_t captured_at_ms) {
    if (captured_at_ms <= 0) {
        return "unknown";
    }
    const auto seconds = static_cast<std::time_t>(captured_at_ms / 1000);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &seconds);
#else
    localtime_r(&seconds, &local_time);
#endif
    std::ostringstream out;
    out << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

}  // namespace pasteit
