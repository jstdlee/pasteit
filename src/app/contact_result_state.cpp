#include "app/contact_result_state.hpp"

#include "util/json.hpp"

#include <sstream>

namespace pasteit {
namespace {

std::vector<ContactField> fields_from_json(std::string_view json) {
    std::vector<ContactField> fields;
    const auto parsed = parse_json(json);
    if (!parsed.has_value()) {
        return fields;
    }
    const auto* field_value = parsed->get("fields");
    const auto* array = field_value == nullptr ? nullptr : field_value->array();
    if (array == nullptr) {
        return fields;
    }
    for (const auto& entry : *array) {
        const auto* kind = entry.get("kind");
        const auto* label = entry.get("label");
        const auto* value = entry.get("value");
        if (kind == nullptr || label == nullptr || value == nullptr ||
            kind->string() == nullptr || label->string() == nullptr || value->string() == nullptr) {
            continue;
        }
        fields.push_back(ContactField{*kind->string(), *label->string(), *value->string()});
    }
    return fields;
}

}  // namespace

bool ContactResultState::set_from_json(std::string action_id, std::string json) {
    auto fields = fields_from_json(json);
    if (fields.empty()) {
        fail(std::move(action_id), "contact fields are missing");
        active_.json = std::move(json);
        return false;
    }
    set_fields(std::move(action_id), std::move(fields), std::move(json));
    return true;
}

void ContactResultState::set_fields(std::string action_id, std::vector<ContactField> fields, std::string json) {
    active_ = ContactResultRecord{
        .action_id = std::move(action_id),
        .fields = std::move(fields),
        .json = std::move(json),
        .status = ContactResultStatus::Ready,
        .error = {},
    };
}

void ContactResultState::fail(std::string action_id, std::string error) {
    active_ = ContactResultRecord{
        .action_id = std::move(action_id),
        .fields = {},
        .json = {},
        .status = ContactResultStatus::Failed,
        .error = std::move(error),
    };
}

std::string ContactResultState::copy_text() const {
    if (active_.fields.empty()) {
        return active_.json;
    }
    std::ostringstream out;
    for (const auto& field : active_.fields) {
        out << field.label << '\t' << field.value << '\n';
    }
    return out.str();
}

}  // namespace pasteit
