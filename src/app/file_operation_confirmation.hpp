#pragma once

#include "core/action.hpp"
#include "core/types.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pasteit {

class ClipboardStore;

struct FileOperationDraft {
    ActionInstance frozen_action;
    std::string source_ref;
    std::filesystem::path destination;
    std::string filename;
    std::vector<PathLocation> candidate_destinations;
    std::string validation_error;
};

bool needs_file_confirmation(ActionKind action_kind);

FileOperationDraft make_file_operation_draft(
    const ActionInstance& action,
    const ClipboardItem& item,
    std::vector<PathLocation> candidate_destinations,
    std::chrono::system_clock::time_point now);

bool validate_file_operation_draft(FileOperationDraft& draft, const ClipboardStore& store);

std::optional<ActionInstance> confirmed_action(const FileOperationDraft& draft, const ClipboardStore& store);

}  // namespace pasteit
