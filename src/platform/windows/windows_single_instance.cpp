#include "platform/windows/windows_single_instance.hpp"

namespace pasteit {
namespace {
constexpr const wchar_t* kMutexName = L"Local\\PasteItSingleInstance";
constexpr const wchar_t* kShowEventName = L"Local\\PasteItShowRequest";
}  // namespace

WindowsSingleInstance::WindowsSingleInstance() {
    mutex_ = CreateMutexW(nullptr, TRUE, kMutexName);
    acquired_ = mutex_ != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
    if (acquired_) {
        // Auto-reset: each signal from a second launch is taken once.
        show_event_ = CreateEventW(nullptr, FALSE, FALSE, kShowEventName);
    }
}

WindowsSingleInstance::~WindowsSingleInstance() {
    if (show_event_ != nullptr) CloseHandle(show_event_);
    if (mutex_ != nullptr) {
        if (acquired_) {
            ReleaseMutex(mutex_);
        }
        CloseHandle(mutex_);
    }
}

bool WindowsSingleInstance::request_show_existing_instance() const {
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, kShowEventName);
    if (event == nullptr) return false;
    // The launching process owns the foreground; let the running instance
    // take it when it shows the popup.
    AllowSetForegroundWindow(ASFW_ANY);
    const bool signalled = SetEvent(event) != FALSE;
    CloseHandle(event);
    return signalled;
}

bool WindowsSingleInstance::take_show_request() const {
    return show_event_ != nullptr && WaitForSingleObject(show_event_, 0) == WAIT_OBJECT_0;
}

}  // namespace pasteit
