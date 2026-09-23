#pragma once
#include "net/http_client.hpp"
#include <chrono>
#include <memory>
#include <string>
namespace pastit {
struct TextGenerationRequest {
    std::string request_id,endpoint,api_key,model_id,system_message,user_message;
    double temperature=0.2;
    std::chrono::milliseconds timeout{30000};
};
struct TextGenerationResult { std::string request_id;bool ok=false;int http_status=0;std::string content;std::string error; };
std::string normalize_chat_completions_endpoint(std::string endpoint);
class OpenAiCompatibleClient {
public: explicit OpenAiCompatibleClient(std::shared_ptr<HttpTransport> transport=make_default_http_transport()):transport_(std::move(transport)){}
TextGenerationResult generate(const TextGenerationRequest& request)const;
private:std::shared_ptr<HttpTransport> transport_;
};
}
