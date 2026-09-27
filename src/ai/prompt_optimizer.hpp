#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace pasteit {

// Rewrites a rough prompt-template draft into a clear, precise, professional
// one with the general LLM. The optimizer's own instructions are a setting
// (Settings > General LLM, saved in settings.json); empty means the default.
std::string default_prompt_optimizer_system();
std::string prompt_optimizer_system(std::string_view configured);
std::string prompt_optimizer_user_message(std::string_view draft);

struct OptimizedPrompt {
    std::string prompt;
    // Placeholders such as {text} or {target_language} in the draft that the
    // optimized prompt dropped (they were re-attached when possible).
    std::vector<std::string> restored_placeholders;
    std::string error;
};

// Cleans the model reply (code fences, "Optimized prompt:" labels, quotes)
// and keeps every {placeholder} of the draft.
OptimizedPrompt finish_optimized_prompt(std::string_view reply, std::string_view draft);

}  // namespace pasteit
