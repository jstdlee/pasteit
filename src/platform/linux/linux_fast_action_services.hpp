#pragma once

#include "platform/fast_action_services.hpp"

#include <cstddef>
#include <functional>

namespace pastit {

class LinuxFastActionServices final : public FastActionServices {
public:
    static constexpr std::size_t kDefaultMaxOutputBytes = 64U * 1024U;

    struct Options {
        std::vector<std::string> terminal_command = {"x-terminal-emulator"};
        std::size_t max_output_bytes = kDefaultMaxOutputBytes;
    };

    using ProcessRunner = std::function<ProcessOutput(const std::vector<std::string>& argv)>;
    using TerminalLauncher = std::function<bool(const std::vector<std::string>& argv,
                                                const std::filesystem::path& working_directory)>;

    LinuxFastActionServices();
    explicit LinuxFastActionServices(Options options);
    LinuxFastActionServices(Options options, ProcessRunner process_runner, TerminalLauncher terminal_launcher);
    void configure(Options options);

    bool open_terminal(const std::filesystem::path& directory) override;
    ProcessOutput run_network_probe(NetworkProbe probe, std::string_view host) override;
    ProcessOutput clone_repository(std::string_view url, const std::filesystem::path& destination) override;
    std::optional<std::int64_t> parse_datetime(std::string_view value, std::string_view source_zone) override;
    std::string format_datetime(std::int64_t epoch_seconds, std::string_view target_zone) override;
    std::string hash_file(const std::filesystem::path& path, HashAlgorithm algorithm) override;
    ProcessOutput run_argv(const std::vector<std::string>& argv) override;

private:
    std::filesystem::path terminal_working_directory(const std::filesystem::path& path) const;

    Options options_;
    ProcessRunner process_runner_;
    TerminalLauncher terminal_launcher_;
};

}  // namespace pastit
