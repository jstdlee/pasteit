#include "platform/linux/linux_fast_action_services.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace pastit {
namespace {

void append_bounded(std::string& output, const char* data, std::size_t size, std::size_t limit) {
    if (output.size() >= limit) {
        return;
    }
    const auto available = limit - output.size();
    output.append(data, std::min(size, available));
}

bool set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool set_close_on_exec(int fd) {
    const int flags = fcntl(fd, F_GETFD, 0);
    return flags >= 0 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

void close_if_open(int& fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

bool read_available(int& fd, std::string& output, std::size_t limit) {
    std::array<char, 4096> buffer{};
    while (fd >= 0) {
        const auto count = read(fd, buffer.data(), buffer.size());
        if (count > 0) {
            append_bounded(output, buffer.data(), static_cast<std::size_t>(count), limit);
            continue;
        }
        if (count == 0) {
            close_if_open(fd);
            return false;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }
        close_if_open(fd);
        return false;
    }
    return false;
}

std::vector<char*> argv_pointers(const std::vector<std::string>& argv) {
    std::vector<char*> pointers;
    pointers.reserve(argv.size() + 1);
    for (const auto& arg : argv) {
        pointers.push_back(const_cast<char*>(arg.c_str()));
    }
    pointers.push_back(nullptr);
    return pointers;
}

void write_setup_error(int fd, int error_code) {
    const char* bytes = reinterpret_cast<const char*>(&error_code);
    std::size_t written = 0;
    while (written < sizeof(error_code)) {
        const auto count = write(fd, bytes + written, sizeof(error_code) - written);
        if (count > 0) {
            written += static_cast<std::size_t>(count);
            continue;
        }
        if (count == -1 && errno == EINTR) {
            continue;
        }
        break;
    }
}

std::optional<int> read_setup_error(int fd) {
    int error_code = 0;
    char* bytes = reinterpret_cast<char*>(&error_code);
    std::size_t read_bytes = 0;
    while (read_bytes < sizeof(error_code)) {
        const auto count = read(fd, bytes + read_bytes, sizeof(error_code) - read_bytes);
        if (count > 0) {
            read_bytes += static_cast<std::size_t>(count);
            continue;
        }
        if (count == 0) {
            return read_bytes == 0 ? std::nullopt : std::optional<int>{EIO};
        }
        if (errno == EINTR) {
            continue;
        }
        return errno;
    }
    return error_code == 0 ? std::optional<int>{EIO} : std::optional<int>{error_code};
}

ProcessOutput run_process_argv(const std::vector<std::string>& argv, std::size_t output_limit) {
    if (argv.empty() || argv.front().empty()) {
        return {.exit_code = -1, .stdout_text = {}, .stderr_text = "empty argv"};
    }

    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        close_if_open(stdout_pipe[0]);
        close_if_open(stdout_pipe[1]);
        close_if_open(stderr_pipe[0]);
        close_if_open(stderr_pipe[1]);
        return {.exit_code = -1, .stdout_text = {}, .stderr_text = std::strerror(errno)};
    }

    auto child_argv = argv_pointers(argv);
    const pid_t pid = fork();
    if (pid == -1) {
        const std::string error = std::strerror(errno);
        close_if_open(stdout_pipe[0]);
        close_if_open(stdout_pipe[1]);
        close_if_open(stderr_pipe[0]);
        close_if_open(stderr_pipe[1]);
        return {.exit_code = -1, .stdout_text = {}, .stderr_text = error};
    }

    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        execvp(child_argv.front(), child_argv.data());
        constexpr char message[] = "execvp failed\n";
        write(STDERR_FILENO, message, sizeof(message) - 1);
        _exit(127);
    }

    close_if_open(stdout_pipe[1]);
    close_if_open(stderr_pipe[1]);
    (void)set_nonblocking(stdout_pipe[0]);
    (void)set_nonblocking(stderr_pipe[0]);

    ProcessOutput output;
    while (stdout_pipe[0] >= 0 || stderr_pipe[0] >= 0) {
        std::array<pollfd, 2> fds{pollfd{.fd = stdout_pipe[0], .events = POLLIN | POLLHUP, .revents = 0},
                                  pollfd{.fd = stderr_pipe[0], .events = POLLIN | POLLHUP, .revents = 0}};
        const int ready = poll(fds.data(), fds.size(), -1);
        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (stdout_pipe[0] >= 0 && (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            (void)read_available(stdout_pipe[0], output.stdout_text, output_limit);
        }
        if (stderr_pipe[0] >= 0 && (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            (void)read_available(stderr_pipe[0], output.stderr_text, output_limit);
        }
    }

    close_if_open(stdout_pipe[0]);
    close_if_open(stderr_pipe[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
    }
    if (WIFEXITED(status)) {
        output.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        output.exit_code = 128 + WTERMSIG(status);
    }
    return output;
}

bool launch_terminal_argv(const std::vector<std::string>& argv, const std::filesystem::path& working_directory) {
    if (argv.empty() || argv.front().empty()) {
        return false;
    }
    int setup_pipe[2] = {-1, -1};
    if (pipe(setup_pipe) != 0) {
        return false;
    }
    if (!set_close_on_exec(setup_pipe[1])) {
        close_if_open(setup_pipe[0]);
        close_if_open(setup_pipe[1]);
        return false;
    }

    auto child_argv = argv_pointers(argv);
    const auto directory = working_directory.string();
    const pid_t pid = fork();
    if (pid == -1) {
        close_if_open(setup_pipe[0]);
        close_if_open(setup_pipe[1]);
        return false;
    }
    if (pid == 0) {
        close(setup_pipe[0]);
        if (!directory.empty() && chdir(directory.c_str()) != 0) {
            write_setup_error(setup_pipe[1], errno);
            _exit(126);
        }
        execvp(child_argv.front(), child_argv.data());
        write_setup_error(setup_pipe[1], errno);
        _exit(127);
    }

    close_if_open(setup_pipe[1]);
    const auto setup_error = read_setup_error(setup_pipe[0]);
    close_if_open(setup_pipe[0]);
    if (setup_error.has_value()) {
        int status = 0;
        while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
        }
        return false;
    }

    std::thread([pid] {
        int status = 0;
        while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
        }
    }).detach();
    return true;
}

std::vector<std::string> network_probe_argv(NetworkProbe probe, std::string_view host) {
    const std::string target{host};
    switch (probe) {
        case NetworkProbe::Ping:
            return {"ping", "-c", "4", target};
        case NetworkProbe::TraceRoute:
            return {"traceroute", target};
        case NetworkProbe::ReverseDns:
            return {"getent", "hosts", target};
        case NetworkProbe::Dig:
            return {"dig", target};
    }
    return {};
}

bool only_digits(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

std::optional<std::int64_t> parse_integer(std::string_view value) {
    std::int64_t result = 0;
    for (const char ch : value) {
        const int digit = ch - '0';
        if (result > (INT64_MAX - digit) / 10) {
            return std::nullopt;
        }
        result = result * 10 + digit;
    }
    return result;
}

std::optional<std::tm> parse_tm(std::string_view value, const char* format) {
    std::tm parsed{};
    parsed.tm_isdst = -1;
    const std::string text{value};
    char* end = strptime(text.c_str(), format, &parsed);
    if (end == nullptr) {
        return std::nullopt;
    }
    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (*end != '\0') {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::int64_t> to_epoch_utc(std::tm parsed) {
    const time_t value = timegm(&parsed);
    if (value == static_cast<time_t>(-1)) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(value);
}

std::mutex& timezone_mutex() {
    static std::mutex mutex;
    return mutex;
}

template <typename Callback>
auto with_timezone(std::string_view zone, Callback callback) {
    std::lock_guard lock(timezone_mutex());
    const char* current = std::getenv("TZ");
    const std::optional<std::string> previous = current == nullptr ? std::nullopt : std::optional<std::string>{current};
    if (!zone.empty()) {
        setenv("TZ", std::string{zone}.c_str(), 1);
        tzset();
    }
    auto result = callback();
    if (previous.has_value()) {
        setenv("TZ", previous->c_str(), 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
    return result;
}

std::optional<std::int64_t> to_epoch_in_zone(std::tm parsed, std::string_view zone) {
    return with_timezone(zone, [&]() -> std::optional<std::int64_t> {
        const time_t value = mktime(&parsed);
        if (value == static_cast<time_t>(-1)) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(value);
    });
}

bool is_explicit_utc_zone(std::string_view zone) {
    return zone == "UTC" || zone == "Etc/UTC" || zone == "Z";
}

struct EvpMd;
struct EvpMdCtx;

struct CryptoApi {
    using CtxNew = EvpMdCtx* (*)();
    using CtxFree = void (*)(EvpMdCtx*);
    using GetDigestByName = const EvpMd* (*)(const char*);
    using DigestInit = int (*)(EvpMdCtx*, const EvpMd*, void*);
    using DigestUpdate = int (*)(EvpMdCtx*, const void*, std::size_t);
    using DigestFinal = int (*)(EvpMdCtx*, unsigned char*, unsigned int*);

    void* handle = nullptr;
    CtxNew ctx_new = nullptr;
    CtxFree ctx_free = nullptr;
    GetDigestByName get_digest_by_name = nullptr;
    DigestInit digest_init = nullptr;
    DigestUpdate digest_update = nullptr;
    DigestFinal digest_final = nullptr;

    CryptoApi() {
        handle = dlopen("libcrypto.so.3", RTLD_LAZY);
        if (handle == nullptr) {
            handle = dlopen("libcrypto.so", RTLD_LAZY);
        }
        if (handle == nullptr) {
            return;
        }
        ctx_new = reinterpret_cast<CtxNew>(dlsym(handle, "EVP_MD_CTX_new"));
        ctx_free = reinterpret_cast<CtxFree>(dlsym(handle, "EVP_MD_CTX_free"));
        get_digest_by_name = reinterpret_cast<GetDigestByName>(dlsym(handle, "EVP_get_digestbyname"));
        digest_init = reinterpret_cast<DigestInit>(dlsym(handle, "EVP_DigestInit_ex"));
        digest_update = reinterpret_cast<DigestUpdate>(dlsym(handle, "EVP_DigestUpdate"));
        digest_final = reinterpret_cast<DigestFinal>(dlsym(handle, "EVP_DigestFinal_ex"));
    }

    bool valid() const {
        return handle != nullptr && ctx_new != nullptr && ctx_free != nullptr && get_digest_by_name != nullptr &&
               digest_init != nullptr && digest_update != nullptr && digest_final != nullptr;
    }
};

CryptoApi& crypto_api() {
    static CryptoApi api;
    return api;
}

std::string hex_digest(const unsigned char* bytes, unsigned int size) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int index = 0; index < size; ++index) {
        output << std::setw(2) << static_cast<int>(bytes[index]);
    }
    return output.str();
}

}  // namespace

LinuxFastActionServices::LinuxFastActionServices() : LinuxFastActionServices(Options{}) {}

LinuxFastActionServices::LinuxFastActionServices(Options options)
    : LinuxFastActionServices(std::move(options), {}, {}) {}

LinuxFastActionServices::LinuxFastActionServices(Options options, ProcessRunner process_runner,
                                                 TerminalLauncher terminal_launcher)
    : options_(std::move(options)),
      process_runner_(std::move(process_runner)),
      terminal_launcher_(std::move(terminal_launcher)) {
    configure(std::move(options_));
}

void LinuxFastActionServices::configure(Options options) {
    options_ = std::move(options);
    if (options_.terminal_command.empty()) {
        options_.terminal_command = {"x-terminal-emulator"};
    }
}

bool LinuxFastActionServices::open_terminal(const std::filesystem::path& directory) {
    const auto working_directory = terminal_working_directory(directory);
    if (working_directory.empty()) {
        return false;
    }
    if (terminal_launcher_) {
        return terminal_launcher_(options_.terminal_command, working_directory);
    }
    return launch_terminal_argv(options_.terminal_command, working_directory);
}

ProcessOutput LinuxFastActionServices::run_network_probe(NetworkProbe probe, std::string_view host) {
    return run_argv(network_probe_argv(probe, host));
}

ProcessOutput LinuxFastActionServices::clone_repository(std::string_view url,
                                                        const std::filesystem::path& destination) {
    return run_argv({"git", "clone", std::string{url}, destination.string()});
}

std::optional<std::int64_t> LinuxFastActionServices::parse_datetime(std::string_view value,
                                                                    std::string_view source_zone) {
    if (only_digits(value)) {
        auto parsed = parse_integer(value);
        if (!parsed.has_value()) {
            return std::nullopt;
        }
        if (value.size() == 13) {
            return *parsed / 1000;
        }
        return parsed;
    }

    std::string text{value};
    bool utc_suffix = false;
    if (!text.empty() && text.back() == 'Z') {
        utc_suffix = true;
        text.pop_back();
    }

    for (const char* format : {"%Y-%m-%dT%H:%M:%S", "%Y-%m-%d %H:%M:%S", "%Y-%m-%d"}) {
        if (auto parsed = parse_tm(text, format); parsed.has_value()) {
            if (utc_suffix || is_explicit_utc_zone(source_zone)) {
                return to_epoch_utc(*parsed);
            }
            return to_epoch_in_zone(*parsed, source_zone);
        }
    }
    return std::nullopt;
}

std::string LinuxFastActionServices::format_datetime(std::int64_t epoch_seconds, std::string_view target_zone) {
    const time_t value = static_cast<time_t>(epoch_seconds);
    std::tm parsed{};
    if (is_explicit_utc_zone(target_zone)) {
        if (gmtime_r(&value, &parsed) == nullptr) {
            return {};
        }
    } else {
        const bool ok = with_timezone(target_zone, [&]() {
            return localtime_r(&value, &parsed) != nullptr;
        });
        if (!ok) {
            return {};
        }
    }
    std::array<char, 64> buffer{};
    const char* format = is_explicit_utc_zone(target_zone) ? "%Y-%m-%dT%H:%M:%SZ" : "%Y-%m-%dT%H:%M:%S%z";
    if (strftime(buffer.data(), buffer.size(), format, &parsed) == 0) {
        return {};
    }
    return buffer.data();
}

std::string LinuxFastActionServices::hash_file(const std::filesystem::path& path, HashAlgorithm algorithm) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }

    auto& api = crypto_api();
    if (!api.valid()) {
        return {};
    }
    const char* digest_name = algorithm == HashAlgorithm::Sha256 ? "SHA256" : "SHA512";
    const EvpMd* digest = api.get_digest_by_name(digest_name);
    if (digest == nullptr) {
        return {};
    }
    EvpMdCtx* context = api.ctx_new();
    if (context == nullptr) {
        return {};
    }

    bool ok = api.digest_init(context, digest, nullptr) == 1;
    std::array<char, 16 * 1024> buffer{};
    while (ok && stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = stream.gcount();
        if (count > 0) {
            ok = api.digest_update(context, buffer.data(), static_cast<std::size_t>(count)) == 1;
        }
    }
    if (stream.bad()) {
        ok = false;
    }

    std::array<unsigned char, 64> digest_bytes{};
    unsigned int digest_size = 0;
    if (ok) {
        ok = api.digest_final(context, digest_bytes.data(), &digest_size) == 1;
    }
    api.ctx_free(context);
    if (!ok) {
        return {};
    }
    return hex_digest(digest_bytes.data(), digest_size);
}

ProcessOutput LinuxFastActionServices::run_argv(const std::vector<std::string>& argv) {
    if (process_runner_) {
        return process_runner_(argv);
    }
    return run_process_argv(argv, options_.max_output_bytes);
}

std::filesystem::path LinuxFastActionServices::terminal_working_directory(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::path directory = path;
    if (!std::filesystem::is_directory(path, error)) {
        directory = path.parent_path();
    }
    if (directory.empty()) {
        directory = std::filesystem::current_path(error);
        if (error) {
            return {};
        }
    }
    auto canonical = std::filesystem::weakly_canonical(directory, error);
    if (!error && !canonical.empty()) {
        return canonical.lexically_normal();
    }
    auto absolute = std::filesystem::absolute(directory, error);
    if (!error && !absolute.empty()) {
        return absolute.lexically_normal();
    }
    return directory.lexically_normal();
}

}  // namespace pastit
