#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace pasteit {

class WindowsSingleInstance {
public:
    WindowsSingleInstance();
    ~WindowsSingleInstance();

    WindowsSingleInstance(const WindowsSingleInstance&) = delete;
    WindowsSingleInstance& operator=(const WindowsSingleInstance&) = delete;

    bool acquired() const { return acquired_; }

private:
    HANDLE mutex_ = nullptr;
    bool acquired_ = false;
};

}  // namespace pasteit
