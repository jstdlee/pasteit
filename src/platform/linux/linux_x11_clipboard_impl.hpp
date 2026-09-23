#pragma once

#include "storage/clipboard_store.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pastit {

std::vector<std::filesystem::path> parse_x11_uri_list(std::string_view text);
ContentKind classify_x11_clipboard_payload(const std::vector<std::string>& mime_types,
                                           const std::vector<std::byte>& bytes);

class X11ClipboardWatcher {
public:
    X11ClipboardWatcher();
    ~X11ClipboardWatcher();

    X11ClipboardWatcher(const X11ClipboardWatcher&) = delete;
    X11ClipboardWatcher& operator=(const X11ClipboardWatcher&) = delete;

    bool available() const;
    std::optional<ClipboardData> poll();
    void process_events();

    bool set_text(std::string_view text);
    bool set_image(const std::vector<std::byte>& bytes, std::string_view mime_type = "image/png");

private:
    struct OutgoingTransfer {
        unsigned long requestor = 0;
        unsigned long property = 0;
        unsigned long target = 0;
        std::size_t offset = 0;
        std::vector<std::byte> bytes;
    };

    bool handle_x11_event(void* event);
    std::optional<ClipboardData> read_current_selection();

    void* display_ = nullptr;
    unsigned long window_ = 0;
    std::string last_payload_hash_;
    std::vector<std::string> owned_mime_types_;
    std::vector<std::byte> owned_bytes_;
    ContentKind owned_kind_ = ContentKind::Unknown;
    unsigned long ownership_time_ = 0;
    std::vector<OutgoingTransfer> outgoing_transfers_;
};

}  // namespace pastit
