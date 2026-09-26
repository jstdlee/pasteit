#include "pipeline/process_runner.hpp"

#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <thread>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace pastit {

std::optional<std::string> find_executable(std::string_view name) {
    if (name.empty() || name.find('/') != std::string_view::npos || name.find('\\') != std::string_view::npos) {
        return std::nullopt;  // Only bare names resolved on PATH.
    }
    const char* path = std::getenv("PATH");
    if (path == nullptr) return std::nullopt;
#if defined(_WIN32)
    constexpr char separator = ';';
    const std::vector<std::string> suffixes{".exe", ".cmd", ".bat", ""};
#else
    constexpr char separator = ':';
    const std::vector<std::string> suffixes{""};
#endif
    std::string_view paths{path};
    while (!paths.empty()) {
        const auto end = paths.find(separator);
        const auto directory = paths.substr(0, end);
        for (const auto& suffix : suffixes) {
            std::error_code error;
            const auto candidate = std::filesystem::path{std::string{directory}} / (std::string{name} + suffix);
            if (std::filesystem::is_regular_file(candidate, error)) {
#if !defined(_WIN32)
                if (access(candidate.c_str(), X_OK) != 0) continue;
#endif
                return candidate.string();
            }
        }
        if (end == std::string_view::npos) break;
        paths.remove_prefix(end + 1);
    }
    return std::nullopt;
}

#if defined(_WIN32)

namespace {

std::wstring widen(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

// CommandLineToArgvW-compatible quoting.
std::wstring quote_argument(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) return argument;
    std::wstring out = L"\"";
    for (auto it = argument.begin();; ++it) {
        std::size_t backslashes = 0;
        while (it != argument.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == argument.end()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
        } else {
            out.append(backslashes, L'\\');
        }
        out.push_back(*it);
    }
    out.push_back(L'"');
    return out;
}

void read_all(HANDLE pipe, std::string& out, std::size_t max_output, bool& truncated) {
    char buffer[8192];
    DWORD read = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        const auto room = max_output > out.size() ? max_output - out.size() : 0;
        if (read > room) truncated = true;
        out.append(buffer, std::min<std::size_t>(read, room));
    }
}

}  // namespace

ProcessRun run_process_with_input(const std::vector<std::string>& argv, std::string_view input,
                                  std::chrono::milliseconds timeout, std::size_t max_output) {
    ProcessRun run;
    if (argv.empty()) return run;
    const auto executable = find_executable(argv.front());
    if (!executable) return run;
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE in_read = nullptr, in_write = nullptr, out_read = nullptr, out_write = nullptr, err_read = nullptr, err_write = nullptr;
    if (!CreatePipe(&in_read, &in_write, &security, 0) || !CreatePipe(&out_read, &out_write, &security, 0) ||
        !CreatePipe(&err_read, &err_write, &security, 0)) {
        return run;
    }
    SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);
    std::wstring command_line = quote_argument(widen(*executable));
    for (std::size_t index = 1; index < argv.size(); ++index) command_line += L" " + quote_argument(widen(argv[index]));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = in_read;
    startup.hStdOutput = out_write;
    startup.hStdError = err_write;
    PROCESS_INFORMATION process{};
    const auto wide_executable = widen(*executable);
    run.started = CreateProcessW(wide_executable.c_str(), command_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                 nullptr, nullptr, &startup, &process) != 0;
    CloseHandle(in_read);
    CloseHandle(out_write);
    CloseHandle(err_write);
    if (!run.started) {
        CloseHandle(in_write);
        CloseHandle(out_read);
        CloseHandle(err_read);
        return run;
    }
    std::thread writer([in_write, data = std::string{input}] {
        DWORD written = 0;
        std::size_t offset = 0;
        while (offset < data.size() &&
               WriteFile(in_write, data.data() + offset, static_cast<DWORD>(std::min<std::size_t>(data.size() - offset, 1 << 16)),
                         &written, nullptr)) {
            offset += written;
        }
        CloseHandle(in_write);
    });
    bool out_truncated = false, err_truncated = false;
    std::thread out_reader([&] { read_all(out_read, run.output, max_output, out_truncated); });
    std::thread err_reader([&] { read_all(err_read, run.error_output, 64 * 1024, err_truncated); });
    if (WaitForSingleObject(process.hProcess, static_cast<DWORD>(timeout.count())) == WAIT_TIMEOUT) {
        run.timed_out = true;
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
    }
    writer.join();
    out_reader.join();
    err_reader.join();
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    run.exit_code = static_cast<int>(code);
    run.truncated = out_truncated;
    CloseHandle(out_read);
    CloseHandle(err_read);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return run;
}

