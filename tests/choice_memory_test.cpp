#include "test_env.hpp"
#include "history/choice_memory.hpp"

#include <cassert>
#include <filesystem>

int main() {
    using namespace pasteit;
    const auto root = std::filesystem::temp_directory_path() / "pasteit-choice-memory-test";
    pasteit_test::remove_tree(root);
    const ChoiceMemoryStore store(root / "choices.json");
    assert(store.load().choices.empty());

    ChoiceMemory memory;
    memory.prompt_parameters["builtin-translate"]["target_language"] = "Japanese";
    memory.prompt_parameters["builtin-translate"]["source_language"] = "auto";
    memory.choices["ask_llm.template"] = "builtin-translate";
    memory.choices["chart.kind"] = "2";
    std::string error;
    assert(store.save(memory, error));

    const auto loaded = store.load();
    assert(loaded.prompt_parameters.at("builtin-translate").at("target_language") == "Japanese");
    assert(loaded.prompt_parameters.at("builtin-translate").at("source_language") == "auto");
    assert(loaded.choice("ask_llm.template") == "builtin-translate");
    assert(loaded.choice("chart.kind") == "2");
    assert(loaded.choice("missing").empty());
    pasteit_test::remove_tree(root);
}
