#include "app/file_operation_confirmation.hpp"

#include "core/action.hpp"
#include "core/types.hpp"
#include "storage/clipboard_store.hpp"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

class TempDirectory {
public:
    explicit TempDirectory(std::string_view label) {
        std::random_device random;
        const auto base = std::filesystem::temp_directory_path();
        for (std::size_t attempt = 0; attempt < 100; ++attempt) {
            path_ = base / (std::string{label} + '-' + std::to_string(random()) + '-' + std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
        }
        throw std::runtime_error("failed to create unique test directory");
    }

    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::chrono::system_clock::time_point fixed_time() {
#if defined(_MSC_VER)
    _putenv_s("TZ", "UTC");
    _tzset();
#else
    setenv("TZ", "UTC", 1);
    tzset();
#endif
    using namespace std::chrono;
    return sys_days{year{2026} / 9 / 20} + hours{13} + minutes{42};
}

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (const unsigned char ch : text) {
        out.push_back(static_cast<std::byte>(ch));
    }
    return out;
}

pasteit::ClipboardItem put_text(
    pasteit::ClipboardStore& store,
    std::string_view text,
    pasteit::ContentKind kind) {
    return store.put(pasteit::ClipboardData{
        .mime_types = {"text/plain"},
        .bytes = bytes(text),
        .kind = kind,
        .source_app = "confirmation-test",
        .captured_at_ms = 700,
    });
}

pasteit::ActionInstance action(
    pasteit::ActionKind kind,
    const std::string& source_ref,
    const std::string& target_ref = {}) {
    pasteit::ActionInstance out;
    out.id = "action_under_test";
    out.kind = kind;
    out.source_ref = source_ref;
    out.target_ref = target_ref;
    out.filename = "model-output.txt";
    out.representation = "raw";
    out.label = "Save";
    out.enabled = true;
    return out;
}

pasteit::PathLocation destination(
    std::string ref,
    const std::filesystem::path& path,
    bool exists = true) {
    return pasteit::PathLocation{
        .ref = std::move(ref),
        .path = path,
        .kind = pasteit::PathKind::Directory,
        .last_seen_ms = 900,
        .source = "test",
        .exists = exists,
    };
}

void create_test_directories(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "chosen");
    std::filesystem::create_directories(root / "fallback");
}

void assert_cannot_confirm(pasteit::FileOperationDraft& draft, const pasteit::ClipboardStore& store) {
    assert(!pasteit::validate_file_operation_draft(draft, store));
    assert(!draft.validation_error.empty());
    assert(!pasteit::confirmed_action(draft, store).has_value());
}

}  // namespace

