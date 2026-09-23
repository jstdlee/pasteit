#include "ui/clipboard_history_model.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <string>
#include <vector>

namespace {

pastit::ClipboardItem clipboard_item(std::string ref, pastit::ContentKind kind, std::int64_t captured_at_ms) {
    pastit::ClipboardItem item;
    item.ref = std::move(ref);
    item.kind = kind;
    item.preview = "preview-" + item.ref;
    item.size_bytes = item.preview.size();
    item.captured_at_ms = captured_at_ms;
    item.source_app = "test-source";
    item.mime_types = {"text/plain"};
    item.blob_path = std::filesystem::path{"/tmp"} / (item.ref + ".bin");
    return item;
}

bool has_row(const pastit::ClipboardHistoryModel& model, const std::string& ref) {
    return std::any_of(model.rows.begin(), model.rows.end(), [&](const pastit::ClipboardHistoryRow& row) {
        return row.ref == ref;
    });
}

}  // namespace

int main() {
    using namespace pastit;

    std::vector<ClipboardItem> many_items;
    for (int index = 0; index < 55; ++index) {
        many_items.push_back(clipboard_item("clip_" + std::to_string(index), ContentKind::Text, 1000 + index));
    }
    many_items.back().kind = ContentKind::Image;
    many_items.back().mime_types = {"image/png"};
    many_items.back().preview = std::string(120, 'x');
    many_items.back().size_bytes = 1536;
    many_items.back().source_app = "spectacle";

    ClipboardHistoryState state;
    const auto limited_model = build_clipboard_history_model(many_items, state);
    assert(limited_model.rows.size() == 50);
    assert(limited_model.rows.front().ref == "clip_54");
    assert(limited_model.rows.back().ref == "clip_5");
    assert(!has_row(limited_model, "clip_4"));

    const auto& newest = limited_model.rows.front();
    assert(newest.type_label == "Image");
    assert(newest.type_icon == "▧");
    assert(newest.kind == ContentKind::Image);
    assert(newest.preview.size() < many_items.back().preview.size());
    assert(newest.preview.find("...") != std::string::npos);
    assert(newest.source == "spectacle");
    assert(newest.size_bytes == 1536);
    assert(newest.size_label == "1.5 KiB");
    assert(newest.captured_at_ms == 1054);
    assert(newest.is_image);
    assert(newest.has_blob);
    assert(newest.can_use);
    assert(newest.can_save);

    auto old_item = clipboard_item("old", ContentKind::Text, 10);
    auto new_item = clipboard_item("new", ContentKind::Url, 20);
    state.detail_ref = "old";
    auto selected_model = build_clipboard_history_model({new_item, old_item}, state);
    assert(selected_model.detail.has_value());
    assert(selected_model.detail->ref == "old");
    assert(selected_model.detail->type_label == "Text");
    assert(selected_model.detail->mime_summary == "text/plain");
    assert(selected_model.rows[1].selected);

    old_item.captured_at_ms = 30;
    selected_model = build_clipboard_history_model({old_item, new_item}, state);
    assert(selected_model.rows.front().ref == "old");
    assert(selected_model.detail.has_value());
    assert(selected_model.detail->ref == "old");
    assert(state.detail_ref == "old");

    selected_model = build_clipboard_history_model({new_item}, state);
    assert(!selected_model.detail.has_value());
    assert(state.detail_ref.empty());

    const auto view = view_clipboard_history_item(state, "clip_54");
    assert(view.kind == ClipboardHistoryCommandKind::View);
    assert(view.ref == "clip_54");
    assert(state.detail_ref == "clip_54");

    const auto use = use_clipboard_history_item("clip_54");
    assert(use.kind == ClipboardHistoryCommandKind::Use);
    assert(use.ref == "clip_54");

    const auto copy = copy_clipboard_history_item("clip_54");
    assert(copy.kind == ClipboardHistoryCommandKind::Copy);
    assert(copy.ref == "clip_54");

    const auto save = save_clipboard_history_item("clip_54");
    assert(save.kind == ClipboardHistoryCommandKind::Save);
    assert(save.ref == "clip_54");

    const auto close = close_clipboard_history_detail(state);
    assert(close.kind == ClipboardHistoryCommandKind::CloseDetail);
    assert(close.ref == "clip_54");
    assert(state.detail_ref.empty());

    const ClipboardHistoryCommand none;
    assert(none.kind == ClipboardHistoryCommandKind::None);
    assert(none.ref.empty());
}
