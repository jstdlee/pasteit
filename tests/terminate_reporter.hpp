#pragma once

// Force-included into every test on Windows: an uncaught exception otherwise
// aborts with 0xc0000409 and no message. Print what() before terminating.
#include <cstdio>
#include <cstdlib>
#include <exception>

namespace pasteit_test {
[[noreturn]] inline void report_terminate() {
    if (const auto current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "Uncaught exception: %s\n", error.what());
        } catch (...) {
            std::fprintf(stderr, "Uncaught non-standard exception\n");
        }
    } else {
        std::fprintf(stderr, "std::terminate called without an exception\n");
    }
    std::fflush(stderr);
    std::_Exit(3);
}
inline const bool terminate_reporter_installed = (std::set_terminate(report_terminate), true);
}  // namespace pasteit_test
