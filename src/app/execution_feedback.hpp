#pragma once
#include "core/protocol.hpp"
#include "platform/platform_services.hpp"
#include <map>
#include <optional>
#include <string>
namespace pastit {
enum class FeedbackCommand{CopyOutputPath,OpenOutput,OpenContainingDirectory};
struct ExecutionFeedbackEntry{std::string request_id,action_id;ExecutionStatus status=ExecutionStatus::Prepared;ExecutionResult result;};
class ExecutionFeedback{
public:void start(std::string request_id,std::string action_id);bool finish(const ExecutionResult& result);std::optional<ExecutionFeedbackEntry> find(std::string_view request_id,std::string_view action_id)const;
private:std::map<std::string,ExecutionFeedbackEntry> entries_;
};
bool run_feedback_command(FeedbackCommand command,const ExecutionResult& result,std::size_t path_index,PlatformServices& platform);
}
