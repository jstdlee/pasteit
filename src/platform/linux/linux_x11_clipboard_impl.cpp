#include "platform/linux/linux_x11_clipboard_impl.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

#if defined(PASTEIT_HAS_X11)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif

namespace pasteit {
namespace {

[[maybe_unused]] std::int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return std::string{value.substr(begin, end - begin)};
}

int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + ch - 'a';
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + ch - 'A';
    }
    return -1;
}

std::string percent_decode(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2 < value.size()) {
            const int high = hex_value(value[index + 1]);
            const int low = hex_value(value[index + 2]);
            if (high >= 0 && low >= 0) {
                out.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        out.push_back(value[index]);
    }
    return out;
}

std::string lower_ascii(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

bool has_mime(const std::vector<std::string>& mime_types, std::string_view wanted) {
    const auto lower_wanted = lower_ascii(wanted);
    for (const auto& mime : mime_types) {
        if (lower_ascii(mime) == lower_wanted) {
            return true;
        }
    }
    return false;
}

std::string bytes_to_string(const std::vector<std::byte>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool starts_with_ascii(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool looks_like_url(std::string_view value) {
    const auto lower = lower_ascii(value);
    return starts_with_ascii(lower, "http://") || starts_with_ascii(lower, "https://");
}

bool looks_like_email(std::string_view value) {
    const auto at = value.find('@');
    return at > 0 && at != std::string_view::npos && value.find(' ', at) == std::string_view::npos &&
           value.find('.', at) != std::string_view::npos;
}

bool looks_like_json(std::string_view value) {
    if (value.size() < 2) {
        return false;
    }
    return (value.front() == '{' && value.back() == '}') || (value.front() == '[' && value.back() == ']');
}

bool looks_like_local_path(std::string_view value) {
    return starts_with_ascii(value, "/") || starts_with_ascii(value, "~/") || starts_with_ascii(value, "file://");
}

[[maybe_unused]] std::string payload_hash(const std::vector<std::string>& mime_types,
                                          const std::vector<std::byte>& bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&](unsigned char value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    for (const auto& mime : mime_types) {
        for (unsigned char ch : mime) {
            mix(ch);
        }
        mix(0);
    }
    for (std::byte byte : bytes) {
        mix(static_cast<unsigned char>(byte));
    }
    return std::to_string(hash);
}

#if defined(PASTEIT_HAS_X11)

struct X11Atoms {
    Atom clipboard = None;
    Atom targets = None;
    Atom timestamp = None;
    Atom utf8_string = None;
    Atom text = None;
    Atom text_plain = None;
    Atom text_plain_utf8 = None;
    Atom uri_list = None;
    Atom image_png = None;
    Atom image_jpeg = None;
    Atom incr = None;
    Atom pasteit_property = None;
};

X11Atoms make_atoms(Display* display) {
    return X11Atoms{
        .clipboard = XInternAtom(display, "CLIPBOARD", False),
        .targets = XInternAtom(display, "TARGETS", False),
        .timestamp = XInternAtom(display, "TIMESTAMP", False),
        .utf8_string = XInternAtom(display, "UTF8_STRING", False),
        .text = XInternAtom(display, "TEXT", False),
        .text_plain = XInternAtom(display, "text/plain", False),
        .text_plain_utf8 = XInternAtom(display, "text/plain;charset=utf-8", False),
        .uri_list = XInternAtom(display, "text/uri-list", False),
        .image_png = XInternAtom(display, "image/png", False),
        .image_jpeg = XInternAtom(display, "image/jpeg", False),
        .incr = XInternAtom(display, "INCR", False),
        .pasteit_property = XInternAtom(display, "PASTEIT_SELECTION", False),
    };
}

std::string atom_name(Display* display, Atom atom) {
    if (atom == XA_STRING) {
        return "STRING";
    }
    char* raw = XGetAtomName(display, atom);
    if (raw == nullptr) {
        return {};
    }
    std::string name{raw};
    XFree(raw);
    return name;
}

bool is_text_target(const X11Atoms& atoms, Atom target) {
    return target == atoms.utf8_string || target == atoms.text_plain_utf8 || target == atoms.text_plain ||
           target == atoms.text || target == XA_STRING;
}

bool is_image_target(const X11Atoms& atoms, Atom target) {
    return target == atoms.image_png || target == atoms.image_jpeg;
}

std::vector<Atom> advertised_targets(Display* display,
                                     const std::vector<std::string>& mime_types,
                                     ContentKind kind) {
    const auto atoms = make_atoms(display);
    std::vector<Atom> targets{atoms.targets, atoms.timestamp};
    if (kind == ContentKind::Image) {
        if (has_mime(mime_types, "image/jpeg") || has_mime(mime_types, "image/jpg")) {
            targets.push_back(atoms.image_jpeg);
        } else {
            targets.push_back(atoms.image_png);
        }
    } else {
        targets.push_back(atoms.utf8_string);
        targets.push_back(atoms.text_plain_utf8);
        targets.push_back(atoms.text_plain);
        targets.push_back(atoms.text);
        targets.push_back(XA_STRING);
    }
    return targets;
}

struct PropertyValue {
    Atom type = None;
    int format = 0;
    std::vector<std::byte> bytes;
    std::vector<Atom> atoms;
};

std::optional<PropertyValue> read_property(Display* display, Window window, Atom property) {
    PropertyValue value;
    long offset = 0;
    constexpr long chunk_longs = 1024L * 1024L;
    for (;;) {
        Atom actual_type = None;
        int actual_format = 0;
        unsigned long item_count = 0;
        unsigned long bytes_after = 0;
        unsigned char* raw = nullptr;
        const int status = XGetWindowProperty(display, window, property, offset, chunk_longs, False,
                                              AnyPropertyType, &actual_type, &actual_format, &item_count,
                                              &bytes_after, &raw);
        if (status != Success || actual_type == None || (value.type != None && value.type != actual_type) ||
            (value.format != 0 && value.format != actual_format)) {
            if (raw != nullptr) {
                XFree(raw);
            }
            return std::nullopt;
        }
        value.type = actual_type;
        value.format = actual_format;
        if (actual_format == 8) {
            value.bytes.reserve(value.bytes.size() + item_count);
            for (unsigned long index = 0; index < item_count; ++index) {
                value.bytes.push_back(static_cast<std::byte>(raw[index]));
            }
        } else if (actual_format == 16) {
            const auto byte_count = item_count * sizeof(short);
            const auto* begin = reinterpret_cast<const std::byte*>(raw);
            value.bytes.insert(value.bytes.end(), begin, begin + byte_count);
        } else if (actual_format == 32) {
            const auto* atoms = reinterpret_cast<const Atom*>(raw);
            value.atoms.insert(value.atoms.end(), atoms, atoms + item_count);
            const auto byte_count = item_count * sizeof(long);
            const auto* begin = reinterpret_cast<const std::byte*>(raw);
            value.bytes.insert(value.bytes.end(), begin, begin + byte_count);
        }
        if (raw != nullptr) {
            XFree(raw);
        }
        if (bytes_after == 0) {
            break;
        }
        const auto consumed_bits = item_count * static_cast<unsigned long>(actual_format);
        offset += static_cast<long>((consumed_bits + 31UL) / 32UL);
    }
    XDeleteProperty(display, window, property);
    XFlush(display);
    return value;
}

std::string mime_for_target(Display* display, Atom target) {
    const auto atoms = make_atoms(display);
    if (target == atoms.image_png) {
        return "image/png";
    }
    if (target == atoms.image_jpeg) {
        return "image/jpeg";
    }
    if (target == atoms.uri_list) {
        return "text/uri-list";
    }
    if (target == atoms.utf8_string) {
        return "text/plain;charset=utf-8";
    }
    if (target == atoms.text_plain_utf8) {
        return "text/plain;charset=utf-8";
    }
    if (target == atoms.text_plain || target == atoms.text || target == XA_STRING) {
        return "text/plain";
    }
    const auto name = atom_name(display, target);
    return name.empty() ? "application/octet-stream" : name;
}

std::vector<Atom> preferred_targets(Display* display, const std::vector<Atom>& supported) {
    const auto atoms = make_atoms(display);
    const std::vector<Atom> preference{
        atoms.image_png,
        atoms.image_jpeg,
        atoms.uri_list,
        atoms.utf8_string,
        atoms.text_plain_utf8,
        atoms.text_plain,
        atoms.text,
        XA_STRING,
    };
    std::vector<Atom> out;
    for (Atom wanted : preference) {
        if (std::find(supported.begin(), supported.end(), wanted) != supported.end()) {
            out.push_back(wanted);
        }
    }
    if (out.empty()) {
        out = {atoms.utf8_string, atoms.uri_list, atoms.image_png, atoms.image_jpeg, XA_STRING};
    }
    return out;
}

#endif

}  // namespace

std::vector<std::filesystem::path> parse_x11_uri_list(std::string_view text) {
    std::vector<std::filesystem::path> paths;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const auto end = text.find_first_of("\r\n", begin);
        const auto line_view = text.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
        auto line = trim(line_view);
        if (!line.empty() && line.front() != '#') {
            std::string path;
            if (starts_with_ascii(line, "file://")) {
                std::string_view rest{line};
                rest.remove_prefix(7);
                if (starts_with_ascii(rest, "localhost/")) {
                    rest.remove_prefix(9);
                    path = std::string{rest};
                } else if (!rest.empty() && rest.front() == '/') {
                    path = std::string{rest};
                }
            } else if (starts_with_ascii(line, "/")) {
                path = line;
            }
            if (!path.empty()) {
                paths.emplace_back(percent_decode(path));
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
        while (begin < text.size() && (text[begin] == '\r' || text[begin] == '\n')) {
            ++begin;
        }
    }
    return paths;
}

ContentKind classify_x11_clipboard_payload(const std::vector<std::string>& mime_types,
                                           const std::vector<std::byte>& bytes) {
    if (has_mime(mime_types, "image/png") || has_mime(mime_types, "image/jpeg") || has_mime(mime_types, "image/jpg")) {
        return ContentKind::Image;
    }
    const auto text = trim(bytes_to_string(bytes));
    if (has_mime(mime_types, "text/uri-list") && !parse_x11_uri_list(text).empty()) {
        return ContentKind::Path;
    }
    if (looks_like_local_path(text)) {
        return ContentKind::Path;
    }
    if (looks_like_url(text)) {
        return ContentKind::Url;
    }
    if (looks_like_json(text)) {
        return ContentKind::Json;
    }
    if (looks_like_email(text)) {
        return ContentKind::Email;
    }
    return bytes.empty() ? ContentKind::Unknown : ContentKind::Text;
}

X11ClipboardWatcher::X11ClipboardWatcher() {
#if defined(PASTEIT_HAS_X11)
    display_ = XOpenDisplay(nullptr);
    if (display_ != nullptr) {
        auto* display = static_cast<Display*>(display_);
        window_ = XCreateSimpleWindow(display, DefaultRootWindow(display), -10, -10, 1, 1, 0, 0, 0);
        XSelectInput(display, static_cast<Window>(window_), PropertyChangeMask);
        XFlush(display);
    }
#endif
}

X11ClipboardWatcher::~X11ClipboardWatcher() {
#if defined(PASTEIT_HAS_X11)
    if (display_ != nullptr) {
        auto* display = static_cast<Display*>(display_);
        const auto atoms = make_atoms(display);
        if (XGetSelectionOwner(display, atoms.clipboard) == static_cast<Window>(window_)) {
            XSetSelectionOwner(display, atoms.clipboard, None, CurrentTime);
        }
        if (window_ != 0) {
            XDestroyWindow(display, static_cast<Window>(window_));
        }
        XCloseDisplay(display);
    }
#endif
}

bool X11ClipboardWatcher::available() const {
    return display_ != nullptr;
}

std::optional<std::string> X11ClipboardWatcher::owned_text_if_current() const {
#if defined(PASTEIT_HAS_X11)
    if (display_ == nullptr) return std::nullopt;
    auto* display = static_cast<Display*>(display_);
    const auto clipboard = XInternAtom(display, "CLIPBOARD", False);
    if (XGetSelectionOwner(display, clipboard) != static_cast<Window>(window_)) return std::nullopt;
    if (owned_kind_ == ContentKind::Image) return std::string{};
    return bytes_to_string(owned_bytes_);
#else
    return std::nullopt;
#endif
}

std::optional<ClipboardData> X11ClipboardWatcher::poll() {
#if defined(PASTEIT_HAS_X11)
    if (display_ == nullptr) {
        return std::nullopt;
    }
    process_events();
    return read_current_selection();
#else
    return std::nullopt;
#endif
}

void X11ClipboardWatcher::process_events() {
#if defined(PASTEIT_HAS_X11)
    if (display_ == nullptr) {
        return;
    }
    auto* display = static_cast<Display*>(display_);
    while (XPending(display) > 0) {
        XEvent event;
        XNextEvent(display, &event);
        (void)handle_x11_event(&event);
    }
#endif
}

bool X11ClipboardWatcher::set_text(std::string_view text) {
#if defined(PASTEIT_HAS_X11)
    if (display_ == nullptr || window_ == 0) {
        return false;
    }
    owned_bytes_.clear();
    owned_bytes_.reserve(text.size());
    for (unsigned char ch : text) {
        owned_bytes_.push_back(static_cast<std::byte>(ch));
    }
    owned_mime_types_ = {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING"};
    owned_kind_ = classify_x11_clipboard_payload(owned_mime_types_, owned_bytes_);
    auto* display = static_cast<Display*>(display_);
    const auto atoms = make_atoms(display);
    XSetSelectionOwner(display, atoms.clipboard, static_cast<Window>(window_), CurrentTime);
    XFlush(display);
    ownership_time_ = CurrentTime;
    return XGetSelectionOwner(display, atoms.clipboard) == static_cast<Window>(window_);
#else
    (void)text;
    return false;
#endif
}

bool X11ClipboardWatcher::set_image(const std::vector<std::byte>& bytes, std::string_view mime_type) {
#if defined(PASTEIT_HAS_X11)
    if (display_ == nullptr || window_ == 0 || bytes.empty()) {
        return false;
    }
    const auto lower_mime = lower_ascii(mime_type);
    const std::string normalized = (lower_mime == "image/jpeg" || lower_mime == "image/jpg") ? "image/jpeg" : "image/png";
    owned_bytes_ = bytes;
    owned_mime_types_ = {normalized};
    owned_kind_ = ContentKind::Image;
    auto* display = static_cast<Display*>(display_);
    const auto atoms = make_atoms(display);
    XSetSelectionOwner(display, atoms.clipboard, static_cast<Window>(window_), CurrentTime);
    XFlush(display);
    ownership_time_ = CurrentTime;
    return XGetSelectionOwner(display, atoms.clipboard) == static_cast<Window>(window_);
#else
    (void)bytes;
    (void)mime_type;
    return false;
#endif
}

bool X11ClipboardWatcher::handle_x11_event(void* event) {
#if defined(PASTEIT_HAS_X11)
    if (display_ == nullptr || event == nullptr) {
        return false;
    }
    auto* xevent = static_cast<XEvent*>(event);
    auto* display = static_cast<Display*>(display_);
    if (xevent->type == PropertyNotify && xevent->xproperty.state == PropertyDelete) {
        const auto match = std::find_if(outgoing_transfers_.begin(), outgoing_transfers_.end(),
                                        [&](const OutgoingTransfer& transfer) {
                                            return transfer.requestor == xevent->xproperty.window &&
                                                   transfer.property == xevent->xproperty.atom;
                                        });
        if (match == outgoing_transfers_.end()) {
            return false;
        }
        constexpr std::size_t chunk_size = 64U * 1024U;
        const auto remaining = match->bytes.size() - match->offset;
        const auto count = std::min(chunk_size, remaining);
        const auto* begin = reinterpret_cast<const unsigned char*>(match->bytes.data() + match->offset);
        XChangeProperty(display, static_cast<Window>(match->requestor), static_cast<Atom>(match->property),
                        static_cast<Atom>(match->target), 8, PropModeReplace, begin, static_cast<int>(count));
        match->offset += count;
        if (count == 0) {
            outgoing_transfers_.erase(match);
        }
        XFlush(display);
        return true;
    }
    if (xevent->type != SelectionRequest) {
        return false;
    }

    const auto atoms = make_atoms(display);
    const XSelectionRequestEvent& request = xevent->xselectionrequest;
    XSelectionEvent response{};
    response.type = SelectionNotify;
    response.display = request.display;
    response.requestor = request.requestor;
    response.selection = request.selection;
    response.target = request.target;
    response.time = request.time;
    response.property = None;

    const Atom property = request.property == None ? request.target : request.property;
    if (request.selection != atoms.clipboard || owned_bytes_.empty()) {
        XSendEvent(display, request.requestor, False, 0, reinterpret_cast<XEvent*>(&response));
        XFlush(display);
        return true;
    }

    if (request.target == atoms.targets) {
        auto targets = advertised_targets(display, owned_mime_types_, owned_kind_);
        XChangeProperty(display, request.requestor, property, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(targets.data()), static_cast<int>(targets.size()));
        response.property = property;
    } else if (request.target == atoms.timestamp) {
        unsigned long timestamp = ownership_time_;
        XChangeProperty(display, request.requestor, property, XA_INTEGER, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&timestamp), 1);
        response.property = property;
    } else if ((owned_kind_ == ContentKind::Image && is_image_target(atoms, request.target)) ||
               (owned_kind_ != ContentKind::Image && is_text_target(atoms, request.target))) {
        constexpr std::size_t direct_transfer_limit = 64U * 1024U;
        if (owned_bytes_.size() > direct_transfer_limit) {
            const unsigned long total = owned_bytes_.size();
            XSelectInput(display, request.requestor, PropertyChangeMask);
            XChangeProperty(display, request.requestor, property, atoms.incr, 32, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(&total), 1);
            outgoing_transfers_.push_back(OutgoingTransfer{
                .requestor = static_cast<unsigned long>(request.requestor),
                .property = static_cast<unsigned long>(property),
                .target = static_cast<unsigned long>(request.target == XA_STRING ? XA_STRING : request.target),
                .offset = 0,
                .bytes = owned_bytes_,
            });
        } else {
            const Atom type = request.target == XA_STRING ? XA_STRING : request.target;
            XChangeProperty(display, request.requestor, property, type, 8, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(owned_bytes_.data()),
                            static_cast<int>(owned_bytes_.size()));
        }
        response.property = property;
    }

    XSendEvent(display, request.requestor, False, 0, reinterpret_cast<XEvent*>(&response));
    XFlush(display);
    return true;
#else
    (void)event;
    return false;
#endif
}

std::optional<ClipboardData> X11ClipboardWatcher::read_current_selection() {
#if defined(PASTEIT_HAS_X11)
    auto* display = static_cast<Display*>(display_);
    const auto atoms = make_atoms(display);
    const Window owner = XGetSelectionOwner(display, atoms.clipboard);
    if (owner == None) {
        return std::nullopt;
    }

    const auto request_target = [&](Atom target) -> std::optional<PropertyValue> {
        XDeleteProperty(display, static_cast<Window>(window_), atoms.pasteit_property);
        XConvertSelection(display, atoms.clipboard, target, atoms.pasteit_property, static_cast<Window>(window_),
                          CurrentTime);
        XFlush(display);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool receiving_incremental = false;
        PropertyValue incremental;
        while (std::chrono::steady_clock::now() < deadline) {
            while (XPending(display) > 0) {
                XEvent event;
                XNextEvent(display, &event);
                if (event.type == SelectionNotify && event.xselection.selection == atoms.clipboard &&
                    event.xselection.requestor == static_cast<Window>(window_) &&
                    event.xselection.target == target) {
                    if (event.xselection.property == None) {
                        return std::nullopt;
                    }
                    auto value = read_property(display, static_cast<Window>(window_), event.xselection.property);
                    if (!value.has_value() || value->type != atoms.incr) {
                        return value;
                    }
                    receiving_incremental = true;
                    incremental.type = target;
                    incremental.format = 8;
                    continue;
                }
                if (receiving_incremental && event.type == PropertyNotify &&
                    event.xproperty.window == static_cast<Window>(window_) &&
                    event.xproperty.atom == atoms.pasteit_property && event.xproperty.state == PropertyNewValue) {
                    auto chunk = read_property(display, static_cast<Window>(window_), atoms.pasteit_property);
                    if (!chunk.has_value()) {
                        return std::nullopt;
                    }
                    if (chunk->bytes.empty()) {
                        return incremental;
                    }
                    incremental.bytes.insert(incremental.bytes.end(), chunk->bytes.begin(), chunk->bytes.end());
                    continue;
                }
                (void)handle_x11_event(&event);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return std::nullopt;
    };

    std::vector<Atom> targets;
    if (const auto value = request_target(atoms.targets); value.has_value() && !value->atoms.empty()) {
        targets = value->atoms;
    }

    for (Atom target : preferred_targets(display, targets)) {
        auto value = request_target(target);
        if (!value.has_value() || value->bytes.empty()) {
            continue;
        }

        std::vector<std::string> mime_types{mime_for_target(display, target)};
        if (target == atoms.utf8_string) {
            mime_types.push_back("UTF8_STRING");
        }
        const auto hash = payload_hash(mime_types, value->bytes);
        if (hash == last_payload_hash_) {
            return std::nullopt;
        }
        last_payload_hash_ = hash;
        return ClipboardData{
            .mime_types = mime_types,
            .bytes = value->bytes,
            .kind = classify_x11_clipboard_payload(mime_types, value->bytes),
            .source_app = "x11",
            .captured_at_ms = now_ms(),
        };
    }
    return std::nullopt;
#else
    return std::nullopt;
#endif
}

}  // namespace pasteit
