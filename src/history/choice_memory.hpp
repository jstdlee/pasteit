#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace pasteit {

// The options last chosen for actions (a template's target language, the
// Ask LLM template, the chart type...), kept across restarts so the next run
// of the same action starts from them.
struct ChoiceMemory {
    // template id -> placeholder name -> value
    std::map<std::string, std::map<std::string, std::string>> prompt_parameters;
    // free-form "<view>.<option>" -> value
    std::map<std::string, std::string> choices;

    std::string choice(std::string_view key) const;
};

class ChoiceMemoryStore {
public:
    explicit ChoiceMemoryStore(std::filesystem::path path) : path_(std::move(path)) {}

    ChoiceMemory load() const;
    bool save(const ChoiceMemory& memory, std::string& error) const;

private:
    std::filesystem::path path_;
};

}  // namespace pasteit
