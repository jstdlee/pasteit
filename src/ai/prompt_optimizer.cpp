#include "ai/prompt_optimizer.hpp"

#include "ai/prompt_expander.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace pasteit {
namespace {

std::string trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string{value.substr(first, last - first + 1)};
}

std::string unescape_newlines(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '\\' && index + 1 < value.size()) {
            const char next = value[index + 1];
            if (next == 'n') { out.push_back('\n'); ++index; continue; }
            if (next == 't') { out.push_back('\t'); ++index; continue; }
            if (next == '\\') { out.push_back('\\'); ++index; continue; }
        }
        out.push_back(value[index]);
    }
    return out;
}

bool starts_with_icase(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) return false;
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(text[index])) != std::tolower(static_cast<unsigned char>(prefix[index]))) return false;
    }
    return true;
}

}  // namespace

std::string default_prompt_optimizer_system() {
    return "You are an expert prompt engineer. Rewrite the user's rough prompt template into a professional prompt "
           "that a language model can follow precisely.\n"
           "Rules:\n"
           "- Keep the user's intent and every essential requirement; do not add new goals.\n"
           "- Be clear and specific: state the task, the input, constraints, tone, and the exact output format "
           "(for example plain text, bullet list, table, JSON) that best serves the intent.\n"
           "- Keep it concise; prefer short imperative sentences or a brief list.\n"
           "- Keep every placeholder in curly braces exactly as written, such as {text} or {target_language}. "
           "{text} is where the clipboard content goes: use it exactly once, preferably at the end after a short label such as \"Input:\"; "
           "if the draft has no {text}, end with that label and {text}.\n"
           "- Tell the model to output only the result, without preambles or explanations, unless the draft asks for them.\n"
           "- Write in the same language as the draft.\n"
           "Return only the improved prompt text, with no title, quotes, code fences or commentary.";
}

std::string prompt_optimizer_system() {
    if (const char* configured = std::getenv("PASTEIT_PROMPT_OPTIMIZER_SYSTEM")) {
        auto value = trim(unescape_newlines(configured));
        if (!value.empty()) return value;
    }
    return default_prompt_optimizer_system();
}

std::string prompt_optimizer_user_message(std::string_view draft) {
    return "Rough prompt template to improve:\n<<<\n" + std::string{draft} + "\n>>>";
}

OptimizedPrompt finish_optimized_prompt(std::string_view reply, std::string_view draft) {
    OptimizedPrompt result;
    std::string text = trim(reply);
    // Drop a surrounding code fence.
    if (text.rfind("```", 0) == 0) {
        const auto first_newline = text.find('\n');
        const auto closing = text.rfind("```");
        if (first_newline != std::string::npos && closing != std::string::npos && closing > first_newline) {
            text = trim(std::string_view{text}.substr(first_newline + 1, closing - first_newline - 1));
        }
    }
    // Drop the <<< >>> markers if the model echoed them.
    if (text.rfind("<<<", 0) == 0) text = trim(std::string_view{text}.substr(3));
    if (text.size() >= 3 && text.compare(text.size() - 3, 3, ">>>") == 0) text = trim(std::string_view{text}.substr(0, text.size() - 3));
    // Drop a leading label line such as "Optimized prompt:".
    for (const std::string_view label : {"optimized prompt:", "improved prompt:", "prompt:", "here is the improved prompt:"}) {
        if (starts_with_icase(text, label)) {
            text = trim(std::string_view{text}.substr(label.size()));
            break;
        }
    }
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') || (text.front() == '\'' && text.back() == '\''))) {
        text = trim(std::string_view{text}.substr(1, text.size() - 2));
    }
    if (text.empty()) {
        result.error = "The LLM returned an empty prompt";
        return result;
    }
    // {text} expands to the whole clipboard: keep only its last use, so the
    // content is not sent twice ("Summarize {text} ... Input: {text}").
    std::size_t uses = 0;
    for (auto at = text.find("{text}"); at != std::string::npos; at = text.find("{text}", at + 6)) ++uses;
    for (; uses > 1; --uses) text.replace(text.find("{text}"), 6, "the input below");
    // Every placeholder of the draft must survive; {text} included.
    auto names = prompt_variable_names(draft);
    if (draft.find("{text}") != std::string_view::npos) names.insert(names.begin(), "text");
    std::vector<std::string> missing;
    for (const auto& name : names) {
        if (text.find("{" + name + "}") == std::string::npos) missing.push_back(name);
    }
    if (!missing.empty()) {
        text += "\n";
        for (const auto& name : missing) {
            text += name == "text" ? "\nInput:\n{text}" : "\n" + name + ": {" + name + "}";
        }
        result.restored_placeholders = missing;
    }
    result.prompt = std::move(text);
    return result;
}

}  // namespace pasteit
