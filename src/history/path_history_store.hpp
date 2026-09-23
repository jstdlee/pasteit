#pragma once
#include "history/path_history.hpp"
#include <filesystem>
#include <string>
namespace pastit {
struct PathHistoryLoadResult{PathHistory history;std::string warning;};
class PathHistoryStore{
public:PathHistoryStore(std::filesystem::path path,std::size_t limit=100):path_(std::move(path)),limit_(limit){}
PathHistoryLoadResult load()const;bool save(const PathHistory& history,std::string& error)const;
private:std::filesystem::path path_;std::size_t limit_;
};
}
