#include "ui/desktop_app.hpp"
#include "ui/desktop_runtime.hpp"

#include <iostream>

namespace pasteit {

std::string desktop_dependency_status() {
    return PASTEIT_DESKTOP_MISSING;
}

int run_desktop_app() {
#if defined(PASTEIT_HAS_DESKTOP_DEPS)
    return run_desktop_runtime();
#else
    std::cout << "PasteIt MVP core is available.\n";
    std::cout << "Desktop popup fallback is active; missing: " << desktop_dependency_status() << "\n";
    return 0;
#endif
}

}  // namespace pasteit
