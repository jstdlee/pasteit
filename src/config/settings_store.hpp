#pragma once

#include "config/app_settings.hpp"

#include <filesystem>
#include <string>

namespace pastit {
struct SettingsLoadResult {
    AppSettings settings;
    std::string warning;
    bool loaded_from_disk = false;
    bool djev_endpoint_saved = false;
    bool djev_model_saved = false;
    bool djev_api_key_saved = false;
};

class SettingsStore {
public:
    explicit SettingsStore(std::filesystem::path path = default_path()) : path_(std::move(path)) {}
    SettingsLoadResult load() const;
    bool save(const AppSettings& settings, std::string& error) const;
    static std::filesystem::path default_path();
private:
    std::filesystem::path path_;
};
}  // namespace pastit
