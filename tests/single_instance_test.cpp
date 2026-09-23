#include "platform/linux/linux_single_instance.hpp"
#include <cassert>
int main(){const auto root=std::filesystem::temp_directory_path()/"pastit-single-instance-test";std::filesystem::create_directories(root);{pastit::LinuxSingleInstance first(root);assert(first.acquired());pastit::LinuxSingleInstance second(root);assert(!second.acquired());}{pastit::LinuxSingleInstance third(root);assert(third.acquired());}std::filesystem::remove_all(root);}
