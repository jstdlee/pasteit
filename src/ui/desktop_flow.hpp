#pragma once

#include "actions/action_catalog.hpp"
#include "core/protocol.hpp"
#include "config/app_settings.hpp"
#include "decision/action_preference.hpp"
#include "decision/usage_model.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace pastit {

struct DesktopDecisionInput {
    std::string request_id;
    std::int64_t captured_at_ms = 0;
    std::vector<ClipboardItem> clipboard_items;
    std::vector<PathLocation> recent_paths;
    std::string focused_target_hash;
    std::string focused_app;
    std::string focused_window_title;
    std::filesystem::path focused_current_directory;
    bool direct_send_available = false;
    std::vector<PromptTemplate> prompt_templates;
    ProviderSettings general_llm;
    std::filesystem::path default_image_directory;
    std::filesystem::path default_text_directory;
    DownloadSettings downloads;
    HashSettings hash;
    DateTimeSettings date_time;
    const UsageModel* usage = nullptr;
    std::vector<PipelineRecipe> pipeline_recipes;
};

struct DesktopDecisionBatch {
    DecisionRequest request;
    ActionCatalog catalog;
    ProviderSettings general_llm;
    ActionRankingContext ranking_context;
    UsageContext usage_context;
};

bool mermaid_action_requires_generation(const ActionInstance& action);
DesktopDecisionBatch build_desktop_decision(const DesktopDecisionInput& input);

// Records a root-list choice in the usage model for the batch context.
void record_batch_usage(UsageModel& usage, const DesktopDecisionBatch& batch, const ActionInstance& action);

}  // namespace pastit
