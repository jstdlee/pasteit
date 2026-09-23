#pragma once

#include "actions/action_catalog.hpp"
#include "core/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pastit {

enum class RecentPathCommandKind {
    None,
    View,
    CopyPath,
    Open,
    OpenParent,
    CopyHere,
    MoveHere,
    UseAsDestination,
    CloseDetail,
};

struct RecentPathCommand {
    RecentPathCommandKind kind = RecentPathCommandKind::None;
    std::string path_ref;
    std::string action_id;
};

struct RecentPathsState {
    std::string detail_ref;
};

struct RecentPathRow {
    std::string ref;
    std::string type;
    std::string type_label;
    std::string name;
    std::string path;
    std::string display_path;
    std::string parent_path;
    std::string source;
    std::string last_seen_label;
    std::string copy_here_action_id;
    std::string move_here_action_id;
    std::int64_t last_seen_ms = 0;
    bool exists = false;
    bool selected = false;
    bool can_copy_path = true;
    bool can_open = false;
    bool can_open_parent = false;
    bool can_copy_here = false;
    bool can_move_here = false;
    bool can_use_as_destination = false;
};

std::string abbreviate_middle(std::string_view value, std::size_t max_bytes = 72);

struct RecentPathDetail {
    std::string ref;
    std::string type_label;
    std::string full_path;
    std::string parent_path;
    std::string source;
    std::string last_seen_label;
    std::string copy_here_action_id;
    std::string move_here_action_id;
    std::int64_t last_seen_ms = 0;
    bool exists = false;
    bool can_copy_path = true;
    bool can_open = false;
    bool can_open_parent = false;
    bool can_copy_here = false;
    bool can_move_here = false;
    bool can_use_as_destination = false;
};

struct RecentPathsModel {
    std::vector<RecentPathRow> rows;
    std::optional<RecentPathDetail> detail;
};

RecentPathsModel build_recent_paths_model(const std::vector<PathLocation>& paths,
                                          RecentPathsState& state,
                                          const ActionCatalog& catalog);
RecentPathsModel build_recent_paths_model(const std::vector<PathLocation>& paths, bool source_path_selected);

RecentPathCommand view_recent_path(RecentPathsState& state, std::string_view ref);
RecentPathCommand copy_recent_path(const RecentPathsModel& model, std::string_view ref);
RecentPathCommand open_recent_path(const RecentPathsModel& model, std::string_view ref);
RecentPathCommand open_recent_path_parent(const RecentPathsModel& model, std::string_view ref);
RecentPathCommand copy_here_recent_path(const RecentPathsModel& model, std::string_view ref);
RecentPathCommand move_here_recent_path(const RecentPathsModel& model, std::string_view ref);
RecentPathCommand use_recent_path_as_destination(const RecentPathsModel& model, std::string_view ref);
RecentPathCommand close_recent_path_detail(RecentPathsState& state);

}  // namespace pastit
