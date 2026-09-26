#include "platform/linux/linux_recent_paths.hpp"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
namespace pasteit {
namespace {
bool numeric(std::string_view value){return !value.empty()&&std::all_of(value.begin(),value.end(),[](unsigned char c){return std::isdigit(c);});}
std::int64_t link_time(const std::filesystem::path& value){struct stat info{};return lstat(value.c_str(),&info)==0?static_cast<std::int64_t>(info.st_mtim.tv_sec)*1000+info.st_mtim.tv_nsec/1000000:0;}
std::int64_t file_time(const std::filesystem::path& value){struct stat info{};return stat(value.c_str(),&info)==0?static_cast<std::int64_t>(info.st_mtim.tv_sec)*1000+info.st_mtim.tv_nsec/1000000:0;}
std::string trim_token(std::string value){while(!value.empty()&&std::string_view{"\"'([{<"}.find(value.front())!=std::string_view::npos)value.erase(value.begin());while(!value.empty()&&std::string_view{"\"'.,;:)]}>"}.find(value.back())!=std::string_view::npos)value.pop_back();return value;}
void append_history_paths(const std::filesystem::path& history_path,std::string source,std::vector<PlatformRecentPath>& result){
 std::ifstream input(history_path);if(!input)return;std::string line;std::size_t line_number=0;const auto base=file_time(history_path);
 while(std::getline(input,line)){
  ++line_number;const auto separator=line.find(';');if(source=="zsh"&&line.starts_with(": ")&&separator!=std::string::npos)line=line.substr(separator+1);
  std::istringstream words(line);std::string token;
  while(words>>token){token=trim_token(std::move(token));if(token.empty()||token.front()!='/')continue;const std::filesystem::path path=std::filesystem::path{token}.lexically_normal();std::error_code ec;const bool directory=std::filesystem::is_directory(path,ec);const bool file=std::filesystem::is_regular_file(path,ec);if(ec||(!directory&&!file))continue;result.push_back({.path=path,.kind=directory?PathKind::Directory:PathKind::File,.source=source,.observed_at_ms=base+static_cast<std::int64_t>(line_number)});}
 }
}
}
std::vector<PlatformRecentPath> LinuxRecentPathCollector::scan_process(std::uint32_t pid)const{
 std::vector<PlatformRecentPath> result;std::error_code ec;const auto dir=std::filesystem::path{"/proc"}/std::to_string(pid)/"fd";
 for(std::filesystem::directory_iterator it(dir,ec),end;!ec&&it!=end;it.increment(ec)){
  const auto link=it->path();auto target=std::filesystem::read_symlink(link,ec);if(ec){ec.clear();continue;}const auto text=target.string();
  if(text.starts_with("socket:")||text.starts_with("pipe:")||text.starts_with("anon_inode:")||text.ends_with(" (deleted)"))continue;
  if(!target.is_absolute())continue;const bool is_dir=std::filesystem::is_directory(target,ec);if(ec){ec.clear();continue;}const bool is_file=std::filesystem::is_regular_file(target,ec);if(ec||(!is_dir&&!is_file)){ec.clear();continue;}
  result.push_back({.path=target.lexically_normal(),.kind=is_dir?PathKind::Directory:PathKind::File,.source="proc-fd",.observed_at_ms=link_time(link)});
 }
 std::sort(result.begin(),result.end(),[](const auto&a,const auto&b){return a.observed_at_ms>b.observed_at_ms;});
 result.erase(std::unique(result.begin(),result.end(),[](const auto&a,const auto&b){return a.path==b.path;}),result.end());return result;
}
std::vector<PlatformRecentPath> LinuxRecentPathCollector::scan_nautilus_service()const{
 return scan_file_manager_services();
}

std::vector<PlatformRecentPath> LinuxRecentPathCollector::scan_file_manager_services()const{
 static constexpr std::string_view names[]={"nautilus","dolphin","thunar","nemo","pcmanfm","caja"};
 std::vector<PlatformRecentPath> result;std::error_code ec;
 for(std::filesystem::directory_iterator it("/proc",ec),end;!ec&&it!=end;it.increment(ec)){
  const auto name=it->path().filename().string();if(!numeric(name))continue;std::ifstream comm(it->path()/"comm");std::string process;std::getline(comm,process);
  const auto match=std::find_if(std::begin(names),std::end(names),[&](const auto candidate){return process.find(candidate)!=std::string::npos;});if(match==std::end(names))continue;
  auto values=scan_process(static_cast<std::uint32_t>(std::stoul(name)));for(auto& value:values)value.source=std::string{*match};result.insert(result.end(),values.begin(),values.end());
 }
 std::sort(result.begin(),result.end(),[](const auto&a,const auto&b){return a.observed_at_ms>b.observed_at_ms;});result.erase(std::unique(result.begin(),result.end(),[](const auto&a,const auto&b){return a.path==b.path;}),result.end());return result;
}

std::vector<PlatformRecentPath> LinuxRecentPathCollector::scan_shell_history()const{
 std::vector<PlatformRecentPath> result;const char* home=std::getenv("HOME");if(home==nullptr)return result;const auto root=std::filesystem::path{home};append_history_paths(root/".bash_history","bash",result);append_history_paths(root/".zsh_history","zsh",result);
 std::sort(result.begin(),result.end(),[](const auto&a,const auto&b){return a.observed_at_ms>b.observed_at_ms;});result.erase(std::unique(result.begin(),result.end(),[](const auto&a,const auto&b){return a.path==b.path;}),result.end());return result;
}
}
