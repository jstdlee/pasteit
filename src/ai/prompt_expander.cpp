#include "ai/prompt_expander.hpp"
#include <algorithm>
#include <cctype>
namespace pasteit {
namespace {

bool valid_variable(std::string_view value) {
    if (value.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_')) return false;
    return std::all_of(value.begin() + 1, value.end(), [](char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.';
    });
}

std::string variable_value(std::string_view name, const PromptVariables& variables, bool& known) {
    const auto value = variables.values.find(std::string{name});
    if (value != variables.values.end()) {
        known = true;
        return value->second;
    }
    if (name == "source_language") {
        known = true;
        return variables.source_language;
    }
    if (name == "target_language") {
        known = true;
        return variables.target_language;
    }
    known = false;
    return {};
}

}  // namespace

std::vector<std::string> prompt_variable_names(std::string_view prompt) {
    std::vector<std::string> result;
    for (std::size_t offset = 0; offset < prompt.size();) {
        const auto open = prompt.find('{', offset);
        if (open == std::string_view::npos) break;
        const auto close = prompt.find('}', open + 1);
        if (close == std::string_view::npos) break;
        const auto name = prompt.substr(open + 1, close - open - 1);
        if (valid_variable(name) && name != "text" &&
            std::find(result.begin(), result.end(), name) == result.end()) {
            result.emplace_back(name);
        }
        offset = close + 1;
    }
    return result;
}

ExpandedPrompt expand_prompt(const PromptTemplate& prompt,std::string_view text,const PromptVariables& variables){
    ExpandedPrompt result;result.system_message.reserve(prompt.system_prompt.size()+text.size());
    for(std::size_t i=0;i<prompt.system_prompt.size();){
        if(prompt.system_prompt.compare(i,6,"{text}")==0){result.system_message.append(text);result.text_embedded=true;i+=6;}
        else if(prompt.system_prompt[i]=='{'){
            const auto close=prompt.system_prompt.find('}',i+1);
            if(close!=std::string::npos){
                const auto name=std::string_view{prompt.system_prompt}.substr(i+1,close-i-1);
                bool known=false;const auto value=variable_value(name,variables,known);
                if(known){result.system_message+=value;i=close+1;continue;}
            }
            result.system_message.push_back(prompt.system_prompt[i++]);
        } else result.system_message.push_back(prompt.system_prompt[i++]);
    }
    result.user_message=result.text_embedded?"Apply the requested transformation and return only the result.":std::string{text};
    return result;
}
}  // namespace pasteit
