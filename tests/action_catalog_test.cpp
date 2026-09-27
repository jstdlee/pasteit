#include "test_env.hpp"
#include "actions/action_catalog.hpp"
#include "storage/clipboard_store.hpp"

#include <cassert>
#include <cstddef>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace {

pasteit::ClipboardItem item(std::string ref, pasteit::ContentKind kind, std::string preview) {
    pasteit::ClipboardItem out;
    out.ref = std::move(ref);
    out.kind = kind;
    out.preview = std::move(preview);
    out.size_bytes = out.preview.size();
    out.mime_types = {"text/plain"};
    return out;
}

pasteit::PathLocation path(std::string ref, std::filesystem::path value, std::int64_t seen) {
    pasteit::PathLocation out;
    out.ref = std::move(ref);
    out.path = std::move(value);
    out.kind = pasteit::PathKind::Directory;
    out.last_seen_ms = seen;
    out.source = "test";
    out.exists = true;
    return out;
}

std::size_t count_kind(const pasteit::ActionCatalog& catalog, pasteit::ActionKind kind, std::string source_ref = {}) {
    std::size_t count = 0;
    for (const auto& action : catalog.actions) {
        if (action.kind == kind && (source_ref.empty() || action.source_ref == source_ref)) {
            ++count;
        }
    }
    return count;
}

bool has_target(const pasteit::ActionCatalog& catalog, pasteit::ActionKind kind, const std::string& target_ref) {
    for (const auto& action : catalog.actions) {
        if (action.kind == kind && action.target_ref == target_ref) {
            return true;
        }
    }
    return false;
}

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (const unsigned char ch : text) {
        out.push_back(static_cast<std::byte>(ch));
    }
    return out;
}

pasteit::DecisionSnapshot with_current_item(const pasteit::DecisionSnapshot& snapshot, std::size_t index) {
    auto out = snapshot;
    out.clipboard_items = {snapshot.clipboard_items.at(index)};
    return out;
}

}  // namespace

