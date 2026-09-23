#include "ui/file_operation_confirmation_panel.hpp"

#include <cassert>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

pastit::ActionInstance save_action(const std::string& source_ref, const std::string& target_ref) {
    pastit::ActionInstance action;
    action.id = "save-current-text";
    action.kind = pastit::ActionKind::SaveTextFile;
    action.source_ref = source_ref;
    action.target_ref = target_ref;
    action.label = "Save text";
    action.filename = "from-catalog.txt";
    action.enabled = true;
    return action;
}

pastit::PathLocation directory(std::string ref, std::filesystem::path path) {
    return pastit::PathLocation{
        .ref = std::move(ref),
        .path = std::move(path),
        .kind = pastit::PathKind::Directory,
        .last_seen_ms = 100,
        .source = "test",
        .exists = true,
    };
}

pastit::FileOperationConfirmationViewState view_state(
    const std::filesystem::path& primary,
    const std::filesystem::path& alternate) {
    pastit::FileOperationDraft draft;
    draft.frozen_action = save_action("clipboard-1", "primary");
    draft.source_ref = "clipboard-1";
    draft.destination = primary;
    draft.filename = "2026092013-01.txt";
    draft.candidate_destinations = {
        directory("primary", primary),
        directory("alternate", alternate),
    };

    return pastit::FileOperationConfirmationViewState{
        .draft = std::move(draft),
        .source_preview = "hello",
        .can_confirm = true,
    };
}

}  // namespace

int main() {
    using namespace pastit;

    const std::filesystem::path primary{"/tmp/pastit-primary"};
    const std::filesystem::path alternate{"/tmp/pastit-alternate"};
    auto state = view_state(primary, alternate);

    const auto model = build_file_operation_confirmation_panel_model(state);
    assert(model.source_ref == "clipboard-1");
    assert(model.source_preview == "hello");
    assert(model.action_label == "Save text");
    assert(model.directory == primary.string());
    assert(model.filename == "2026092013-01.txt");
    assert(model.output_preview == (primary / "2026092013-01.txt").string());
    assert(model.validation_error.empty());
    assert(model.can_confirm);
    assert(model.shortcuts.size() == 2);
    assert(model.shortcuts[1].ref == "alternate");

    const auto browse = browse_file_operation_destination(state);
    assert(browse.command == FileOperationConfirmationCommand::Browse);
    assert(browse.initial_directory == primary);
    assert(browse.frozen_draft.has_value());
    assert(browse.frozen_draft->destination == primary);

    const auto shortcut = apply_file_operation_shortcut(state, "alternate");
    assert(shortcut.command == FileOperationConfirmationCommand::None);
    assert(state.draft.destination == alternate);
    assert(state.draft.filename == "2026092013-01.txt");
    assert(!state.can_confirm);

    state.draft.filename = "renamed.txt";
    state.can_confirm = true;
    const auto confirm = confirm_file_operation(state);
    assert(confirm.command == FileOperationConfirmationCommand::Confirm);
    assert(confirm.frozen_draft.has_value());
    assert(confirm.frozen_draft->source_ref == "clipboard-1");
    assert(confirm.frozen_draft->destination == alternate);
    assert(confirm.frozen_draft->filename == "renamed.txt");
    assert(confirm.frozen_draft->frozen_action.filename == "from-catalog.txt");

    state.draft.validation_error = "filename cannot contain path separators";
    state.can_confirm = false;
    const auto invalid_model = build_file_operation_confirmation_panel_model(state);
    assert(!invalid_model.can_confirm);
    assert(invalid_model.validation_error == "filename cannot contain path separators");
    const auto invalid_confirm = confirm_file_operation(state);
    assert(invalid_confirm.command == FileOperationConfirmationCommand::None);
    assert(!invalid_confirm.frozen_draft.has_value());

    const auto cancel = cancel_file_operation(state);
    assert(cancel.command == FileOperationConfirmationCommand::Cancel);
    assert(cancel.frozen_draft.has_value());
    assert(cancel.frozen_draft->destination == alternate);
}
