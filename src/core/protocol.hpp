#pragma once

#include "core/action.hpp"
#include "core/types.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pasteit {

// A learned habit shared with Djev: how often the user picked this action
// for similar clipboard content. Contains no clipboard text.
struct UsageHint {
    std::string action_id;
    double share = 0.0;
    std::uint32_t count = 0;
};

// Locally judged data shape of the current clipboard item (no content).
struct ClipboardProfileHint {
    std::string shape;
    double confidence = 0.0;
    std::size_t lines = 0;
    std::size_t columns = 0;
    bool header = false;
    std::vector<std::string> tags;
    // Other plausible shapes with a description; when the local guess is
    // uncertain, Djev is asked to pick among them.
    std::vector<std::pair<std::string, std::string>> alternatives;
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
    std::optional<ClipboardProfileHint> profile;
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
    // Answer to the optional content_type question, when it was asked.
    std::string content_type;
    double content_type_confidence = 0.0;
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

}  // namespace pasteit
