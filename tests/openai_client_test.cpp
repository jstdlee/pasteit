#include "ai/openai_compatible_client.hpp"
#include "util/json.hpp"

#include <cassert>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {
class CaptureTransport final:public pasteit::HttpTransport{
public: pasteit::HttpResponse response;pasteit::HttpRequest seen;
pasteit::HttpResponse post_json(const pasteit::HttpRequest& request)override{seen=request;return response;}
};
// Rejects temperature like a diffusion LLM server does.
class NoTemperatureTransport final:public pasteit::HttpTransport{
public: int calls=0;std::string last_body;
pasteit::HttpResponse post_json(const pasteit::HttpRequest& request)override{
    ++calls;last_body=request.body;
    if(request.body.find("\"temperature\"")!=std::string::npos)
        return {.status=400,.body=R"({"error":{"message":"The temperature ... sampling parameters are not yet supported with diffusion models."}})"};
    return {.status=200,.body=R"({"choices":[{"message":{"content":"ok"}}]})"};
}
};
// Rejects unknown members like a strict OpenAI-compatible gateway.
class NoHintTransport final:public pasteit::HttpTransport{
public: int calls=0;std::string last_body;
pasteit::HttpResponse post_json(const pasteit::HttpRequest& request)override{
    ++calls;last_body=request.body;
    if(request.body.find("chat_template_kwargs")!=std::string::npos)
        return {.status=400,.body=R"({"error":{"message":"Unrecognized request argument supplied: chat_template_kwargs"}})"};
    return {.status=200,.body=R"({"choices":[{"message":{"content":"ok"}}]})"};
}
};
}
// Replies with server-sent events, delivered in awkward byte-sized pieces.
class StreamTransport final:public pasteit::HttpTransport{
public: std::string events;int status=200;pasteit::HttpRequest seen;std::size_t chunk=3;
pasteit::HttpResponse post_json(const pasteit::HttpRequest& request)override{seen=request;return {.status=status,.body=events};}
pasteit::HttpResponse post_stream(const pasteit::HttpRequest& request,const pasteit::HttpChunkSink& on_data)override{
    seen=request;
    for(std::size_t at=0;at<events.size();at+=chunk)if(!on_data(std::string_view(events).substr(at,chunk)))break;
    return {.status=status,.body=events};
}
};
void stream_tests(){
    using namespace pasteit;
    {
        ChatStreamParser parser;
        parser.feed("data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"hmm\"}}]}\r\n\r\n: keep-alive\n");
        parser.feed("data: {\"choices\":[{\"delta\":{\"content\":\"Hel\"}}]}\n\ndata: {\"choi");
        assert(parser.content()=="Hel"&&parser.reasoning_chars()==3&&!parser.done());
        parser.feed("ces\":[{\"delta\":{\"content\":\"lo \u4f60\"}}]}\n\ndata: [DONE]\n\n");
        assert(parser.content()=="Hello \xE4\xBD\xA0"&&parser.done()&&parser.saw_events());
        ChatStreamParser failing;
        failing.feed("data: {\"error\":{\"message\":\"quota exceeded\"}}\n");
        assert(failing.error()=="quota exceeded");
    }
    assert(visible_stream_text("<think>still going").empty());
    assert(visible_stream_text("<think>a</think>\n\nAnswer")=="Answer");
    assert(visible_stream_text("Plain")=="Plain");

    auto transport=std::make_shared<StreamTransport>();
    OpenAiCompatibleClient client(transport);
    TextGenerationRequest request{.request_id="s",.endpoint="http://127.0.0.1:18999/v1",.api_key="",.model_id="m",
                                  .system_message="sys",.user_message="hi"};
    transport->events=
        "data: {\"choices\":[{\"delta\":{\"content\":\"<think>plan\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"</think>\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"One \"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"two\"}}]}\n\ndata: [DONE]\n\n";
    std::vector<std::string> seen;
    const auto streamed=client.generate(request,[&](const TextStreamUpdate& update){if(seen.empty()||seen.back()!=update.text)seen.push_back(update.text);});
    assert(streamed.ok&&streamed.content=="One two");
    assert(transport->seen.body.find("\"stream\":true")!=std::string::npos);
    assert(transport->seen.headers.at("Accept").find("text/event-stream")!=std::string::npos);
    // The think block never shows; the answer grows word by word.
    assert(std::find(seen.begin(),seen.end(),"One ")!=seen.end());
    assert(seen.back()=="One two");
    assert(std::none_of(seen.begin(),seen.end(),[](const std::string& text){return text.find("plan")!=std::string::npos;}));

    // A server that ignores "stream" and answers with plain JSON still works.
    transport->events=R"({"choices":[{"message":{"content":"whole answer"}}]})";
    const auto plain=client.generate(request,[](const TextStreamUpdate&){});
    assert(plain.ok&&plain.content=="whole answer");
    // An error event inside the stream fails the request with its message.
    transport->events="data: {\"error\":{\"message\":\"overloaded\"}}\n\n";
    const auto failed=client.generate(request,[](const TextStreamUpdate&){});
    assert(!failed.ok&&failed.error=="overloaded");
    // HTTP errors keep the provider's message.
    transport->status=401;transport->events=R"({"error":{"message":"bad key"}})";
    const auto denied=client.generate(request,[](const TextStreamUpdate&){});
    assert(!denied.ok&&denied.error=="bad key");
    // Without a sink the request is not streamed.
    transport->status=200;transport->events=R"({"choices":[{"message":{"content":"x"}}]})";
    assert(client.generate(request).ok);
    assert(transport->seen.body.find("\"stream\":false")!=std::string::npos);
}
int main(){
    stream_tests();
    auto transport=std::make_shared<CaptureTransport>();
    transport->response={.status=200,.body=R"({"choices":[{"message":{"content":"你好"}}]})"};
    pasteit::OpenAiCompatibleClient client(transport);
    pasteit::TextGenerationRequest request{.request_id="r1",.endpoint="http://localhost:1234/",.api_key="key",.model_id="m",
        .system_message="translate",.user_message="hello",.temperature=0.25};
    const auto result=client.generate(request);
    assert(result.ok&&result.content=="你好");
    assert(transport->seen.url=="http://localhost:1234/v1/chat/completions");
    assert(transport->seen.headers.at("Authorization")=="Bearer key");
    assert(transport->seen.body.find("\"stream\":false")!=std::string::npos);
    assert(transport->seen.body.find("\"model\":\"m\"")!=std::string::npos);
    assert(pasteit::normalize_chat_completions_endpoint("http://x/v1/chat/completions")=="http://x/v1/chat/completions");
    assert(pasteit::normalize_chat_completions_endpoint("https://x/v1")=="https://x/v1/chat/completions");
    assert(pasteit::normalize_chat_completions_endpoint("https://x/v1/")=="https://x/v1/chat/completions");
    assert(pasteit::normalize_chat_completions_endpoint("https://x")=="https://x/v1/chat/completions");
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
    pasteit::OpenAiCompatibleClient diffusion(strict);
    pasteit::TextGenerationRequest diffusion_request{.request_id="d",.endpoint="http://127.0.0.1:18999",.api_key="",.model_id="dgemma",
                                                   .system_message="s",.user_message="u",.temperature=0.2};
    const auto first=diffusion.generate(diffusion_request);
    assert(first.ok&&first.content=="ok"&&strict->calls==2);
    assert(diffusion.generate(diffusion_request).ok&&strict->calls==3);
    assert(strict->last_body.find("temperature")==std::string::npos);

    // Thinking is off by default: the body asks reasoning models to skip it.
    assert(!pasteit::TextGenerationRequest{}.thinking);
    request.endpoint="http://localhost:1234/";
    request.thinking=false;
    transport->response={.status=200,.body=R"({"choices":[{"message":{"content":"<think>\nhmm\n</think>\n\nAnswer"}}]})"};
    const auto fast=client.generate(request);
    assert(fast.ok&&fast.content=="Answer");
    assert(transport->seen.body.find(R"("chat_template_kwargs":{"enable_thinking":false})")!=std::string::npos);
    assert(transport->seen.body.find(R"("reasoning_effort":"none")")!=std::string::npos);
    assert(transport->seen.body.find(R"("thinking":{"type":"disabled"})")!=std::string::npos);
    assert(pasteit::parse_json(transport->seen.body).has_value());
    // Thinking on: no hints are sent.
    request.thinking=true;
    assert(client.generate(request).ok);
    assert(transport->seen.body.find("enable_thinking")==std::string::npos);
    assert(transport->seen.body.find("reasoning_effort")==std::string::npos);
    assert(transport->seen.body.find("\"thinking\"")==std::string::npos);
    // OpenAI proper never gets the extra members; everyone else gets all three.
    request.thinking=false;
    request.endpoint="https://api.openai.com/v1";
    assert(client.generate(request).ok);
    assert(transport->seen.body.find("chat_template_kwargs")==std::string::npos);
    assert(transport->seen.body.find("reasoning_effort")==std::string::npos);
    request.endpoint="http://127.0.0.1:11434/v1";
    assert(client.generate(request).ok);
    assert(transport->seen.body.find(R"("reasoning_effort":"none")")!=std::string::npos);
    assert(transport->seen.body.find("enable_thinking")!=std::string::npos);
    assert(pasteit::parse_json(transport->seen.body).has_value());

    // A server that rejects the hints gets one retry without them, remembered per endpoint.
    auto no_hints=std::make_shared<NoHintTransport>();
    pasteit::OpenAiCompatibleClient hint_client(no_hints);
    pasteit::TextGenerationRequest hint_request{.request_id="h",.endpoint="http://127.0.0.1:18998",.api_key="",.model_id="m",
                                              .system_message="s",.user_message="u",.temperature=0.2};
    const auto retried=hint_client.generate(hint_request);
    assert(retried.ok&&no_hints->calls==2);
    assert(no_hints->last_body.find("chat_template_kwargs")==std::string::npos);
    assert(no_hints->last_body.find("\"temperature\"")!=std::string::npos);
    assert(hint_client.generate(hint_request).ok&&no_hints->calls==3);

    // <think> stripping.
    assert(pasteit::strip_thinking_block("plain")=="plain");
    assert(pasteit::strip_thinking_block("  <think>a</think>b")=="b");
    assert(pasteit::strip_thinking_block("reasoning only closed</think>\nfinal")=="final");
    assert(pasteit::strip_thinking_block("Use <think> tags like <think>x</think>")=="Use <think> tags like <think>x</think>");
    assert(pasteit::strip_thinking_block("<think>unterminated")=="<think>unterminated");
}
