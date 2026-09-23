#include "platform/windows/windows_single_instance.hpp"

namespace pastit {

WindowsSingleInstance::WindowsSingleInstance() {
    mutex_ = CreateMutexW(nullptr, TRUE, L"Local\\PasteItSingleInstance");
    acquired_ = mutex_ != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
}

WindowsSingleInstance::~WindowsSingleInstance() {
    if (mutex_ != nullptr) {
        if (acquired_) {
            ReleaseMutex(mutex_);
        }
        CloseHandle(mutex_);
    }
}

}  // namespace pastit