int main() {
    using namespace pasteit;

    DecisionSnapshot snapshot;
    snapshot.clipboard_hash = "clip_hash";
    snapshot.focused_target_hash = "target_hash";
    snapshot.captured_at_ms = 1234;
    snapshot.recent_paths = {
        path("path_01", std::filesystem::temp_directory_path(), 300),
        path("path_02", std::filesystem::temp_directory_path() / "other", 200),
    };
    snapshot.clipboard_items = {
        item("clip_text", ContentKind::Text, "hello pasteit"),
        item("clip_image", ContentKind::Image, "thumbnail only"),
        item("clip_url", ContentKind::Url, "https://example.com/report.pdf"),
        item("clip_email", ContentKind::Email, "jane@example.com"),
        item("clip_path", ContentKind::Path, "/tmp/source.txt"),
        item("clip_json", ContentKind::Json, "{\"a\":1}"),
        item("clip_resume", ContentKind::Text,
             "Jane Doe\njane@example.com\n+65 9123 4567\nSkills\nC++, Linux, SQLite\n"),
        item("clip_invalid_json", ContentKind::Text, "{\"a\":"),
        item("clip_invalid_marked_json", ContentKind::Json, "{\"a\":"),
    };
    snapshot.clipboard_items[1].mime_types = {"image/png"};

    const auto text_catalog = build_catalog(with_current_item(snapshot, 0));
    const auto image_catalog = build_catalog(with_current_item(snapshot, 1));
    const auto url_catalog = build_catalog(with_current_item(snapshot, 2));
    const auto email_catalog = build_catalog(with_current_item(snapshot, 3));
    const auto path_catalog = build_catalog(with_current_item(snapshot, 4));
    const auto json_catalog = build_catalog(with_current_item(snapshot, 5));
    const auto resume_catalog = build_catalog(with_current_item(snapshot, 6));
    const auto invalid_text_catalog = build_catalog(with_current_item(snapshot, 7));
    const auto invalid_json_catalog = build_catalog(with_current_item(snapshot, 8));

    assert(count_kind(text_catalog, ActionKind::PasteText, "clip_text") >= 1);
    assert(count_kind(text_catalog, ActionKind::SaveTextFile, "clip_text") >= 1);
    assert(count_kind(image_catalog, ActionKind::PasteImage, "clip_image") == 0);
    assert(count_kind(image_catalog, ActionKind::SaveImageFile, "clip_image") >= 1);
    assert(count_kind(image_catalog, ActionKind::CopyTemporaryImagePath, "clip_image") == 1);
    assert(count_kind(url_catalog, ActionKind::PasteUrl, "clip_url") >= 1);
    assert(count_kind(url_catalog, ActionKind::OpenUrl, "clip_url") >= 1);
    assert(count_kind(url_catalog, ActionKind::DownloadUrl, "clip_url") >= 1);
    assert(count_kind(url_catalog, ActionKind::SaveUrlFile, "clip_url") >= 1);
    assert(count_kind(email_catalog, ActionKind::PasteEmail, "clip_email") >= 1);
    assert(count_kind(email_catalog, ActionKind::SaveEmailFile, "clip_email") >= 1);
    assert(count_kind(email_catalog, ActionKind::ComposeEmail, "clip_email") >= 1);
    assert(count_kind(email_catalog, ActionKind::SendEmail, "clip_email") >= 1);
    assert(count_kind(path_catalog, ActionKind::CopyPath, "clip_path") == 0);
    assert(count_kind(path_catalog, ActionKind::CopyPathToDirectory, "clip_path") >= 1);
    assert(count_kind(path_catalog, ActionKind::MovePath, "clip_path") >= 1);
    assert(count_kind(json_catalog, ActionKind::PrettyJson, "clip_json") >= 1);
    assert(count_kind(json_catalog, ActionKind::SaveJsonFile, "clip_json") >= 1);
    assert(count_kind(json_catalog, ActionKind::SaveJsonPrettyFile, "clip_json") >= 1);
    assert(count_kind(resume_catalog, ActionKind::CopyResumeField, "clip_resume") >= 4);
    assert(count_kind(resume_catalog, ActionKind::CopyResumeAsJson, "clip_resume") == 1);
    assert(count_kind(resume_catalog, ActionKind::SaveResumeFile, "clip_resume") >= 1);
    const auto resume_email = resume_catalog.find_by_kind_and_label(ActionKind::CopyResumeField, "Copy email");
    assert(resume_email.has_value());
    assert(resume_email->parameters.at("field_kind") == "email");
    assert(resume_email->parameters.at("value") == "jane@example.com");

    assert(has_target(text_catalog, ActionKind::SaveTextFile, "path_01"));
    assert(has_target(image_catalog, ActionKind::SaveImageFile, "path_01"));
    const auto save_image = image_catalog.find_by_kind_and_label(ActionKind::SaveImageFile, "Save image");
    assert(save_image.has_value());
    assert(save_image->filename == "clipboard-image.png");
    assert(has_target(json_catalog, ActionKind::SaveJsonPrettyFile, "path_01"));

    assert(count_kind(invalid_text_catalog, ActionKind::PrettyJson, "clip_invalid_json") == 0);
    assert(count_kind(invalid_text_catalog, ActionKind::SaveJsonPrettyFile, "clip_invalid_json") == 0);
    assert(count_kind(invalid_json_catalog, ActionKind::PrettyJson, "clip_invalid_marked_json") == 0);
    assert(count_kind(invalid_json_catalog, ActionKind::SaveJsonPrettyFile, "clip_invalid_marked_json") == 0);
    assert(count_kind(invalid_json_catalog, ActionKind::PasteText, "clip_invalid_marked_json") >= 1);
    assert(count_kind(invalid_json_catalog, ActionKind::SaveTextFile, "clip_invalid_marked_json") >= 1);

    std::set<std::string> ids;
    for (const auto& action : text_catalog.actions) {
        assert(!action.id.empty());
        assert(ids.insert(action.id).second);
        assert(text_catalog.find(action.id).has_value());
    }

    const auto store_root = std::filesystem::temp_directory_path() / "pasteit_long_catalog_test";
    pasteit_test::remove_tree(store_root);
    ClipboardStore store(store_root);
    const std::string long_json = "{\"padding\":\"" + std::string(300, 'x') + "\",\"answer\":42}";
    const auto long_json_item = store.put(ClipboardData{
        .mime_types = {"application/json"},
        .bytes = bytes(long_json),
        .kind = ContentKind::Json,
        .source_app = "test",
        .captured_at_ms = 2000,
    });
    assert(long_json_item.preview.size() < long_json.size());

    const std::string long_resume = "Jane Doe\n" + std::string(240, 'x') +
                                    "\njane@example.com\n+65 9123 4567\nSkills\nC++, Linux, SQLite\n";
    const auto long_resume_item = store.put(ClipboardData{
        .mime_types = {"text/plain"},
        .bytes = bytes(long_resume),
        .kind = ContentKind::Text,
        .source_app = "test",
        .captured_at_ms = 2001,
    });
    assert(long_resume_item.preview.find("jane@example.com") == std::string::npos);

    DecisionSnapshot long_snapshot;
    long_snapshot.clipboard_items = {long_json_item, long_resume_item};
    long_snapshot.recent_paths = {path("path_long", std::filesystem::temp_directory_path(), 400)};
    const auto long_json_catalog = build_catalog(with_current_item(long_snapshot, 0));
    const auto long_resume_catalog = build_catalog(with_current_item(long_snapshot, 1));
    assert(count_kind(long_json_catalog, ActionKind::PrettyJson, long_json_item.ref) == 1);
    assert(count_kind(long_resume_catalog, ActionKind::CopyResumeField, long_resume_item.ref) >= 4);
    pasteit_test::remove_tree(store_root);
}
