#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pastit {

struct ProcessOutput {
    int exit_code = -1;
    std::string stdout_text;
    std::string stderr_text;
};

enum class NetworkProbe { Ping, TraceRoute, ReverseDns, Dig };
enum class HashAlgorithm { Sha256, Sha512 };

class FastActionServices {
public:
    virtual ~FastActionServices() = default;
    virtual bool open_terminal(const std::filesystem::path& directory) = 0;
    virtual ProcessOutput run_network_probe(NetworkProbe probe, std::string_view host) = 0;
    virtual ProcessOutput clone_repository(std::string_view url,
                                           const std::filesystem::path& destination) = 0;
    virtual std::optional<std::int64_t> parse_datetime(std::string_view value,
                                                       std::string_view source_zone) = 0;
    virtual std::string format_datetime(std::int64_t epoch_seconds, std::string_view target_zone) = 0;
    virtual std::string hash_file(const std::filesystem::path& path, HashAlgorithm algorithm) = 0;
    virtual ProcessOutput run_argv(const std::vector<std::string>& argv) = 0;
};

class EmptyFastActionServices final : public FastActionServices {
public:
    bool open_terminal(const std::filesystem::path&) override { return false; }
    ProcessOutput run_network_probe(NetworkProbe, std::string_view) override { return {}; }
    ProcessOutput clone_repository(std::string_view, const std::filesystem::path&) override { return {}; }
    std::optional<std::int64_t> parse_datetime(std::string_view, std::string_view) override {
        return std::nullopt;
    }
    std::string format_datetime(std::int64_t, std::string_view) override { return {}; }
    std::string hash_file(const std::filesystem::path&, HashAlgorithm) override { return {}; }
    ProcessOutput run_argv(const std::vector<std::string>&) override { return {}; }
};

inline FastActionServices& empty_fast_action_services() {
    static EmptyFastActionServices services;
    return services;
}

}  // namespace pastit
