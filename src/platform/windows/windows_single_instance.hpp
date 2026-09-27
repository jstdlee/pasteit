#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace pasteit {

class WindowsSingleInstance {
public:
    WindowsSingleInstance();
    ~WindowsSingleInstance();

    WindowsSingleInstance(const WindowsSingleInstance&) = delete;
    WindowsSingleInstance& operator=(const WindowsSingleInstance&) = delete;

    bool acquired() const { return acquired_; }
    // A second launch asks the running instance to show its popup (same as
    // Linux): it signals a named event the first instance polls each frame.
    bool request_show_existing_instance() const;
    bool take_show_request() const;

private:
    HANDLE mutex_ = nullptr;
    HANDLE show_event_ = nullptr;
    bool acquired_ = false;
};

}  // namespace pasteit
