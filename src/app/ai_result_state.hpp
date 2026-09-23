#pragma once
#include "ai/openai_compatible_client.hpp"
#include <optional>
#include <string>
namespace pastit {
enum class AiResultStatus{Idle,Pending,Completed,Failed};
struct AiResultRecord{
    std::string request_id,action_id,source_text,editable_text,error;
    AiResultStatus status=AiResultStatus::Idle;
    TextGenerationRequest frozen_request;
};
class AiResultState{
public:
    void start(std::string action_id,std::string source_text,const TextGenerationRequest& request);
    bool complete(std::string_view request_id,std::string content);
    bool fail(std::string_view request_id,std::string error);
    void edit(std::string text);
    std::optional<TextGenerationRequest> retry_request()const;
    const AiResultRecord& active()const{return active_;}
private:AiResultRecord active_;
};
}
