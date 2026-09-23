#include "ui/recent_paths_model.hpp"
#include "util/path_utf8.hpp"
#include "util/utf8.hpp"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace pastit {
namespace {

std::string abbreviate_middle_impl(std::string_view value, std::size_t max_bytes) {
    constexpr std::string_view marker = "...";
    if (value.size() <= max_bytes || max_bytes <= marker.size()) {
        return max_bytes <= marker.size() ? utf8_prefix_bytes(value, max_bytes) : std::string{value};
    }
    const auto remaining = max_bytes - marker.size();
    const auto prefix = utf8_prefix_bytes(value, remaining / 2);
    auto suffix_start = value.size() - (remaining - prefix.size());
    while (suffix_start < value.size() &&
           (static_cast<unsigned char>(value[suffix_start]) & 0xC0U) == 0x80U) {
        ++suffix_start;
    }
    return prefix + std::string{marker} + std::string{value.substr(suffix_start)};
}

std::string display_name(const std::filesystem::path& path) {
    const auto filename = path_to_utf8_string(path.filename());
    if (!filename.empty()) {
        return filename;
    }
    return path_to_utf8_string(path);
}

std::string action_id_for(const ActionCatalog& catalog, ActionKind kind, const std::string& target_ref) {
    for (const auto& action : catalog.actions) {
        if (action.enabled && action.kind == kind && action.target_ref == target_ref) {
            return action.id;
        }
    }
    return {};
}

RecentPathRow row_from_path(const PathLocation& value, const ActionCatalog& catalog, bool selected) {
    const bool directory = value.kind == PathKind::Directory;
    const auto copy_here_id = action_id_for(catalog, ActionKind::CopyPathToDirectory, value.ref);
    const auto move_here_id = action_id_for(catalog, ActionKind::MovePath, value.ref);
    return {
        .ref = value.ref,
        .type = directory ? "folder" : "file",
        .type_label = directory ? "folder" : "file",
        .name = display_name(value.path),
        .path = path_to_utf8_string(value.path),
        .display_path = abbreviate_middle_impl(path_to_utf8_string(value.path), 72),
        .parent_path = path_to_utf8_string(value.path.parent_path()),
        .source = value.source,
        .last_seen_label = std::to_string(value.last_seen_ms),
        .copy_here_action_id = copy_here_id,
        .move_here_action_id = move_here_id,
        .last_seen_ms = value.last_seen_ms,
        .exists = value.exists,
        .selected = selected,
        .can_copy_path = true,
        .can_open = value.exists,
        .can_open_parent = value.exists && !directory && !value.path.parent_path().empty(),
        .can_copy_here = value.exists && directory && !copy_here_id.empty(),
        .can_move_here = value.exists && directory && !move_here_id.empty(),
        .can_use_as_destination = value.exists && directory,
    };
}

RecentPathDetail detail_from_row(const RecentPathRow& row) {
    return {
        .ref = row.ref,
        .type_label = row.type_label,
        .full_path = row.path,
        .parent_path = row.parent_path,
        .source = row.source,
        .last_seen_label = row.last_seen_label,
        .copy_here_action_id = row.copy_here_action_id,
        .move_here_action_id = row.move_here_action_id,
        .last_seen_ms = row.last_seen_ms,
        .exists = row.exists,
        .can_copy_path = row.can_copy_path,
        .can_open = row.can_open,
        .can_open_parent = row.can_open_parent,
        .can_copy_here = row.can_copy_here,
        .can_move_here = row.can_move_here,
        .can_use_as_destination = row.can_use_as_destination,
    };
}

const RecentPathRow* find_row(const RecentPathsModel& model, std::string_view ref) {
    const auto row = std::find_if(model.rows.begin(), model.rows.end(), [&](const RecentPathRow& candidate) {
        return candidate.ref == ref;
    });
    return row == model.rows.end() ? nullptr : &*row;
}

RecentPathCommand command(RecentPathCommandKind kind, std::string_view path_ref, std::string action_id = {}) {
    return {.kind = kind, .path_ref = std::string(path_ref), .action_id = std::move(action_id)};
}

}  // namespace

