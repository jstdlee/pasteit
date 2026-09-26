#pragma once

#include <filesystem>

namespace pasteit {

class LinuxSingleInstance {
public:
    explicit LinuxSingleInstance(std::filesystem::path runtime_directory = {});
    ~LinuxSingleInstance();

    LinuxSingleInstance(const LinuxSingleInstance&) = delete;
    LinuxSingleInstance& operator=(const LinuxSingleInstance&) = delete;

    bool acquired() const { return acquired_; }
    bool request_show_existing_instance() const;
    bool take_show_request() const;
    const std::filesystem::path& lock_path() const { return path_; }

private:
    int fd_ = -1;
    bool acquired_ = false;
    std::filesystem::path path_;
};

}  // namespace pasteit
