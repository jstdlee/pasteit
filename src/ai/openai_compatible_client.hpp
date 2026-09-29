#pragma once
#include "net/http_client.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
namespace pasteit {
struct TextGenerationRequest {
    std::string request_id,endpoint,api_key,model_id,system_message,user_message;
    double temperature=0.2;
    // Off by default: reasoning models are asked to answer without a thinking phase.
    bool thinking=false;
    // Reasoning models on hosted gateways can take well over 30 s for a long
    // clipboard; callers that need a quick answer (provider tests) set less.
    std::chrono::milliseconds timeout{120000};
};
struct TextGenerationResult { std::string request_id;bool ok=false;int http_status=0;std::string content;std::string error; };
std::string normalize_chat_completions_endpoint(std::string endpoint);
// Removes a leading <think>...</think> reasoning block (or reasoning that ends
// in a lone </think>) that some local servers leave in message content.
std::string strip_thinking_block(std::string content);
// JSON members (each with a leading comma) that ask the server to skip the
// reasoning phase; empty for endpoints known to reject unknown parameters.
std::string disable_thinking_fields(std::string_view url);
// Incremental parser for an OpenAI-style "stream":true reply (server-sent
// events): collects choices[0].delta.content across "data:" lines, which may
// arrive split at any byte.
class ChatStreamParser {
public:
    void feed(std::string_view bytes);
    const std::string& content() const { return content_; }
    std::size_t reasoning_chars() const { return reasoning_chars_; }
    bool saw_events() const { return saw_events_; }
    bool done() const { return done_; }
    const std::string& error() const { return error_; }
private:
    void line(std::string_view text);
    std::string pending_, content_, error_;
    std::size_t reasoning_chars_ = 0;
    bool saw_events_ = false, done_ = false;
};
// Text a streaming view should show so far: hides a <think> block that is
// still open and strips a finished one.
std::string visible_stream_text(const std::string& content);

struct TextStreamUpdate {
    std::string text;               // visible answer so far
    std::size_t reasoning_chars = 0;  // reasoning received but not shown
};
using TextStreamSink = std::function<void(const TextStreamUpdate&)>;

class OpenAiCompatibleClient {
public: explicit OpenAiCompatibleClient(std::shared_ptr<HttpTransport> transport=make_default_http_transport()):transport_(std::move(transport)){}
TextGenerationResult generate(const TextGenerationRequest& request)const;
// Streams the answer: on_update runs on the calling thread as text arrives.
// Servers that ignore "stream" still work; their whole answer arrives at once.
TextGenerationResult generate(const TextGenerationRequest& request, const TextStreamSink& on_update)const;
private:std::shared_ptr<HttpTransport> transport_;
};
}
