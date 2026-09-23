#pragma once
#include <filesystem>
namespace pastit {
class LinuxSingleInstance{
public:explicit LinuxSingleInstance(std::filesystem::path runtime_directory={});~LinuxSingleInstance();LinuxSingleInstance(const LinuxSingleInstance&)=delete;LinuxSingleInstance& operator=(const LinuxSingleInstance&)=delete;bool acquired()const{return acquired_;}const std::filesystem::path& lock_path()const{return path_;}
private:int fd_=-1;bool acquired_=false;std::filesystem::path path_;
};
}