std::string abbreviate_middle(std::string_view value, std::size_t max_bytes) {
    return abbreviate_middle_impl(value, max_bytes);
}

RecentPathsModel build_recent_paths_model(const std::vector<PathLocation>& paths,
                                          RecentPathsState& state,
                                          const ActionCatalog& catalog) {
    RecentPathsModel model;
    model.rows.reserve(paths.size());
    for (const auto& value : paths) {
        model.rows.push_back(row_from_path(value, catalog, !state.detail_ref.empty() && value.ref == state.detail_ref));
    }
    std::stable_sort(model.rows.begin(), model.rows.end(), [](const RecentPathRow& left, const RecentPathRow& right) {
        return left.last_seen_ms > right.last_seen_ms;
    });

    if (!state.detail_ref.empty()) {
        const auto* selected = find_row(model, state.detail_ref);
        if (selected == nullptr) {
            state.detail_ref.clear();
        } else {
            model.detail = detail_from_row(*selected);
        }
    }

    return model;
}

RecentPathsModel build_recent_paths_model(const std::vector<PathLocation>& paths, bool source_path_selected) {
    ActionCatalog catalog;
    if (source_path_selected) {
        for (const auto& value : paths) {
            if (value.kind != PathKind::Directory || !value.exists) {
                continue;
            }
            ActionInstance copy_action;
            copy_action.id = "copy_here_" + value.ref;
            copy_action.kind = ActionKind::CopyPathToDirectory;
            copy_action.target_ref = value.ref;
            copy_action.enabled = true;
            catalog.actions.push_back(std::move(copy_action));

            ActionInstance move_action;
            move_action.id = "move_here_" + value.ref;
            move_action.kind = ActionKind::MovePath;
            move_action.target_ref = value.ref;
            move_action.enabled = true;
            catalog.actions.push_back(std::move(move_action));
        }
    }
    RecentPathsState state;
    return build_recent_paths_model(paths, state, catalog);
}

RecentPathCommand view_recent_path(RecentPathsState& state, std::string_view ref) {
    state.detail_ref = std::string(ref);
    return command(RecentPathCommandKind::View, ref);
}

RecentPathCommand copy_recent_path(const RecentPathsModel& model, std::string_view ref) {
    const auto* row = find_row(model, ref);
    if (row == nullptr || !row->can_copy_path) {
        return {};
    }
    return command(RecentPathCommandKind::CopyPath, row->ref);
}

RecentPathCommand open_recent_path(const RecentPathsModel& model, std::string_view ref) {
    const auto* row = find_row(model, ref);
    if (row == nullptr || !row->can_open) {
        return {};
    }
    return command(RecentPathCommandKind::Open, row->ref);
}

RecentPathCommand open_recent_path_parent(const RecentPathsModel& model, std::string_view ref) {
    const auto* row = find_row(model, ref);
    if (row == nullptr || !row->can_open_parent) {
        return {};
    }
    return command(RecentPathCommandKind::OpenParent, row->ref);
}

RecentPathCommand copy_here_recent_path(const RecentPathsModel& model, std::string_view ref) {
    const auto* row = find_row(model, ref);
    if (row == nullptr || !row->can_copy_here) {
        return {};
    }
    return command(RecentPathCommandKind::CopyHere, row->ref, row->copy_here_action_id);
}

RecentPathCommand move_here_recent_path(const RecentPathsModel& model, std::string_view ref) {
    const auto* row = find_row(model, ref);
    if (row == nullptr || !row->can_move_here) {
        return {};
    }
    return command(RecentPathCommandKind::MoveHere, row->ref, row->move_here_action_id);
}

RecentPathCommand use_recent_path_as_destination(const RecentPathsModel& model, std::string_view ref) {
    const auto* row = find_row(model, ref);
    if (row == nullptr || !row->can_use_as_destination) {
        return {};
    }
    return command(RecentPathCommandKind::UseAsDestination, row->ref);
}

RecentPathCommand close_recent_path_detail(RecentPathsState& state) {
    const auto ref = state.detail_ref;
    state.detail_ref.clear();
    return command(RecentPathCommandKind::CloseDetail, ref);
}

}  // namespace pastit
