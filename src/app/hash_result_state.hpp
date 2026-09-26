#pragma once

#include "platform/fast_action_services.hpp"

#include <filesystem>
#include <string>

namespace pasteit {

enum class HashResultStatus { Idle, Completed, Failed };

struct HashResult {
    std::string action_id;
    HashAlgorithm algorithm = HashAlgorithm::Sha256;
    std::filesystem::path path;
    std::string digest;
    HashResultStatus status = HashResultStatus::Completed;
    std::string error;
};

class HashResultState {
public:
    void complete(HashResult result);
    void fail(std::string action_id, HashAlgorithm algorithm, std::filesystem::path path, std::string error);
    std::string copy_text() const;
    const HashResult& active() const { return active_; }

private:
    HashResult active_;
};

}  // namespace pasteit
