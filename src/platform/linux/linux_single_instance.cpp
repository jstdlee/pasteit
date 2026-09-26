#include "platform/linux/linux_single_instance.hpp"

#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>

namespace pasteit {
namespace {

volatile std::sig_atomic_t show_request_pending = 0;

void handle_show_request(int) {
    show_request_pending = 1;
}

}  // namespace

LinuxSingleInstance::LinuxSingleInstance(std::filesystem::path dir) {
    if (dir.empty()) {
        if (const char* xdg = std::getenv("XDG_RUNTIME_DIR")) dir = xdg;
        else dir = std::filesystem::temp_directory_path();
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    path_ = dir / ("pasteit-" + std::to_string(getuid()) + ".lock");
    fd_ = open(path_.c_str(), O_CREAT | O_RDWR, 0600);
    acquired_ = fd_ >= 0 && flock(fd_, LOCK_EX | LOCK_NB) == 0;
    if (!acquired_) return;

    struct sigaction action {};
    action.sa_handler = handle_show_request;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    (void)sigaction(SIGUSR1, &action, nullptr);

    const auto pid = std::to_string(getpid());
    (void)ftruncate(fd_, 0);
    (void)pwrite(fd_, pid.data(), pid.size(), 0);
}

LinuxSingleInstance::~LinuxSingleInstance() {
    if (fd_ >= 0) {
        if (acquired_) (void)flock(fd_, LOCK_UN);
        (void)close(fd_);
    }
}

bool LinuxSingleInstance::request_show_existing_instance() const {
    if (fd_ < 0 || acquired_) return false;
    char buffer[32]{};
    const auto count = pread(fd_, buffer, sizeof(buffer) - 1, 0);
    if (count <= 0) return false;
    char* end = nullptr;
    const auto pid = std::strtol(buffer, &end, 10);
    if (end == buffer || pid <= 1) return false;
    return kill(static_cast<pid_t>(pid), SIGUSR1) == 0;
}

bool LinuxSingleInstance::take_show_request() const {
    if (!acquired_ || show_request_pending == 0) return false;
    show_request_pending = 0;
    return true;
}

}  // namespace pasteit
