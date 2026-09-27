#include "ai/prompt_optimizer.hpp"

#include <cassert>
#include <string>

int main() {
    using namespace pasteit;
    const std::string draft = "translate {text} to {target_language} pls";

    // Fences, labels and quotes are removed; placeholders kept.
    auto result = finish_optimized_prompt("```text\nOptimized prompt: Translate the input into {target_language}.\n\nInput:\n{text}\n```", draft);
    assert(result.error.empty());
    assert(result.prompt == "Translate the input into {target_language}.\n\nInput:\n{text}");
    assert(result.restored_placeholders.empty());

    // Dropped placeholders are re-attached and reported.
    result = finish_optimized_prompt("Translate the text accurately.", draft);
    assert(result.prompt.find("{text}") != std::string::npos);
    assert(result.prompt.find("target_language: {target_language}") != std::string::npos);
    assert(result.restored_placeholders.size() == 2);

    assert(!finish_optimized_prompt("   ", draft).error.empty());

    // {text} used twice: only the last stays.
    result = finish_optimized_prompt("Summarize {text} for {target_language} readers.\n\nInput:\n{text}", draft);
    assert(result.prompt == "Summarize the input below for {target_language} readers.\n\nInput:\n{text}");

    // The setting wins; blank falls back to the default.
    assert(prompt_optimizer_system("  \n ") == default_prompt_optimizer_system());
    assert(default_prompt_optimizer_system().find("{text}") != std::string::npos);
    assert(prompt_optimizer_system(" Be brief. ") == "Be brief.");
    assert(prompt_optimizer_user_message("x {text}").find("x {text}") != std::string::npos);
}
