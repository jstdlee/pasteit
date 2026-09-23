#include "platform/linux/linux_recent_paths.hpp"
#include <cassert>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>
int main(){
 const auto root=std::filesystem::temp_directory_path()/"pastit-proc-path-test";std::filesystem::remove_all(root);std::filesystem::create_directories(root/"dir");std::ofstream(root/"file.txt")<<"x";
 const int file=open((root/"file.txt").c_str(),O_RDONLY);const int dir=open((root/"dir").c_str(),O_RDONLY|O_DIRECTORY);assert(file>=0&&dir>=0);
 const auto paths=pastit::LinuxRecentPathCollector{}.scan_process(static_cast<std::uint32_t>(getpid()));
 bool saw_file=false,saw_dir=false;for(const auto& item:paths){saw_file|=item.path==root/"file.txt";saw_dir|=item.path==root/"dir";}
 close(file);close(dir);std::filesystem::remove_all(root);assert(saw_file&&saw_dir);
}
