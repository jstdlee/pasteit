#pragma once

#include "detect/fast_content_detector.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pasteit {

enum class ContactResultStatus { Idle, Ready, Failed };

struct ContactResultRecord {
    std::string action_id;
    std::vector<ContactField> fields;
    std::string json;
    ContactResultStatus status = ContactResultStatus::Idle;
    std::string error;
};

class ContactResultState {
public:
    bool set_from_json(std::string action_id, std::string json);
    void set_fields(std::string action_id, std::vector<ContactField> fields, std::string json);
    void fail(std::string action_id, std::string error);
    std::string copy_text() const;
    const ContactResultRecord& active() const { return active_; }

private:
    ContactResultRecord active_;
};

}  // namespace pasteit
