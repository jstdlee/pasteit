#include "history/path_history.hpp"
#include "history/path_history_store.hpp"
#include <cassert>
#include <fstream>
int main(){
 const auto root=std::filesystem::temp_directory_path()/"pastit-path-history-test";std::filesystem::remove_all(root);std::filesystem::create_directories(root/"folder");
 std::ofstream(root/"folder/a b.txt")<<"x";
 std::ofstream(root/"folder/proc-only.txt")<<"y";
 pastit::PathHistory history;
 auto file=history.observe("file://"+(root/"folder/a%20b.txt").string(),"clipboard",10);assert(file&&file->kind==pastit::PathKind::File);
 history.observe_path(root/"folder",pastit::PathKind::Directory,"nautilus",20);
 history.observe_path(root/"folder/proc-only.txt",pastit::PathKind::File,"proc-fd",30);
 assert(history.recent(10).size()==2);assert(history.destination_directories(10).size()==1);
 history.retain_latest(1);assert(history.recent(10).size()==1);
 history.clear();assert(history.recent(10).empty());
 history.observe_path(root/"folder",pastit::PathKind::Directory,"bash",40);
 pastit::PathHistoryStore store(root/"paths.json",10);std::string error;assert(store.save(history,error));
 const auto loaded=store.load();assert(loaded.warning.empty());assert(loaded.history.recent(10).size()==1);
 std::filesystem::remove_all(root);
}
