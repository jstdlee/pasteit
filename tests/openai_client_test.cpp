#include "ai/openai_compatible_client.hpp"

#include <cassert>
#include <memory>

namespace {
class CaptureTransport final:public pastit::HttpTransport{
public: pastit::HttpResponse response;pastit::HttpRequest seen;
pastit::HttpResponse post_json(const pastit::HttpRequest& request)override{seen=request;return response;}
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
}
