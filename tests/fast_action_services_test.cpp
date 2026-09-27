#include "test_env.hpp"
#include "platform/fast_action_services.hpp"
#include "platform/linux/linux_fast_action_services.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <unistd.h>

namespace {

class FakeFastActionServices final : public pasteit::FastActionServices {
public:
    bool open_terminal(const std::filesystem::path& directory) override {
        last_terminal_directory = directory;
        return true;
    }

    pasteit::ProcessOutput run_network_probe(pasteit::NetworkProbe probe, std::string_view host) override {
        last_probe = probe;
        last_probe_host = std::string{host};
        return {.exit_code = 0, .stdout_text = last_probe_host, .stderr_text = {}};
    }

    pasteit::ProcessOutput clone_repository(std::string_view url,
                                           const std::filesystem::path& destination) override {
        last_clone_url = std::string{url};
        last_clone_destination = destination;
        return {.exit_code = 0, .stdout_text = destination.string(), .stderr_text = {}};
    }

    std::optional<std::int64_t> parse_datetime(std::string_view, std::string_view) override { return 42; }
    std::string format_datetime(std::int64_t, std::string_view) override { return "formatted"; }
    std::string hash_file(const std::filesystem::path&, pasteit::HashAlgorithm) override { return "digest"; }

    pasteit::ProcessOutput run_argv(const std::vector<std::string>& argv) override {
        last_argv = argv;
        return {.exit_code = 0, .stdout_text = argv.empty() ? "" : argv.front(), .stderr_text = {}};
    }

    std::filesystem::path last_terminal_directory;
    pasteit::NetworkProbe last_probe = pasteit::NetworkProbe::Ping;
    std::string last_probe_host;
    std::string last_clone_url;
    std::filesystem::path last_clone_destination;
    std::vector<std::string> last_argv;
};

bool starts_with(std::string_view value, std::string_view prefix) {
    return value.substr(0, prefix.size()) == prefix;
}

std::filesystem::path make_unique_temp_dir() {
    const auto base = std::filesystem::temp_directory_path();
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto path = base / ("pasteit-fast-action-services-" + std::to_string(getpid()) + "-" +
                            std::to_string(ticks) + "-" + std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directory(path, error)) {
            return path;
        }
    }
    assert(false && "failed to create unique temporary directory");
    return {};
}

void assert_digest_or_unavailable(std::string_view actual, std::string_view expected) {
    assert(actual.empty() || actual == expected);
}

}  // namespace

int main() {
    static_assert(std::is_abstract_v<pasteit::FastActionServices>);

    FakeFastActionServices fake;
    const std::filesystem::path clone_destination = "/tmp/clone target with spaces";
    fake.run_network_probe(pasteit::NetworkProbe::ReverseDns, "192.0.2.10");
    fake.clone_repository("https://github.com/acme/widget", clone_destination);
    fake.open_terminal("/tmp/project path/source file.cpp");
    fake.run_argv({"git", "clone", "https://github.com/acme/widget", clone_destination.string()});
    assert(fake.last_probe == pasteit::NetworkProbe::ReverseDns);
    assert(fake.last_probe_host == "192.0.2.10");
    assert(fake.last_clone_url == "https://github.com/acme/widget");
    assert(fake.last_clone_destination == clone_destination);
    assert(fake.last_terminal_directory == "/tmp/project path/source file.cpp");
    assert(fake.last_argv.back() == clone_destination.string());

    std::vector<std::vector<std::string>> commands;
    std::vector<std::string> terminal_argv;
    std::filesystem::path terminal_cwd;
    pasteit::LinuxFastActionServices linux_services(
        {.terminal_command = {"test-terminal", "--working-directory"}, .max_output_bytes = 16},
        [&](const std::vector<std::string>& argv) {
            commands.push_back(argv);
            return pasteit::ProcessOutput{.exit_code = 0, .stdout_text = argv.back(), .stderr_text = {}};
        },
        [&](const std::vector<std::string>& argv, const std::filesystem::path& working_directory) {
            terminal_argv = argv;
            terminal_cwd = working_directory;
            return true;
        });

    linux_services.run_network_probe(pasteit::NetworkProbe::Ping, "192.0.2.10");
    assert((commands.back() == std::vector<std::string>{"ping", "-c", "4", "192.0.2.10"}));
    linux_services.run_network_probe(pasteit::NetworkProbe::TraceRoute, "2001:db8::1");
    assert((commands.back() == std::vector<std::string>{"traceroute", "2001:db8::1"}));
    linux_services.run_network_probe(pasteit::NetworkProbe::ReverseDns, "192.0.2.10");
    assert((commands.back() == std::vector<std::string>{"getent", "hosts", "192.0.2.10"}));
    linux_services.run_network_probe(pasteit::NetworkProbe::Dig, "example.com");
    assert((commands.back() == std::vector<std::string>{"dig", "example.com"}));
    linux_services.clone_repository("git@github.com:acme/widget.git", clone_destination);
    assert((commands.back() == std::vector<std::string>{"git", "clone", "git@github.com:acme/widget.git",
                                                        clone_destination.string()}));

    const auto root = make_unique_temp_dir();
    const auto nested = root / "folder with spaces";
    const auto file_path = nested / "source file.txt";
    std::filesystem::create_directories(nested);
    {
        std::ofstream stream(file_path);
        stream << "abc";
    }

    assert(linux_services.open_terminal(file_path));
    assert((terminal_argv == std::vector<std::string>{"test-terminal", "--working-directory"}));
    assert(terminal_cwd == std::filesystem::weakly_canonical(nested).lexically_normal());

    pasteit::LinuxFastActionServices real_services({.max_output_bytes = 16});
    pasteit::LinuxFastActionServices missing_terminal(
        {.terminal_command = {(root / "definitely-missing-terminal").string()}, .max_output_bytes = 16});
    assert(!missing_terminal.open_terminal(nested));
    pasteit::LinuxFastActionServices missing_working_directory(
        {.terminal_command = {"/bin/true"}, .max_output_bytes = 16});
    assert(!missing_working_directory.open_terminal(root / "missing directory" / "source.txt"));

    const auto parsed = real_services.parse_datetime("1970-01-01T00:00:05Z", "UTC");
    assert(parsed.has_value());
    assert(*parsed == 5);
    assert(starts_with(real_services.format_datetime(0, "UTC"), "1970-01-01T00:00:00"));

    assert_digest_or_unavailable(real_services.hash_file(file_path, pasteit::HashAlgorithm::Sha256),
                                 "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert_digest_or_unavailable(real_services.hash_file(file_path, pasteit::HashAlgorithm::Sha512),
                                 "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                                 "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");

    const auto process = real_services.run_argv({"/bin/sh", "-c", "printf stdout; printf stderr >&2; exit 7"});
    assert(process.exit_code == 7);
    assert(process.stdout_text == "stdout");
    assert(process.stderr_text == "stderr");

    const auto bounded = real_services.run_argv({"/bin/sh", "-c", "printf 12345678901234567890"});
    assert(bounded.exit_code == 0);
    assert(bounded.stdout_text == "1234567890123456");
    const auto bounded_error = real_services.run_argv({"/bin/sh", "-c", "printf 12345678901234567890 >&2"});
    assert(bounded_error.exit_code == 0);
    assert(bounded_error.stderr_text == "1234567890123456");

    pasteit_test::remove_tree(root);
}
