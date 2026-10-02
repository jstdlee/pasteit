#pragma once

#include "app/task_center.hpp"
#include "ui/localization.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace pasteit {

enum class TaskCommandKind { None, Pause, Resume, Cancel, Retry, Remove, PauseAll, ResumeAll, CancelAll, ClearDone };

struct TaskCommand {
    TaskCommandKind kind = TaskCommandKind::None;
    std::string id;  // empty for the "all" commands
};

struct TaskPanelState {
    bool open = false;
    int tab = 0;  // 0 tasks, 1 logs
    float anchor_x = 0.0F;
    float anchor_y = 0.0F;
    std::string log_search;
    bool show_level[4] = {false, true, true, true};  // Debug, Info, Warning, Error
};

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
// The utility-cluster status button: idle list icon; while busy an accent
// progress ring (warning when paused) with the active count; a red dot for
// failures and an amber dot for unseen warnings. Returns true when clicked.
bool task_status_button(const TaskCenter& center, bool open, UiLanguage language);

// Popover under the status button with Tasks and Logs tabs. `status_line`
// is the one-sentence app status shown at the top.
TaskCommand draw_task_popover(TaskPanelState& state, TaskCenter& center, std::int64_t now_ms, UiLanguage language,
                              std::string_view status_line, const std::function<void(std::string_view)>& copy_text,
                              const std::function<void()>& open_log_folder);
#endif

}  // namespace pasteit
