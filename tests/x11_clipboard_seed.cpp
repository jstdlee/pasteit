#include "platform/linux/linux_x11_clipboard_impl.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: x11_clipboard_seed TEXT [SECONDS]\n"
                     "   or: x11_clipboard_seed --image FILE MIME [SECONDS]\n";
        return 2;
    }
    pastit::X11ClipboardWatcher owner;
    if (!owner.available()) {
        std::cerr << "could not open the X11 display\n";
        return 1;
    }

    int seconds = 5;
    bool owned = false;
    if (std::string_view{argv[1]} == "--image") {
        if (argc < 4) {
            std::cerr << "--image requires FILE and MIME\n";
            return 2;
        }
        std::ifstream input(argv[2], std::ios::binary);
        const std::vector<char> chars((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        std::vector<std::byte> data;
        data.reserve(chars.size());
        for (const char ch : chars) {
            data.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
        }
        owned = !data.empty() && owner.set_image(data, argv[3]);
        if (argc >= 5) {
            seconds = std::max(1, std::stoi(argv[4]));
        }
    } else {
        owned = owner.set_text(argv[1]);
        if (argc >= 3) {
            seconds = std::max(1, std::stoi(argv[2]));
        }
    }
    if (!owned) {
        std::cerr << "could not own the X11 clipboard\n";
        return 1;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{seconds};
    while (std::chrono::steady_clock::now() < deadline) {
        owner.process_events();
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    return 0;
}
