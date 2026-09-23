#pragma once

#include "app/file_operation_confirmation.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pastit {

enum class FileOperationConfirmationCommand {
    None,
    Browse,
    Confirm,
    Cancel,
};

struct FileOperationShortcutRow {
    std::string ref;
    std::string label;
    std::string path;
    std::string source;
    bool selected = false;
};

struct FileOperationConfirmationPanelModel {
    std::string source_ref;
    std::string source_preview;
    std::string action_label;
    std::string directory;
    std::string filename;
    std::string output_preview;
    std::string validation_error;
    bool can_confirm = false;
    std::vector<FileOperationShortcutRow> shortcuts;
};

struct FileOperationConfirmationViewState {
    FileOperationDraft draft;
    std::string source_preview;
    bool can_confirm = false;
    bool focus_pending = true;
};

struct FileOperationConfirmationCommandResult {
    FileOperationConfirmationCommand command = FileOperationConfirmationCommand::None;
    std::optional<FileOperationDraft> frozen_draft;
    std::filesystem::path initial_directory;
};

FileOperationConfirmationPanelModel build_file_operation_confirmation_panel_model(
    const FileOperationConfirmationViewState& state);
FileOperationConfirmationCommandResult browse_file_operation_destination(
    const FileOperationConfirmationViewState& state);
FileOperationConfirmationCommandResult apply_file_operation_shortcut(
    FileOperationConfirmationViewState& state,
    std::string_view destination_ref);
FileOperationConfirmationCommandResult confirm_file_operation(const FileOperationConfirmationViewState& state);
FileOperationConfirmationCommandResult cancel_file_operation(const FileOperationConfirmationViewState& state);

}  // namespace pastit
