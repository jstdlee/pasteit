#include "ui/desktop_app.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    // --software-render (or PASTEIT_SOFTWARE_GL=1): Mesa's software OpenGL,
    // for ARM boards and virtual machines whose GPU driver misbehaves.
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--software-render") == 0) {
#if defined(_WIN32)
            _putenv_s("PASTEIT_SOFTWARE_GL", "1");
#else
            setenv("PASTEIT_SOFTWARE_GL", "1", 1);
#endif
        }
    }
    const auto missing = pasteit::desktop_dependency_status();
    if (!missing.empty()) {
        std::cout << "PasteIt MVP core is available.\n";
        std::cout << "Desktop popup fallback is active; missing: " << missing << "\n";
        return 0;
    }
    return pasteit::run_desktop_app();
}
