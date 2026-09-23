#pragma once

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pastit {

struct FocusContext {
    std::uint64_t window_id = 0;
    std::string app_name;
    std::string window_title;
    std::string focused_target_hash;
    std::uint32_t pid = 0;
    std::optional<std::filesystem::path> current_directory;
};

std::string parse_x11_wm_class(std::string_view raw);
std::vector<unsigned int> x11_lock_modifier_variants();

class X11ContextService {
public:
    X11ContextService();
    ~X11ContextService();

    X11ContextService(const X11ContextService&) = delete;
    X11ContextService& operator=(const X11ContextService&) = delete;

    bool available() const;
    bool register_ctrl_alt_f_shortcut();
    bool poll_ctrl_alt_f_shortcut();
    FocusContext collect_focus_context() const;
    bool focus_and_paste(const FocusContext& context) const;

private:
    void* display_ = nullptr;
    unsigned long root_ = 0;
    unsigned int shortcut_keycode_ = 0;
    bool shortcut_registered_ = false;
};

FocusContext collect_x11_context();
bool register_ctrl_alt_f_shortcut();
bool poll_ctrl_alt_f_shortcut();
bool focus_x11_target_and_paste(const FocusContext& context);

}  // namespace pastit
