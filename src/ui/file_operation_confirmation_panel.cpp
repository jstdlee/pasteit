#include "ui/file_operation_confirmation_panel.hpp"
#include "util/path_utf8.hpp"

#include <algorithm>

namespace pastit {
namespace {

std::string shortcut_label(const PathLocation& destination) {
    const auto filename = path_to_utf8_string(destination.path.filename());
    if (!filename.empty()) {
        return filename;
    }
    return path_to_utf8_string(destination.path);
}

}  // namespace

FileOperationConfirmationPanelModel build_file_operation_confirmation_panel_model(
    const FileOperationConfirmationViewState& state) {
    const auto& draft = state.draft;
    FileOperationConfirmationPanelModel model;
    model.source_ref = draft.source_ref;
    model.source_preview = state.source_preview;
    model.action_label = draft.frozen_action.label;
    model.directory = path_to_utf8_string(draft.destination);
    model.filename = draft.filename;
    if (!draft.destination.empty() && !draft.filename.empty()) {
        model.output_preview = path_to_utf8_string(draft.destination / path_from_utf8_string(draft.filename));
    }
    model.validation_error = draft.validation_error;
    model.can_confirm = state.can_confirm;
    model.shortcuts.reserve(draft.candidate_destinations.size());
    for (const auto& destination : draft.candidate_destinations) {
        model.shortcuts.push_back({
            .ref = destination.ref,
            .label = shortcut_label(destination),
            .path = path_to_utf8_string(destination.path),
            .source = destination.source,
            .selected = destination.path == draft.destination,
        });
    }
    return model;
}

FileOperationConfirmationCommandResult browse_file_operation_destination(
    const FileOperationConfirmationViewState& state) {
    FileOperationConfirmationCommandResult result;
    result.command = FileOperationConfirmationCommand::Browse;
    result.frozen_draft = state.draft;
    result.initial_directory = state.draft.destination;
    return result;
}

FileOperationConfirmationCommandResult apply_file_operation_shortcut(
    FileOperationConfirmationViewState& state,
    std::string_view destination_ref) {
    auto& draft = state.draft;
    const auto found = std::find_if(draft.candidate_destinations.begin(), draft.candidate_destinations.end(),
                                    [&](const PathLocation& destination) {
                                        return destination.ref == destination_ref && destination.kind == PathKind::Directory;
                                    });
    if (found != draft.candidate_destinations.end()) {
        draft.destination = found->path;
        draft.validation_error.clear();
        state.can_confirm = false;
    }
    FileOperationConfirmationCommandResult result;
    result.command = FileOperationConfirmationCommand::None;
    result.frozen_draft = draft;
    return result;
}

FileOperationConfirmationCommandResult confirm_file_operation(const FileOperationConfirmationViewState& state) {
    if (!state.can_confirm) {
        return {};
    }
    FileOperationConfirmationCommandResult result;
    result.command = FileOperationConfirmationCommand::Confirm;
    result.frozen_draft = state.draft;
    return result;
}

FileOperationConfirmationCommandResult cancel_file_operation(const FileOperationConfirmationViewState& state) {
    FileOperationConfirmationCommandResult result;
    result.command = FileOperationConfirmationCommand::Cancel;
    result.frozen_draft = state.draft;
    return result;
}

}  // namespace pastit
