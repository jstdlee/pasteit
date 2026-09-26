#include "ui/recent_paths_model.hpp"

#include <cassert>
#include <filesystem>
#include <string>
#include <vector>

namespace {

pastit::PathLocation path(std::string ref, std::filesystem::path value, pastit::PathKind kind,
                          std::int64_t last_seen_ms, bool exists) {
    pastit::PathLocation out;
    out.ref = std::move(ref);
    out.path = std::move(value);
    out.kind = kind;
    out.last_seen_ms = last_seen_ms;
    out.source = "test";
    out.exists = exists;
    return out;
}

pastit::ActionInstance action(std::string id, pastit::ActionKind kind, std::string target_ref, bool enabled = true) {
    pastit::ActionInstance out;
    out.id = std::move(id);
    out.kind = kind;
    out.source_ref = "clip_source";
    out.target_ref = std::move(target_ref);
    out.enabled = enabled;
    return out;
}

}  // namespace

int main() {
    using namespace pastit;

    const std::vector<PathLocation> paths = {
        path("dest", "/tmp/dest", PathKind::Directory, 10, true),
        path("file", "/tmp/source/file.txt", PathKind::File, 30, true),
        path("missing", "/tmp/missing", PathKind::Directory, 20, false),
    };

    ActionCatalog catalog;
    catalog.actions = {
        action("copy_dest_exact", ActionKind::CopyPathToDirectory, "dest"),
        action("move_dest_exact", ActionKind::MovePath, "dest"),
        action("copy_missing_disabled", ActionKind::CopyPathToDirectory, "missing", false),
    };

    RecentPathsState state;
    auto model = build_recent_paths_model(paths, state, catalog);
    assert(model.rows.size() == 3);
    // Existing paths first, then by rank (recency here, no uses yet).
    assert(model.rows[0].ref == "file");
    assert(model.rows[1].ref == "dest");
    assert(model.rows[2].ref == "missing");

    // A frequently used folder outranks a path that was merely seen later.
    auto used = paths;
    used[0].use_weight = 4.0;
    used[0].use_count = 12;
    used[0].last_used_ms = 5;
    RecentPathsState used_state;
    const auto used_model = build_recent_paths_model(used, used_state, catalog);
    assert(used_model.rows[0].ref == "dest" && used_model.rows[0].use_count == 12);

    const auto& file_row = model.rows[0];
    assert(file_row.type_label == "file");
    assert(file_row.name == "file.txt");
    assert(file_row.path == "/tmp/source/file.txt");
    assert(file_row.display_path == "/tmp/source/file.txt");
    assert(file_row.parent_path == "/tmp/source");
    assert(file_row.can_copy_path);
    assert(file_row.can_open);
    assert(file_row.can_open_parent);
    assert(!file_row.can_copy_here);
    assert(!file_row.can_move_here);
    assert(!file_row.can_use_as_destination);

    const auto& missing_row = model.rows[2];
    assert(!missing_row.can_open);
    assert(!missing_row.can_use_as_destination);
    assert(!missing_row.can_copy_here);
    assert(missing_row.copy_here_action_id.empty());

    const auto& dest_row = model.rows[1];
    assert(dest_row.type_label == "folder");
    assert(dest_row.can_copy_here);
    assert(dest_row.can_move_here);
    assert(dest_row.can_use_as_destination);
    assert(dest_row.copy_here_action_id == "copy_dest_exact");
    assert(dest_row.move_here_action_id == "move_dest_exact");

    state.detail_ref = "dest";
    model = build_recent_paths_model(paths, state, catalog);
    assert(model.detail.has_value());
    assert(model.detail->ref == "dest");
    assert(model.detail->full_path == "/tmp/dest");
    assert(model.detail->parent_path == "/tmp");
    assert(model.detail->copy_here_action_id == "copy_dest_exact");
    assert(model.detail->move_here_action_id == "move_dest_exact");
    assert(model.rows[1].selected);

    model = build_recent_paths_model({paths[1], paths[0]}, state, catalog);
    assert(model.detail.has_value());
    assert(model.detail->ref == "dest");
    assert(state.detail_ref == "dest");

    model = build_recent_paths_model({paths[1]}, state, catalog);
    assert(!model.detail.has_value());
    assert(state.detail_ref.empty());

    const auto view = view_recent_path(state, "dest");
    assert(view.kind == RecentPathCommandKind::View);
    assert(view.path_ref == "dest");
    assert(state.detail_ref == "dest");

    model = build_recent_paths_model(paths, state, catalog);
    const auto copy = copy_recent_path(model, "dest");
    assert(copy.kind == RecentPathCommandKind::CopyPath);
    assert(copy.path_ref == "dest");
    assert(copy.action_id.empty());

    const auto open = open_recent_path(model, "dest");
    assert(open.kind == RecentPathCommandKind::Open);
    assert(open.path_ref == "dest");

    const auto parent = open_recent_path_parent(model, "file");
    assert(parent.kind == RecentPathCommandKind::OpenParent);
    assert(parent.path_ref == "file");

    const auto copy_here = copy_here_recent_path(model, "dest");
    assert(copy_here.kind == RecentPathCommandKind::CopyHere);
    assert(copy_here.path_ref == "dest");
    assert(copy_here.action_id == "copy_dest_exact");

    const auto move_here = move_here_recent_path(model, "dest");
    assert(move_here.kind == RecentPathCommandKind::MoveHere);
    assert(move_here.path_ref == "dest");
    assert(move_here.action_id == "move_dest_exact");

    const auto use_destination = use_recent_path_as_destination(model, "dest");
    assert(use_destination.kind == RecentPathCommandKind::UseAsDestination);
    assert(use_destination.path_ref == "dest");

    assert(copy_here_recent_path(model, "missing").kind == RecentPathCommandKind::None);
    assert(open_recent_path_parent(model, "dest").kind == RecentPathCommandKind::None);

    const auto close = close_recent_path_detail(state);
    assert(close.kind == RecentPathCommandKind::CloseDetail);
    assert(close.path_ref == "dest");
    assert(state.detail_ref.empty());

    const auto long_path = abbreviate_middle("/home/user/projects/a-very-long-project-name/build/output/release/artifact.tar.gz");
    assert(long_path.find("...") != std::string::npos);
    assert(long_path.starts_with("/home/user/projects/"));
    assert(long_path.ends_with("artifact.tar.gz"));
    assert(abbreviate_middle("/tmp/short", 72) == "/tmp/short");
}
