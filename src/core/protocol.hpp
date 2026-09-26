#pragma once

#include "core/action.hpp"
#include "core/types.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pastit {

// A learned habit shared with Djev: how often the user picked this action
// for similar clipboard content. Contains no clipboard text.
struct UsageHint {
    std::string action_id;
    double share = 0.0;
    std::uint32_t count = 0;
};

struct DecisionSnapshot {
    std::string clipboard_hash;
    std::string focused_target_hash;
    std::string focused_app;
    std::string focused_window_title;
    std::string focused_current_directory;
    std::int64_t captured_at_ms = 0;
    std::vector<ClipboardItem> clipboard_items;
    std::vector<PathLocation> recent_paths;
    std::vector<ActionInstance> available_actions;
    std::vector<UsageHint> usage_hints;
};

struct DecisionRequest {
    int protocol_version = 1;
    std::string request_id;
    DecisionSnapshot snapshot;
};

struct DecisionResponse {
    std::string request_id;
    std::string choice;
    double confidence = 0.0;
    std::map<std::string, double> probabilities;
    bool valid = true;
    int http_status = 0;
    std::string error;
};

enum class ExecutionStatus {
    Prepared,
    Sent,
    Ranked,
    Selected,
    Executing,
    Completed,
    Failed,
    Unsupported,
    Stale,
};

struct ExecutionResult {
    std::string request_id;
    std::string action_id;
    ExecutionStatus status = ExecutionStatus::Prepared;
    std::string message;
    std::optional<std::filesystem::path> output_path;
    std::vector<std::filesystem::path> output_paths;
    std::optional<std::string> output_clipboard_ref;
    std::optional<std::string> job_id;
    std::optional<std::string> download_job_id;
};

}  // namespace pastit
