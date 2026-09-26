#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pasteit {

struct ProcessRun {
    bool started = false;
    bool timed_out = false;
    bool truncated = false;
    int exit_code = -1;
    std::string output;
    std::string error_output;
};

// Runs argv[0] found on PATH with `input` on stdin. No shell is involved, so
// clipboard text can never become part of a command. The child is killed at
// the timeout; output beyond max_output bytes is dropped.
ProcessRun run_process_with_input(const std::vector<std::string>& argv, std::string_view input,
                                  std::chrono::milliseconds timeout, std::size_t max_output);

std::optional<std::string> find_executable(std::string_view name);

}  // namespace pasteit
