#include "history/path_history_store.hpp"
#include "util/json.hpp"
#include "util/path_utf8.hpp"
#include "util/replace_file.hpp"
#include <fstream>
#include <sstream>
namespace pasteit {
PathHistoryLoadResult PathHistoryStore::load()const{
 PathHistoryLoadResult result;std::ifstream input(path_);if(!input)return result;std::ostringstream text;text<<input.rdbuf();const auto root=parse_json(text.str());
 if(!root||!root->array()){result.warning="Path history is malformed; an empty history was loaded.";return result;}
 for(const auto& item:*root->array()){
  const auto* p=item.get("path");const auto* k=item.get("kind");const auto* s=item.get("source");const auto* t=item.get("last_seen_ms");
  if(!p||!p->string())continue;if(s&&s->string()&&*s->string()=="proc-fd")continue;const auto kind=k&&k->string()&&*k->string()=="directory"?PathKind::Directory:PathKind::File;
  const auto path=path_from_utf8_string(*p->string());
  result.history.observe_path(path,kind,s&&s->string()?*s->string():"persisted",static_cast<std::int64_t>(t&&t->number()?*t->number():0));
  const auto* w=item.get("use_weight");const auto* n=item.get("use_count");const auto* u=item.get("last_used_ms");
  if(w&&w->number())result.history.restore_use(path,*w->number(),static_cast<std::uint32_t>(n&&n->number()?*n->number():0),static_cast<std::int64_t>(u&&u->number()?*u->number():0));
 }
 return result;
}
bool PathHistoryStore::save(const PathHistory& history,std::string& error)const{
 std::error_code ec;std::filesystem::create_directories(path_.parent_path(),ec);if(ec){error=ec.message();return false;}
 auto tmp=path_;tmp+=".tmp";std::ofstream out(tmp,std::ios::trunc);if(!out){error="Could not open path history.";return false;}
 out<<'[';const auto items=history.recent(limit_);for(std::size_t i=0;i<items.size();++i){const auto& v=items[i];if(i)out<<',';out<<"{\"path\":"<<json_quote(path_to_utf8_string(v.path))<<",\"kind\":"<<json_quote(v.kind==PathKind::Directory?"directory":"file")<<",\"source\":"<<json_quote(v.source)<<",\"last_seen_ms\":"<<v.last_seen_ms;if(v.use_count>0)out<<",\"use_weight\":"<<v.use_weight<<",\"use_count\":"<<v.use_count<<",\"last_used_ms\":"<<v.last_used_ms;out<<'}';}out<<"]\n";
 out.flush();if(!out){error="Could not write path history.";return false;}
 out.close();if(!out){error="Could not close path history.";return false;}
 replace_file(tmp,path_,ec);
 if(ec){std::error_code ignored;std::filesystem::remove(tmp,ignored);error=ec.message();return false;}
 error.clear();return true;
}
}
