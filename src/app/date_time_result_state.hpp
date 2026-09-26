#pragma once

#include <cstdint>
#include <string>

namespace pasteit {

enum class DateTimeResultStatus { Idle, Completed, Failed };

struct DateTimeResult {
    std::string action_id;
    std::string original;
    std::string source_zone;
    std::string target_zone;
    std::int64_t epoch_seconds = 0;
    std::string formatted;
    DateTimeResultStatus status = DateTimeResultStatus::Completed;
    std::string error;
};

class DateTimeResultState {
public:
    void complete(DateTimeResult result);
    void fail(std::string action_id, std::string error);
    std::string copy_text() const;
    const DateTimeResult& active() const { return active_; }

private:
    DateTimeResult active_;
};

}  // namespace pasteit
