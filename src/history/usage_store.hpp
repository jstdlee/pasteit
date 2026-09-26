#pragma once

#include "decision/usage_model.hpp"

#include <filesystem>
#include <string>

namespace pasteit {

struct UsageLoadResult {
    UsageModel model;
    std::string warning;
};

class UsageStore {
public:
    explicit UsageStore(std::filesystem::path path) : path_(std::move(path)) {}

    UsageLoadResult load() const;
    bool save(const UsageModel& model, std::string& error) const;
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace pasteit
