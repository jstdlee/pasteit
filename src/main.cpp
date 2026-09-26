#include "ui/desktop_app.hpp"

#include <iostream>

int main() {
    const auto missing = pasteit::desktop_dependency_status();
    if (!missing.empty()) {
        std::cout << "PasteIt MVP core is available.\n";
        std::cout << "Desktop popup fallback is active; missing: " << missing << "\n";
        return 0;
    }
    return pasteit::run_desktop_app();
}