#else

ProcessRun run_process_with_input(const std::vector<std::string>& argv, std::string_view input,
                                  std::chrono::milliseconds timeout, std::size_t max_output) {
    ProcessRun run;
    if (argv.empty()) return run;
    const auto executable = find_executable(argv.front());
    if (!executable) return run;
    int in_pipe[2], out_pipe[2], err_pipe[2];
    if (pipe(in_pipe) != 0) return run;
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        return run;
    }
    if (pipe(err_pipe) != 0) {
        close(in_pipe[0]); close(in_pipe[1]); close(out_pipe[0]); close(out_pipe[1]);
        return run;
    }
    // Prepared before fork: the child of a threaded process may only call
    // async-signal-safe functions.
    std::vector<char*> args;
    for (const auto& argument : argv) args.push_back(const_cast<char*>(argument.c_str()));
    args.push_back(nullptr);
    const pid_t child = fork();
    if (child < 0) {
        for (int fd : {in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1]}) close(fd);
        return run;
    }
    if (child == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        for (int fd : {in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1]}) close(fd);
        execv(executable->c_str(), args.data());
        _exit(127);
    }
    run.started = true;
    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);
    for (int fd : {in_pipe[1], out_pipe[0], err_pipe[0]}) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    // A child that exits early must not kill PasteIt with SIGPIPE; ignoring it
    // process-wide is safe because every write here checks its result.
    static std::once_flag ignore_sigpipe;
    std::call_once(ignore_sigpipe, [] { std::signal(SIGPIPE, SIG_IGN); });

    std::size_t written = 0;
    int input_fd = in_pipe[1];
    if (input.empty()) {
        close(input_fd);
        input_fd = -1;
    }
    bool out_open = true, err_open = true;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    char buffer[8192];
    while (out_open || err_open) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            run.timed_out = true;
            kill(child, SIGKILL);
            break;
        }
        pollfd fds[3];
        nfds_t count = 0;
        int out_index = -1, err_index = -1, in_index = -1;
        if (out_open) { out_index = static_cast<int>(count); fds[count++] = {out_pipe[0], POLLIN, 0}; }
        if (err_open) { err_index = static_cast<int>(count); fds[count++] = {err_pipe[0], POLLIN, 0}; }
        if (input_fd >= 0) { in_index = static_cast<int>(count); fds[count++] = {input_fd, POLLOUT, 0}; }
        if (poll(fds, count, static_cast<int>(std::min<long long>(remaining.count(), 200))) < 0 && errno != EINTR) break;
        if (in_index >= 0 && (fds[in_index].revents & (POLLOUT | POLLERR | POLLHUP))) {
            const auto chunk = std::min<std::size_t>(input.size() - written, 1 << 16);
            const auto result = write(input_fd, input.data() + written, chunk);
            if (result > 0) written += static_cast<std::size_t>(result);
            if (result < 0 && errno != EAGAIN) written = input.size();
            if (written >= input.size()) {
                close(input_fd);
                input_fd = -1;
            }
        }
        const auto drain = [&](int index, int fd, std::string& out, std::size_t cap, bool& open) {
            if (index < 0 || !(fds[index].revents & (POLLIN | POLLHUP | POLLERR))) return;
            const auto result = read(fd, buffer, sizeof(buffer));
            if (result > 0) {
                const auto room = cap > out.size() ? cap - out.size() : 0;
                if (static_cast<std::size_t>(result) > room && &out == &run.output) run.truncated = true;
                out.append(buffer, std::min<std::size_t>(static_cast<std::size_t>(result), room));
            } else if (result == 0 || (result < 0 && errno != EAGAIN && errno != EINTR)) {
                open = false;
            }
        };
        drain(out_index, out_pipe[0], run.output, max_output, out_open);
        drain(err_index, err_pipe[0], run.error_output, 64 * 1024, err_open);
    }
    if (input_fd >= 0) close(input_fd);
    close(out_pipe[0]);
    close(err_pipe[0]);
    int status = 0;
    waitpid(child, &status, 0);
    run.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return run;
}

#endif

}  // namespace pastit
