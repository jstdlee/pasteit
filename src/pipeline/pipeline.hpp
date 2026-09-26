#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace pasteit {

// One stage of a pipeline: a command name and its arguments, tokenized like a
// POSIX shell (quotes and backslashes) but never executed by one.
struct PipelineStage {
    std::vector<std::string> argv;
};

struct PipelineParse {
    std::vector<PipelineStage> stages;
    std::string error;
};

struct PipelineOptions {
    std::vector<std::string> allowed_tools;  // external programs, e.g. gawk, jq
    bool allow_any_program = false;          // any program on PATH (still no shell)
    // Custom commands: a name usable as a stage that expands to a pipeline,
    // e.g. errors -> grep -i 'error|fail'. Extra arguments go to its last stage.
    std::map<std::string, std::string, std::less<>> custom_commands;
    std::chrono::milliseconds timeout{2000};
    std::size_t max_output = 1024 * 1024;
    // Optional text filter for the "anonymize" stage.
    std::function<std::string(std::string_view)> anonymize;
};

struct PipelineResult {
    bool ok = false;
    std::string output;
    std::string error;
    bool truncated = false;
    std::chrono::milliseconds elapsed{0};
};

PipelineParse parse_pipeline(std::string_view command);
// Raw text of each stage (quotes kept) split at unquoted, unescaped '|', and
// the inverse; used by the stage editor so edits never change quoting.
std::vector<std::string> split_pipeline_text(std::string_view command);
std::string join_pipeline_stages(const std::vector<std::string>& stages);
PipelineResult run_pipeline(std::string_view input, std::string_view command, const PipelineOptions& options);

// Names of the stages implemented in PasteIt itself (portable, no process).
const std::vector<std::string>& builtin_pipeline_commands();
// One-line usage for the builder's help list.
std::string pipeline_command_help(std::string_view name);

}  // namespace pasteit
