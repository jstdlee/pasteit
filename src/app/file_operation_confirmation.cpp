#include "app/file_operation_confirmation.hpp"
#include "util/path_utf8.hpp"

#include "app/generated_filename.hpp"
#include "history/path_history.hpp"
#include "storage/clipboard_store.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

namespace pastit {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::filesystem::path selected_destination(
    const ActionInstance& action,
    const std::vector<PathLocation>& destinations) {
    if (!action.target_ref.empty()) {
        const auto found = std::find_if(destinations.begin(), destinations.end(), [&](const PathLocation& destination) {
            return destination.ref == action.target_ref && destination.kind == PathKind::Directory;
        });
        if (found != destinations.end()) {
            return found->path;
        }
    }

    const auto existing = std::find_if(destinations.begin(), destinations.end(), [](const PathLocation& destination) {
        return destination.kind == PathKind::Directory && destination.exists;
    });
    if (existing != destinations.end()) {
        return existing->path;
    }

    const auto any_directory = std::find_if(destinations.begin(), destinations.end(), [](const PathLocation& destination) {
        return destination.kind == PathKind::Directory;
    });
    if (any_directory != destinations.end()) {
        return any_directory->path;
    }

    return {};
}

std::string source_basename(const ClipboardItem& item) {
    std::string source = item.preview;
    if (!item.blob_path.empty()) {
        std::ifstream blob(item.blob_path, std::ios::binary);
        if (blob) {
            source.assign(std::istreambuf_iterator<char>{blob}, std::istreambuf_iterator<char>{});
        }
    }

    const auto paths = paths_from_uri_list(source);
    if (!paths.empty() && !paths.front().filename().empty()) {
        return path_to_utf8_string(paths.front().filename());
    }

    const auto path_text = trim(std::move(source));
    if (!path_text.empty()) {
        const auto filename = path_to_utf8_string(path_from_utf8_string(path_text).filename());
        if (!filename.empty()) {
            return filename;
        }
    }

    return "clipboard.bin";
}

std::string default_filename(
    const ActionInstance& action,
    const ClipboardItem& item,
    std::chrono::system_clock::time_point now,
    const std::filesystem::path& destination) {
    switch (action.kind) {
        case ActionKind::CopyPathToDirectory:
        case ActionKind::MovePath:
            return source_basename(item);
        default:
            return generated_filename(action.kind, item, now, destination);
    }
}

bool filename_is_valid(const std::string& filename, std::string& error) {
    if (filename.empty()) {
        error = "filename is required";
        return false;
    }
    if (filename == "." || filename == "..") {
        error = "filename cannot be a traversal segment";
        return false;
    }
    for (const char ch : filename) {
        if (ch == '\0') {
            error = "filename cannot contain NUL";
            return false;
        }
        if (ch == '/' || ch == '\\') {
            error = "filename cannot contain path separators";
            return false;
        }
    }
    if (path_from_utf8_string(filename).is_absolute()) {
        error = "filename must be relative";
        return false;
    }
    return true;
}

bool destination_is_valid(const std::filesystem::path& destination, std::string& error) {
    if (destination.empty()) {
        error = "destination is required";
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(destination, ec) || ec) {
        error = "destination does not exist";
        return false;
    }
    if (!std::filesystem::is_directory(destination, ec) || ec) {
        error = "destination must be a directory";
        return false;
    }
    return true;
}

bool draft_is_valid(const FileOperationDraft& draft, const ClipboardStore& store, std::string& error) {
    if (draft.source_ref.empty() || draft.source_ref != draft.frozen_action.source_ref) {
        error = "frozen source does not match action";
        return false;
    }
    if (!store.item(draft.source_ref).has_value()) {
        error = "frozen source no longer exists";
        return false;
    }
    if (!destination_is_valid(draft.destination, error)) {
        return false;
    }
    if (!filename_is_valid(draft.filename, error)) {
        return false;
    }
    error.clear();
    return true;
}

}  // namespace

bool needs_file_confirmation(ActionKind action_kind) {
    switch (action_kind) {
        case ActionKind::SaveTextFile:
        case ActionKind::SaveImageFile:
        case ActionKind::DownloadUrl:
        case ActionKind::SaveUrlFile:
        case ActionKind::SaveEmailFile:
        case ActionKind::CopyPathToDirectory:
        case ActionKind::MovePath:
        case ActionKind::SaveJsonFile:
        case ActionKind::SaveJsonPrettyFile:
        case ActionKind::SaveResumeFile:
            return true;
        default:
            return false;
    }
}

FileOperationDraft make_file_operation_draft(
    const ActionInstance& action,
    const ClipboardItem& item,
    std::vector<PathLocation> candidate_destinations,
    std::chrono::system_clock::time_point now) {
    FileOperationDraft draft;
    draft.frozen_action = action;
    draft.source_ref = item.ref;
    draft.destination = selected_destination(action, candidate_destinations);
    draft.filename = default_filename(action, item, now, draft.destination);
    draft.candidate_destinations = std::move(candidate_destinations);
    return draft;
}

bool validate_file_operation_draft(FileOperationDraft& draft, const ClipboardStore& store) {
    std::string error;
    const bool valid = draft_is_valid(draft, store, error);
    draft.validation_error = std::move(error);
    return valid;
}

std::optional<ActionInstance> confirmed_action(const FileOperationDraft& draft, const ClipboardStore& store) {
    std::string error;
    if (!draft_is_valid(draft, store, error)) {
        return std::nullopt;
    }

    auto confirmed = draft.frozen_action;
    confirmed.filename = draft.filename;
    confirmed.parameters["confirmed_destination"] = path_to_utf8_string(draft.destination);
    confirmed.parameters["confirmed_filename"] = draft.filename;
    return confirmed;
}

}  // namespace pastit
