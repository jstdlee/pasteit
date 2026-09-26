#pragma once
#include "config/app_settings.hpp"
#include <map>
#include <string>
#include <string_view>
#include <vector>
namespace pasteit {
struct PromptVariables {
    std::string source_language="auto";
    std::string target_language="Simplified Chinese";
    std::map<std::string, std::string> values;
};
struct ExpandedPrompt { std::string system_message; std::string user_message; bool text_embedded=false; };
// Returns unique named placeholders other than {text}, which is filled from
// the current clipboard item automatically.
std::vector<std::string> prompt_variable_names(std::string_view prompt);
ExpandedPrompt expand_prompt(const PromptTemplate& prompt,std::string_view text,const PromptVariables& variables);
}