int main() {
    using namespace pasteit;

    assert(needs_file_confirmation(ActionKind::SaveTextFile));
    assert(needs_file_confirmation(ActionKind::SaveImageFile));
    assert(needs_file_confirmation(ActionKind::SaveUrlFile));
    assert(needs_file_confirmation(ActionKind::SaveEmailFile));
    assert(needs_file_confirmation(ActionKind::DownloadUrl));
    assert(needs_file_confirmation(ActionKind::CopyPathToDirectory));
    assert(needs_file_confirmation(ActionKind::MovePath));
    assert(needs_file_confirmation(ActionKind::SaveJsonFile));
    assert(needs_file_confirmation(ActionKind::SaveJsonPrettyFile));
    assert(needs_file_confirmation(ActionKind::SaveResumeFile));

    assert(!needs_file_confirmation(ActionKind::PasteText));
    assert(!needs_file_confirmation(ActionKind::PasteImage));
    assert(!needs_file_confirmation(ActionKind::PasteUrl));
    assert(!needs_file_confirmation(ActionKind::OpenUrl));
    assert(!needs_file_confirmation(ActionKind::PasteEmail));
    assert(!needs_file_confirmation(ActionKind::ComposeEmail));
    assert(!needs_file_confirmation(ActionKind::SendEmail));
    assert(!needs_file_confirmation(ActionKind::CopyPath));
    assert(!needs_file_confirmation(ActionKind::PrettyJson));
    assert(!needs_file_confirmation(ActionKind::TransformText));

    const TempDirectory temporary_directory{"pasteit-file-operation-confirmation-test"};
    const auto& root = temporary_directory.path();
    create_test_directories(root);

    ClipboardStore store(root / "store");
    const auto text_item = put_text(store, "hello", ContentKind::Text);
    std::vector<PathLocation> candidates{
        destination("fallback", root / "fallback"),
        destination("chosen", root / "chosen"),
    };
    const auto source_action = action(ActionKind::SaveTextFile, text_item.ref, "chosen");
    auto draft = make_file_operation_draft(source_action, text_item, candidates, fixed_time());

    assert(draft.frozen_action.id == source_action.id);
    assert(draft.frozen_action.source_ref == text_item.ref);
    assert(draft.source_ref == text_item.ref);
    assert(draft.destination == root / "chosen");
    assert(draft.filename == "2026092013-01.txt");
    assert(draft.candidate_destinations.size() == 2);
    assert(draft.validation_error.empty());

    draft.filename = "renamed cafe.txt";
    assert(validate_file_operation_draft(draft, store));
    assert(draft.validation_error.empty());

    const auto confirmed = confirmed_action(draft, store);
    assert(confirmed.has_value());
    assert(confirmed->filename == "renamed cafe.txt");
    assert(confirmed->parameters.at("confirmed_destination") == (root / "chosen").string());
    assert(confirmed->parameters.at("confirmed_filename") == "renamed cafe.txt");
    assert(source_action.filename == "model-output.txt");
    assert(draft.frozen_action.filename == "model-output.txt");

    draft.filename = "resume cafe \xc3\xa9.txt";
    assert(validate_file_operation_draft(draft, store));
    assert(draft.filename == "resume cafe \xc3\xa9.txt");

    for (const std::string invalid : {"", ".", "..", "nested/file.txt", "../escape.txt"}) {
        auto invalid_draft = draft;
        invalid_draft.filename = invalid;
        assert_cannot_confirm(invalid_draft, store);
    }

    auto nul_draft = draft;
    nul_draft.filename = std::string{"bad", 3} + '\0' + ".txt";
    assert_cannot_confirm(nul_draft, store);

    auto missing_destination = draft;
    missing_destination.destination = root / "missing";
    missing_destination.filename = "valid.txt";
    assert_cannot_confirm(missing_destination, store);

    ClipboardStore empty_store(root / "empty-store");
    auto stale_source = draft;
    stale_source.filename = "valid.txt";
    assert_cannot_confirm(stale_source, empty_store);

    const auto source_path = root / "source.txt";
    {
        std::ofstream out(source_path);
        out << "copy me";
    }
    const auto path_item = put_text(store, source_path.string(), ContentKind::Path);
    auto copy_draft =
        make_file_operation_draft(action(ActionKind::CopyPathToDirectory, path_item.ref, "chosen"), path_item, candidates, fixed_time());
    assert(copy_draft.filename == "source.txt");
    copy_draft.filename = "copied-renamed.md";
    const auto confirmed_copy = confirmed_action(copy_draft, store);
    assert(confirmed_copy.has_value());
    assert(confirmed_copy->kind == ActionKind::CopyPathToDirectory);
    assert(confirmed_copy->filename == "copied-renamed.md");
    assert(confirmed_copy->parameters.at("confirmed_filename") == "copied-renamed.md");

    auto move_draft =
        make_file_operation_draft(action(ActionKind::MovePath, path_item.ref, "chosen"), path_item, candidates, fixed_time());
    assert(move_draft.filename == "source.txt");
    move_draft.filename = "moved-renamed.md";
    const auto confirmed_move = confirmed_action(move_draft, store);
    assert(confirmed_move.has_value());
    assert(confirmed_move->kind == ActionKind::MovePath);
    assert(confirmed_move->parameters.at("confirmed_filename") == "moved-renamed.md");

    const auto long_uri = std::string{"file:///"} + std::string(200, 'a') + "/full-source-name.txt";
    const auto long_path_item = put_text(store, long_uri, ContentKind::Path);
    assert(long_path_item.preview.find("full-source-name.txt") == std::string::npos);
    const auto long_path_draft = make_file_operation_draft(
        action(ActionKind::CopyPathToDirectory, long_path_item.ref, "chosen"),
        long_path_item,
        candidates,
        fixed_time());
    assert(long_path_draft.filename == "full-source-name.txt");
}
