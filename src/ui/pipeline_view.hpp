#pragma once

#include "config/app_settings.hpp"
#include "pipeline/pipeline.hpp"
#include "ui/data_views.hpp"
#include "ui/localization.hpp"

#include <chrono>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace pasteit {

struct PipelineViewState {
    bool open = false;
    bool focus_pending = false;
    std::string input;
    std::string command;
    std::string ran_command;
    std::chrono::steady_clock::time_point edited{};
    std::optional<std::future<PipelineResult>> pending;
    // Runs replaced while still in flight; collected without blocking, since
    // destroying a std::async future waits for it.
    std::vector<std::future<PipelineResult>> abandoned;
    int active_stage = -1;  // chips insert after this stage
    PipelineResult result;
    bool has_result = false;
    std::string recipe_name;
    std::string status;
};

void open_pipeline_view(PipelineViewState& state, std::string input, std::string command);

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
struct PipelineViewHost {
    DataViewHost data;
    std::function<void(std::string_view)> replace_clipboard;
    std::function<void(PipelineRecipe)> save_recipe;
};

// Live preview: the pipeline re-runs on a worker thread 300 ms after edits.
void draw_pipeline_view(PipelineViewState& state, const PipelineOptions& options, const PipelineViewHost& host,
                        UiLanguage language);
#endif

}  // namespace pasteit
