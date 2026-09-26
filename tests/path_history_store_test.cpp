#include "storage/path_history.hpp"
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
 // Use counts rank a folder and survive a save/load round trip.
 std::filesystem::create_directories(root/"other");
 history.observe_path(root/"other",pastit::PathKind::Directory,"bash",50);
 assert(history.recent(10).front().path==root/"other");
 history.record_use(root/"folder",45);history.record_use(root/"folder",46);
 assert(history.recent(10).front().path==root/"folder"&&history.recent(10).front().use_count==2);
 assert(store.save(history,error));
 const auto reloaded=store.load();const auto top=reloaded.history.recent(10).front();
 assert(top.path==root/"folder"&&top.use_count==2&&top.last_used_ms==46&&top.use_weight>1.9);
 std::filesystem::remove_all(root);
}
