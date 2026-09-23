#include "app/ai_result_state.hpp"
namespace pastit {
void AiResultState::start(std::string action_id,std::string source_text,const TextGenerationRequest& request){
    active_={.request_id=request.request_id,.action_id=std::move(action_id),.source_text=std::move(source_text),.status=AiResultStatus::Pending,.frozen_request=request};
}
bool AiResultState::complete(std::string_view id,std::string content){if(id!=active_.request_id)return false;active_.editable_text=std::move(content);active_.error.clear();active_.status=AiResultStatus::Completed;return true;}
bool AiResultState::fail(std::string_view id,std::string error){if(id!=active_.request_id)return false;active_.error=std::move(error);active_.status=AiResultStatus::Failed;return true;}
void AiResultState::edit(std::string text){active_.editable_text=std::move(text);}
std::optional<TextGenerationRequest> AiResultState::retry_request()const{if(active_.request_id.empty())return std::nullopt;return active_.frozen_request;}
}
