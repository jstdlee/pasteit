#include "ai/openai_compatible_client.hpp"

#include <cassert>
#include <memory>

namespace {
class CaptureTransport final:public pastit::HttpTransport{
public: pastit::HttpResponse response;pastit::HttpRequest seen;
pastit::HttpResponse post_json(const pastit::HttpRequest& request)override{seen=request;return response;}
};
// Rejects temperature like a diffusion LLM server does.
class NoTemperatureTransport final:public pastit::HttpTransport{
public: int calls=0;std::string last_body;
pastit::HttpResponse post_json(const pastit::HttpRequest& request)override{
    ++calls;last_body=request.body;
    if(request.body.find("\"temperature\"")!=std::string::npos)
        return {.status=400,.body=R"({"error":{"message":"The temperature ... sampling parameters are not yet supported with diffusion models."}})"};
    return {.status=200,.body=R"({"choices":[{"message":{"content":"ok"}}]})"};
}
};
}
int main(){
    auto transport=std::make_shared<CaptureTransport>();
    transport->response={.status=200,.body=R"({"choices":[{"message":{"content":"你好"}}]})"};
    pastit::OpenAiCompatibleClient client(transport);
    pastit::TextGenerationRequest request{.request_id="r1",.endpoint="http://localhost:1234/",.api_key="key",.model_id="m",
        .system_message="translate",.user_message="hello",.temperature=0.25};
    const auto result=client.generate(request);
    assert(result.ok&&result.content=="你好");
    assert(transport->seen.url=="http://localhost:1234/v1/chat/completions");
    assert(transport->seen.headers.at("Authorization")=="Bearer key");
    assert(transport->seen.body.find("\"stream\":false")!=std::string::npos);
    assert(transport->seen.body.find("\"model\":\"m\"")!=std::string::npos);
    assert(pastit::normalize_chat_completions_endpoint("http://x/v1/chat/completions")=="http://x/v1/chat/completions");
    assert(pastit::normalize_chat_completions_endpoint("https://x/v1")=="https://x/v1/chat/completions");
    assert(pastit::normalize_chat_completions_endpoint("https://x/v1/")=="https://x/v1/chat/completions");
    assert(pastit::normalize_chat_completions_endpoint("https://x")=="https://x/v1/chat/completions");
    request.endpoint="https://opencode.ai/zen/go/v1";
    request.request_id="go-request-42";
    transport->response={.status=200,.body=R"({"choices":[{"message":{"content":"OK"}}]})"};
    assert(client.generate(request).ok);
    assert(transport->seen.url=="https://opencode.ai/zen/go/v1/chat/completions");
    assert(transport->seen.headers.at("x-opencode-session")=="go-request-42");
    request.endpoint="http://localhost:1234/";
    transport->response={.status=500,.body=R"({"error":{"message":"model unavailable"}})"};
    assert(client.generate(request).error=="model unavailable");
    transport->response={.status=200,.body="broken"};assert(!client.generate(request).ok);
    transport->response={.status=200,.body=R"({"choices":[]})"};assert(!client.generate(request).ok);
    // A server that rejects temperature gets one retry without it, and the
    // endpoint is remembered so later requests skip the failing attempt.
    auto strict=std::make_shared<NoTemperatureTransport>();
    pastit::OpenAiCompatibleClient diffusion(strict);
    pastit::TextGenerationRequest diffusion_request{.request_id="d",.endpoint="http://127.0.0.1:18999",.api_key="",.model_id="dgemma",
                                                   .system_message="s",.user_message="u",.temperature=0.2};
    const auto first=diffusion.generate(diffusion_request);
    assert(first.ok&&first.content=="ok"&&strict->calls==2);
    assert(diffusion.generate(diffusion_request).ok&&strict->calls==3);
    assert(strict->last_body.find("temperature")==std::string::npos);
}
