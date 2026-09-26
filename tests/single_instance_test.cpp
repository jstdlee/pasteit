#include "platform/linux/linux_single_instance.hpp"
#include <cassert>
int main(){
    const auto root=std::filesystem::temp_directory_path()/"pasteit-single-instance-test";
    std::filesystem::create_directories(root);
    {
        pasteit::LinuxSingleInstance first(root);
        assert(first.acquired());
        pasteit::LinuxSingleInstance second(root);
        assert(!second.acquired());
        assert(second.request_show_existing_instance());
        assert(first.take_show_request());
        assert(!first.take_show_request());
    }
    {
        pasteit::LinuxSingleInstance third(root);
        assert(third.acquired());
    }
    std::filesystem::remove_all(root);
}
