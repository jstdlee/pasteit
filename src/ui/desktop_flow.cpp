#include "ui/desktop_flow.hpp"
#include "decision/candidate_selector.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iterator>
#include <set>
#include <sstream>
#include <string_view>

namespace pastit {
namespace {

void hash_append(std::uint64_t& hash, std::string_view value) {
    for (const unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
}

std::string snapshot_hash(const std::vector<ClipboardItem>& items) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto& item : items) {
        hash_append(hash, item.ref);
        hash_append(hash, item.preview);
        hash_append(hash, std::to_string(item.size_bytes));
        hash_append(hash, std::to_string(item.captured_at_ms));
    }
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

std::string lowercase_ascii(std::string value) {
    for (auto& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool contains_algorithm(const std::vector<std::string>& algorithms, std::string_view expected) {
    return std::any_of(algorithms.begin(), algorithms.end(), [&](const auto& value) {
        return lowercase_ascii(value) == expected;
    });
}

}  // namespace

DesktopDecisionBatch build_desktop_decision(const DesktopDecisionInput& input) {
    DesktopDecisionBatch batch;
    batch.general_llm = input.general_llm;
    batch.request.protocol_version = 1;
    batch.request.request_id = input.request_id;

    auto& snapshot = batch.request.snapshot;
    snapshot.captured_at_ms = input.captured_at_ms;
    snapshot.focused_target_hash = input.focused_target_hash;
    snapshot.focused_app = input.focused_app;
    snapshot.focused_window_title = input.focused_window_title;
    snapshot.focused_current_directory = input.focused_current_directory.string();
    snapshot.clipboard_items = input.clipboard_items;
    std::stable_sort(snapshot.clipboard_items.begin(), snapshot.clipboard_items.end(), [](const ClipboardItem& left, const ClipboardItem& right) {
        return left.captured_at_ms > right.captured_at_ms;
    });
    if (snapshot.clipboard_items.size() > 1) {
        snapshot.clipboard_items.resize(1);
    }
    snapshot.clipboard_hash = snapshot_hash(snapshot.clipboard_items);
    snapshot.recent_paths = input.recent_paths;
    std::set<std::string> default_text_refs;
    std::set<std::string> default_image_refs;
    std::set<std::string> default_download_refs;
    const auto add_default = [&](const std::filesystem::path& path, std::string ref, std::string source,
                                 std::set<std::string>& refs) {
        if (path.empty()) return;
        std::error_code error;
        if (!std::filesystem::is_directory(path, error)) return;
        const auto duplicate=std::find_if(snapshot.recent_paths.begin(),snapshot.recent_paths.end(),[&](const auto& item){return item.path==path;});
        if (duplicate != snapshot.recent_paths.end()) {
            duplicate->last_seen_ms=input.captured_at_ms;
            duplicate->source=source;
            refs.insert(duplicate->ref);
        }
        else { refs.insert(ref); snapshot.recent_paths.push_back({.ref=std::move(ref),.path=path,.kind=PathKind::Directory,.last_seen_ms=input.captured_at_ms,.source=std::move(source),.exists=true}); }
    };
    add_default(input.default_text_directory,"default_text","configured-text",default_text_refs);
    add_default(input.default_image_directory,"default_image","configured-image",default_image_refs);
    add_default(input.downloads.resume_directory,"default_download","configured-download",default_download_refs);

    batch.catalog = build_catalog(snapshot, input.prompt_templates, input.general_llm);
    const bool show_sha256 = contains_algorithm(input.hash.default_algorithms, "sha256");
    const bool show_sha512 = contains_algorithm(input.hash.default_algorithms, "sha512");
    std::erase_if(batch.catalog.actions, [&](const ActionInstance& action) {
        return (action.kind == ActionKind::HashSha256 && !show_sha256) ||
               (action.kind == ActionKind::HashSha512 && !show_sha512);
    });
    for (auto& action : batch.catalog.actions) {
        if (action.kind == ActionKind::SendEmail) {
            action.enabled = input.direct_send_available;
        }
        if (action.kind == ActionKind::ConvertTimezone || action.kind == ActionKind::ToUnixTimestamp ||
            action.kind == ActionKind::CopyNormalizedDateTime) {
            if (!input.date_time.source_zone.empty()) {
                action.parameters["source_zone"] = input.date_time.source_zone;
            }
        }
        if (action.kind == ActionKind::ConvertTimezone) {
            if (!input.date_time.target_zone.empty()) {
                action.parameters["target_zone"] = input.date_time.target_zone;
            }
        }
        const bool text_save = action.kind == ActionKind::SaveTextFile || action.kind == ActionKind::SaveUrlFile ||
                               action.kind == ActionKind::SaveEmailFile || action.kind == ActionKind::SaveJsonFile ||
                               action.kind == ActionKind::SaveJsonPrettyFile || action.kind == ActionKind::SaveResumeFile;
        if (default_text_refs.contains(action.target_ref) && !text_save) action.enabled = false;
        if (default_image_refs.contains(action.target_ref) && action.kind != ActionKind::SaveImageFile) action.enabled = false;
        const bool url_download = action.kind == ActionKind::DownloadUrl || action.kind == ActionKind::SaveUrlFile;
        if (default_download_refs.contains(action.target_ref) && !url_download) action.enabled = false;
    }
    const auto candidates = select_djev_candidates(batch.catalog, snapshot, 26, input.action_preferences);
    snapshot.available_actions = candidates.actions;
    return batch;
}

}  // namespace pastit
