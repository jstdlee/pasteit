#pragma once

#include "core/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pastit {

struct ContactField {
    std::string kind;
    std::string label;
    std::string value;
};

struct DateTimeValue {
    std::string original;
    std::string normalized;
    std::string source_zone;
    std::int64_t epoch_seconds = 0;
    bool has_epoch = false;
};

struct FastContentSignals {
    bool contact = false;
    bool code = false;
    bool diagram = false;
    bool ip = false;
    bool github_url = false;
    bool qr = false;
    bool date_time = false;
    std::vector<ContactField> contact_fields;
    std::optional<DateTimeValue> date_time_value;
};

FastContentSignals detect_fast_content(ContentKind kind, std::string_view text);

// Short names of the detected signals, used as usage-learning context.
std::vector<std::string> fast_signal_tags(const FastContentSignals& signals);

}  // namespace pastit
