#include "history/usage_store.hpp"

#include "util/json.hpp"
#include "util/replace_file.hpp"

#include <fstream>
#include <sstream>

namespace pastit {

UsageLoadResult UsageStore::load() const {
    UsageLoadResult result;
    std::ifstream input(path_, std::ios::binary);
    if (!input) return result;
    std::ostringstream text;
    text << input.rdbuf();
    const auto root = parse_json(text.str());
    const auto* contexts = root ? root->get("contexts") : nullptr;
    if (!contexts || !contexts->object()) {
        result.warning = "Usage history is malformed; learning restarts from empty.";
        return result;
    }
    for (const auto& [context_key, actions] : *contexts->object()) {
        if (!actions.object()) continue;
        for (const auto& [action_key, cell] : *actions.object()) {
            const auto* weight = cell.get("w");
            const auto* last = cell.get("t");
            const auto* count = cell.get("n");
            if (!weight || !weight->number()) continue;
            result.model.contexts[context_key][action_key] = UsageCell{
                .weight = *weight->number(),
                .last_ms = static_cast<std::int64_t>(last && last->number() ? *last->number() : 0),
                .count = static_cast<std::uint32_t>(count && count->number() ? *count->number() : 1),
            };
        }
    }
    return result;
}

bool UsageStore::save(const UsageModel& model, std::string& error) const {
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
            error = "Could not open usage history.";
            return false;
        }
        out << "{\"version\":1,\"contexts\":{";
        bool first_context = true;
        for (const auto& [context_key, actions] : model.contexts) {
            if (!first_context) out << ',';
            first_context = false;
            out << "\n" << json_quote(context_key) << ":{";
            bool first_action = true;
            for (const auto& [action_key, cell] : actions) {
                if (!first_action) out << ',';
                first_action = false;
                out << json_quote(action_key) << ":{\"w\":" << cell.weight << ",\"t\":" << cell.last_ms
                    << ",\"n\":" << cell.count << '}';
            }
            out << '}';
        }
        out << "}}\n";
        out.flush();
        if (!out) {
            error = "Could not write usage history.";
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

}  // namespace pastit
