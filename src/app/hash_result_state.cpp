#include "app/hash_result_state.hpp"

#include <utility>

namespace pasteit {

void HashResultState::complete(HashResult result) {
    result.status = HashResultStatus::Completed;
    result.error.clear();
    active_ = std::move(result);
}

void HashResultState::fail(std::string action_id, HashAlgorithm algorithm, std::filesystem::path path, std::string error) {
    active_ = HashResult{
        .action_id = std::move(action_id),
        .algorithm = algorithm,
        .path = std::move(path),
        .digest = {},
        .status = HashResultStatus::Failed,
        .error = std::move(error),
    };
}

std::string HashResultState::copy_text() const {
    return active_.digest;
}

}  // namespace pasteit
