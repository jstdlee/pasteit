#include "app/execution_feedback.hpp"
#include "util/path_utf8.hpp"
namespace pastit {
namespace {std::string key(std::string_view request,std::string_view action){return std::string{request}+"\n"+std::string{action};}}
void ExecutionFeedback::start(std::string request,std::string action){auto map_key=key(request,action);entries_[map_key]={.request_id=std::move(request),.action_id=std::move(action),.status=ExecutionStatus::Executing};}
bool ExecutionFeedback::finish(const ExecutionResult& result){auto it=entries_.find(key(result.request_id,result.action_id));if(it==entries_.end())return false;it->second.status=result.status;it->second.result=result;return true;}
std::optional<ExecutionFeedbackEntry> ExecutionFeedback::find(std::string_view request,std::string_view action)const{auto it=entries_.find(key(request,action));return it==entries_.end()?std::nullopt:std::optional<ExecutionFeedbackEntry>{it->second};}
bool run_feedback_command(FeedbackCommand command,const ExecutionResult& result,std::size_t index,PlatformServices& platform){
 const auto& paths=result.output_paths;if(index>=paths.size())return false;const auto& path=paths[index];
 switch(command){case FeedbackCommand::CopyOutputPath:return platform.copy_text(path_to_utf8_string(path));case FeedbackCommand::OpenOutput:return platform.open_path(path);case FeedbackCommand::OpenContainingDirectory:return platform.open_path(path.parent_path());}return false;
}
}
