#include "platform/windows/windows_fast_action_services.hpp"

#include "platform/windows/windows_strings.hpp"
#include "util/path_utf8.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cctype>
#include <ctime>
#include <exception>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <utility>

namespace pastit {
namespace {

void close_handle(HANDLE& handle) {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

void append_bounded(std::string& output, const char* data, std::size_t size, std::size_t limit) {
    if (output.size() >= limit) {
        return;
    }
    const auto available = limit - output.size();
    output.append(data, std::min(size, available));
}

std::string read_pipe(HANDLE pipe, std::size_t limit) {
    std::string output;
    std::array<char, 4096> buffer{};
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) || read == 0) {
            break;
        }
        append_bounded(output, buffer.data(), read, limit);
    }
    CloseHandle(pipe);
    return output;
}

ProcessOutput run_process_argv_capture(const std::vector<std::string>& argv, std::size_t output_limit,
                                       const std::filesystem::path& working_directory = {}) {
    if (argv.empty() || argv.front().empty()) {
        return {.exit_code = -1, .stdout_text = {}, .stderr_text = "empty argv"};
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stderr_read = nullptr;
    HANDLE stderr_write = nullptr;
    if (!CreatePipe(&stdout_read, &stdout_write, &security, 0) ||
        !CreatePipe(&stderr_read, &stderr_write, &security, 0)) {
        close_handle(stdout_read);
        close_handle(stdout_write);
        close_handle(stderr_read);
        close_handle(stderr_write);
        return {.exit_code = -1, .stdout_text = {}, .stderr_text = "could not create process pipes"};
    }
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = stdout_write;
    startup.hStdError = stderr_write;

    PROCESS_INFORMATION process{};
    auto command = windows_command_line(argv);
    const auto cwd = working_directory.empty() ? std::wstring{} : path_to_wide(working_directory);
    const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        cwd.empty() ? nullptr : cwd.c_str(), &startup, &process);
    close_handle(stdout_write);
    close_handle(stderr_write);
    if (!created) {
        close_handle(stdout_read);
        close_handle(stderr_read);
        return {.exit_code = -1, .stdout_text = {}, .stderr_text = "CreateProcessW failed"};
    }

    std::string stdout_text;
    std::string stderr_text;
    std::thread stdout_reader([&] { stdout_text = read_pipe(stdout_read, output_limit); });
    std::thread stderr_reader([&] { stderr_text = read_pipe(stderr_read, output_limit); });

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
        exit_code = static_cast<DWORD>(-1);
    }
    close_handle(process.hThread);
    close_handle(process.hProcess);
    stdout_reader.join();
    stderr_reader.join();

    return {.exit_code = static_cast<int>(exit_code),
            .stdout_text = std::move(stdout_text),
            .stderr_text = std::move(stderr_text)};
}

bool launch_process_detached(const std::vector<std::string>& argv, const std::filesystem::path& working_directory) {
    if (argv.empty() || argv.front().empty()) {
        return false;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    auto command = windows_command_line(argv);
    const auto cwd = working_directory.empty() ? std::wstring{} : path_to_wide(working_directory);
    const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                                        CREATE_NEW_CONSOLE, nullptr,
                                        cwd.empty() ? nullptr : cwd.c_str(), &startup, &process);
    if (!created) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

std::string lowercase_ascii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::vector<std::string> terminal_argv_for(const std::vector<std::string>& base,
                                           const std::filesystem::path& working_directory) {
    auto argv = base.empty() ? std::vector<std::string>{"cmd.exe"} : base;
    const auto exe = lowercase_ascii(path_to_utf8_string(path_from_utf8_string(argv.front()).filename()));
    const auto directory = path_to_utf8(working_directory);
    if (exe == "wt.exe" || exe == "wt") {
        argv.push_back("-d");
        argv.push_back(directory);
    }
    return argv;
}

std::vector<std::string> network_probe_argv(NetworkProbe probe, std::string_view host) {
    const std::string target{host};
    switch (probe) {
        case NetworkProbe::Ping:
            return {"ping", "-n", "4", target};
        case NetworkProbe::TraceRoute:
            return {"tracert", target};
        case NetworkProbe::ReverseDns:
            return {"nslookup", target};
        case NetworkProbe::Dig:
            return {"nslookup", "-type=any", target};
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
    std::istringstream input{std::string{value}};
    input >> std::get_time(&parsed, format);
    if (input.fail()) {
        return std::nullopt;
    }
    input >> std::ws;
    if (!input.eof()) {
        return std::nullopt;
    }
    return parsed;
}

bool is_explicit_utc_zone(std::string_view zone) {
    return zone == "UTC" || zone == "Etc/UTC" || zone == "Z";
}

std::optional<std::int64_t> to_epoch_utc(std::tm parsed) {
    const auto value = _mkgmtime(&parsed);
    if (value == static_cast<time_t>(-1)) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(value);
}

#if defined(_MSC_VER)
const std::chrono::time_zone* named_zone(std::string_view name) {
    if (name == "local" || name == "Local") {
        return std::chrono::current_zone();
    }
    return std::chrono::locate_zone(name);
}
#endif

std::string hex_digest(const unsigned char* bytes, std::size_t size) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < size; ++index) {
        output << std::setw(2) << static_cast<int>(bytes[index]);
    }
    return output.str();
}

