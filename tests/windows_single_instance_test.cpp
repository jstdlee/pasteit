// Windows only: a second instance cannot acquire the lock, and its show
// request reaches the first instance exactly once.
#include "platform/windows/windows_single_instance.hpp"

#include <cassert>

int main() {
    pasteit::WindowsSingleInstance first;
    assert(first.acquired());
    assert(!first.take_show_request());
    {
        pasteit::WindowsSingleInstance second;
        assert(!second.acquired());
        assert(second.request_show_existing_instance());
    }
    assert(first.take_show_request());
    assert(!first.take_show_request());  // auto-reset: taken once
}
