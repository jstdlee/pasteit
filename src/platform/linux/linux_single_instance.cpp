#include "platform/linux/linux_single_instance.hpp"
#include <cstdlib>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
namespace pastit {
LinuxSingleInstance::LinuxSingleInstance(std::filesystem::path dir){if(dir.empty()){if(const char* xdg=std::getenv("XDG_RUNTIME_DIR"))dir=xdg;else dir=std::filesystem::temp_directory_path();}std::error_code ec;std::filesystem::create_directories(dir,ec);path_=dir/("pastit-"+std::to_string(getuid())+".lock");fd_=open(path_.c_str(),O_CREAT|O_RDWR,0600);acquired_=fd_>=0&&flock(fd_,LOCK_EX|LOCK_NB)==0;}
LinuxSingleInstance::~LinuxSingleInstance(){if(fd_>=0){if(acquired_)flock(fd_,LOCK_UN);close(fd_);}}
}