std::string bcrypt_hash_file(const std::filesystem::path& path, LPCWSTR algorithm_name) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, algorithm_name, nullptr, 0) < 0) {
        return {};
    }

    DWORD object_length = 0;
    DWORD bytes = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_length),
                          sizeof(object_length), &bytes, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    DWORD hash_length = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hash_length),
                          sizeof(hash_length), &bytes, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    std::vector<unsigned char> object(object_length);
    std::vector<unsigned char> digest(hash_length);
    if (BCryptCreateHash(algorithm, &hash, object.data(), object_length, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    bool ok = true;
    std::array<char, 16 * 1024> buffer{};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = stream.gcount();
        if (count > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                                        static_cast<ULONG>(count), 0) < 0) {
            ok = false;
            break;
        }
    }
    if (stream.bad()) {
        ok = false;
    }
    if (ok && BCryptFinishHash(hash, digest.data(), hash_length, 0) < 0) {
        ok = false;
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok ? hex_digest(digest.data(), digest.size()) : std::string{};
}

}  // namespace

WindowsFastActionServices::WindowsFastActionServices() : WindowsFastActionServices(Options{}) {}

WindowsFastActionServices::WindowsFastActionServices(Options options) : options_(std::move(options)) {
    if (options_.terminal_command.empty()) {
        options_.terminal_command = {"cmd.exe"};
    }
}

void WindowsFastActionServices::configure(Options options) {
    if (options.terminal_command.empty()) {
        options.terminal_command = {"cmd.exe"};
    }
    options_ = std::move(options);
}

bool WindowsFastActionServices::open_terminal(const std::filesystem::path& directory) {
    const auto working_directory = terminal_working_directory(directory);
    if (working_directory.empty()) {
        return false;
    }
    return launch_process_detached(terminal_argv_for(options_.terminal_command, working_directory), working_directory);
}

ProcessOutput WindowsFastActionServices::run_network_probe(NetworkProbe probe, std::string_view host) {
    return run_argv(network_probe_argv(probe, host));
}

ProcessOutput WindowsFastActionServices::clone_repository(std::string_view url,
                                                          const std::filesystem::path& destination) {
    return run_argv({"git", "clone", std::string{url}, path_to_utf8(destination)});
}

std::optional<std::int64_t> WindowsFastActionServices::parse_datetime(std::string_view value,
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
#if defined(_MSC_VER)
            try {
                using namespace std::chrono;
                const year_month_day date{year{parsed->tm_year + 1900},
                                          month{static_cast<unsigned>(parsed->tm_mon + 1)},
                                          std::chrono::day{static_cast<unsigned>(parsed->tm_mday)}};
                if (!date.ok()) {
                    return std::nullopt;
                }
                const auto local = local_days{date} + hours{parsed->tm_hour} +
                                   minutes{parsed->tm_min} + seconds{parsed->tm_sec};
                return named_zone(source_zone)->to_sys(local).time_since_epoch().count();
            } catch (const std::exception&) {
                return std::nullopt;
            }
#else
            // MinGW's standard library may not provide the C++20 time-zone
            // database. Report unsupported rather than using local time.
            return std::nullopt;
#endif
        }
    }
    return std::nullopt;
}

std::string WindowsFastActionServices::format_datetime(std::int64_t epoch_seconds, std::string_view target_zone) {
    const auto value = static_cast<time_t>(epoch_seconds);
    std::tm parsed{};
    if (is_explicit_utc_zone(target_zone)) {
        if (gmtime_s(&parsed, &value) != 0) {
            return {};
        }
        std::array<char, 64> buffer{};
        if (std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &parsed) == 0) {
            return {};
        }
        return buffer.data();
    }

#if defined(_MSC_VER)
    try {
        using namespace std::chrono;
        const auto* zone = named_zone(target_zone);
        const sys_seconds instant{seconds{epoch_seconds}};
        const auto local = zone->to_local(instant);
        const auto local_day = floor<days>(local);
        const year_month_day date{sys_days{local_day.time_since_epoch()}};
        const hh_mm_ss clock{local - local_day};
        const auto offset = zone->get_info(instant).offset.count();
        const auto offset_minutes = static_cast<std::int64_t>(offset < 0 ? -offset : offset) / 60;
        std::ostringstream out;
        out << std::setfill('0') << std::setw(4) << int(date.year()) << '-'
            << std::setw(2) << unsigned(date.month()) << '-'
            << std::setw(2) << unsigned(date.day()) << 'T'
            << std::setw(2) << clock.hours().count() << ':'
            << std::setw(2) << clock.minutes().count() << ':'
            << std::setw(2) << clock.seconds().count()
            << (offset < 0 ? '-' : '+')
            << std::setw(2) << offset_minutes / 60
            << std::setw(2) << offset_minutes % 60;
        return out.str();
    } catch (const std::exception&) {
        return {};
    }
#else
    return {};
#endif
}

std::string WindowsFastActionServices::hash_file(const std::filesystem::path& path, HashAlgorithm algorithm) {
    return bcrypt_hash_file(path, algorithm == HashAlgorithm::Sha256 ? BCRYPT_SHA256_ALGORITHM : BCRYPT_SHA512_ALGORITHM);
}

ProcessOutput WindowsFastActionServices::run_argv(const std::vector<std::string>& argv) {
    return run_process_argv_capture(argv, options_.max_output_bytes);
}

std::filesystem::path WindowsFastActionServices::terminal_working_directory(const std::filesystem::path& path) const {
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
