#include "history/choice_memory.hpp"

#include "util/json.hpp"
#include "util/replace_file.hpp"

#include <fstream>
#include <sstream>

namespace pasteit {

namespace {
void write_string_map(std::ostream& out, const std::map<std::string, std::string>& values) {
    out << '{';
    bool first = true;
    for (const auto& [key, value] : values) {
        if (!first) out << ',';
        first = false;
        out << json_quote(key) << ':' << json_quote(value);
    }
    out << '}';
}

std::map<std::string, std::string> read_string_map(const JsonValue* value) {
    std::map<std::string, std::string> out;
    if (value == nullptr || value->object() == nullptr) return out;
    for (const auto& [key, item] : *value->object()) {
        if (const auto* text = item.string()) out[key] = *text;
    }
    return out;
}
}  // namespace

std::string ChoiceMemory::choice(std::string_view key) const {
    const auto found = choices.find(std::string{key});
    return found == choices.end() ? std::string{} : found->second;
}

ChoiceMemory ChoiceMemoryStore::load() const {
    ChoiceMemory memory;
    std::ifstream input(path_, std::ios::binary);
    if (!input) return memory;
    std::ostringstream text;
    text << input.rdbuf();
    const auto root = parse_json(text.str());
    if (!root) return memory;
    if (const auto* parameters = root->get("prompt_parameters"); parameters && parameters->object()) {
        for (const auto& [template_id, values] : *parameters->object()) {
            auto read = read_string_map(&values);
            if (!read.empty()) memory.prompt_parameters[template_id] = std::move(read);
        }
    }
    memory.choices = read_string_map(root->get("choices"));
    return memory;
}

bool ChoiceMemoryStore::save(const ChoiceMemory& memory, std::string& error) const {
    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(), ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    auto temporary = path_;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "Could not open remembered choices.";
            return false;
        }
        out << "{\"version\":1,\"prompt_parameters\":{";
        bool first = true;
        for (const auto& [template_id, values] : memory.prompt_parameters) {
            if (!first) out << ',';
            first = false;
            out << "\n" << json_quote(template_id) << ':';
            write_string_map(out, values);
        }
        out << "},\n\"choices\":";
        write_string_map(out, memory.choices);
        out << "}\n";
        out.flush();
        if (!out) {
            error = "Could not write remembered choices.";
            return false;
        }
    }
    replace_file(temporary, path_, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        error = ec.message();
        return false;
    }
    error.clear();
    return true;
}

}  // namespace pasteit
