#include "app/date_time_result_state.hpp"

#include <utility>

namespace pastit {

void DateTimeResultState::complete(DateTimeResult result) {
    result.status = DateTimeResultStatus::Completed;
    result.error.clear();
    active_ = std::move(result);
}

void DateTimeResultState::fail(std::string action_id, std::string error) {
    active_ = DateTimeResult{
        .action_id = std::move(action_id),
        .original = {},
        .source_zone = {},
        .target_zone = {},
        .epoch_seconds = 0,
        .formatted = {},
        .status = DateTimeResultStatus::Failed,
        .error = std::move(error),
    };
}

std::string DateTimeResultState::copy_text() const {
    return active_.formatted;
}

}  // namespace pastit
