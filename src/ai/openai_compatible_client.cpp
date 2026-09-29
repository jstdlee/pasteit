#include "ai/openai_compatible_client.hpp"
#include "util/json.hpp"
#include <mutex>
#include <set>
#include <sstream>
namespace pasteit {
std::string normalize_chat_completions_endpoint(std::string endpoint){
    while(!endpoint.empty()&&endpoint.back()=='/')endpoint.pop_back();
    constexpr std::string_view suffix="/v1/chat/completions";
    if(endpoint.size()>=suffix.size()&&endpoint.substr(endpoint.size()-suffix.size())==suffix)return endpoint;
    constexpr std::string_view version="/v1";
    if(endpoint.size()>=version.size()&&endpoint.substr(endpoint.size()-version.size())==version)
        return endpoint+"/chat/completions";
    return endpoint+std::string{suffix};
}
std::string strip_thinking_block(std::string content){
    constexpr std::string_view open_tag="<think>",close_tag="</think>";
    const auto first=content.find_first_not_of(" \t\r\n");
    const auto open=content.find(open_tag);
    const auto close=content.find(close_tag);
    if(close==std::string::npos)return content;
    // Either the reply starts with <think>, or the chat template already
    // emitted <think> so only the closing tag appears in the content.
    if((open!=std::string::npos&&open==first&&close>open)||open==std::string::npos){
        content.erase(0,close+close_tag.size());
        content.erase(0,content.find_first_not_of(" \t\r\n"));
    }
    return content;
}
std::string disable_thinking_fields(std::string_view url){
    // OpenAI proper rejects unknown members and reasoning_effort on
    // non-reasoning models, so nothing is added there.
    if(url.find("://api.openai.com")!=std::string_view::npos)return {};
    // Each server family reads a different switch, and hosted gateways
    // (OpenRouter-style, opencode zen) ignore chat_template_kwargs, so all
    // three are sent; a server that rejects them gets a retry without.
    //   chat_template_kwargs: vLLM, SGLang, llama.cpp -> Qwen3 / GLM templates
    //   reasoning_effort "none": Ollama, OpenAI-style gateways
    //   thinking.type "disabled": DeepSeek / Anthropic-style gateways
    return ",\"chat_template_kwargs\":{\"enable_thinking\":false}"
           ",\"reasoning_effort\":\"none\""
           ",\"thinking\":{\"type\":\"disabled\"}";
}
void ChatStreamParser::feed(std::string_view bytes){
    pending_.append(bytes);
    std::size_t start=0;
    for(;;){
        const auto end=pending_.find('\n',start);
        if(end==std::string::npos)break;
        std::string_view text(pending_.data()+start,end-start);
        if(!text.empty()&&text.back()=='\r')text.remove_suffix(1);
        line(text);
        start=end+1;
    }
    pending_.erase(0,start);
}
void ChatStreamParser::line(std::string_view text){
    if(!text.starts_with("data:"))return;  // comments, event:, id:, blank separators
    text.remove_prefix(5);
    while(!text.empty()&&text.front()==' ')text.remove_prefix(1);
    saw_events_=true;
    if(text=="[DONE]"){done_=true;return;}
    const auto root=parse_json(std::string(text));
    if(!root)return;
    if(const auto* error=root->get("error")){
        const auto* message=error->get("message");
        error_=message&&message->string()?*message->string():"LLM stream reported an error";
        return;
    }
    const auto* choices=root->get("choices");
    if(!choices||!choices->array()||choices->array()->empty())return;
    const auto* delta=choices->array()->front().get("delta");
    if(!delta)return;
    if(const auto* piece=delta->get("content");piece&&piece->string())content_+=*piece->string();
    for(const char* name:{"reasoning_content","reasoning"}){
        if(const auto* piece=delta->get(name);piece&&piece->string())reasoning_chars_+=piece->string()->size();
    }
}
std::string visible_stream_text(const std::string& content){
    const auto first=content.find_first_not_of(" \t\r\n");
    const bool opens=first!=std::string::npos&&content.compare(first,7,"<think>")==0;
    const bool closed=content.find("</think>")!=std::string::npos;
    if(opens&&!closed)return {};
    return strip_thinking_block(content);
}
TextGenerationResult OpenAiCompatibleClient::generate(const TextGenerationRequest& request)const{
    return generate(request,TextStreamSink{});
}
TextGenerationResult OpenAiCompatibleClient::generate(const TextGenerationRequest& request, const TextStreamSink& on_update)const{
    TextGenerationResult result{.request_id=request.request_id};
    if(request.endpoint.find_first_not_of(" \t")==std::string::npos){result.error="LLM endpoint is not configured (Settings > General LLM)";return result;}
    // Some servers (e.g. diffusion LLMs) reject sampling parameters; remember
    // such endpoints and send them requests without temperature.
    static std::mutex no_temperature_mutex;
    static std::set<std::string> no_temperature_endpoints;
    // Endpoints that answered 400 to the no-thinking hints get them dropped.
    static std::set<std::string> no_thinking_hint_endpoints;
    const auto url=normalize_chat_completions_endpoint(request.endpoint);
    bool with_temperature=true;
    std::string thinking_fields=request.thinking?std::string{}:disable_thinking_fields(url);
    {
        std::lock_guard lock(no_temperature_mutex);with_temperature=!no_temperature_endpoints.contains(url);
        if(no_thinking_hint_endpoints.contains(url))thinking_fields.clear();
    }
    const bool streaming=static_cast<bool>(on_update);
    const auto make_body=[&](bool temperature){
        std::ostringstream body;body<<"{\"model\":"<<json_quote(request.model_id)<<",\"messages\":["
            <<"{\"role\":\"system\",\"content\":"<<json_quote(request.system_message)<<"},"
            <<"{\"role\":\"user\",\"content\":"<<json_quote(request.user_message)<<"}],";
        if(temperature)body<<"\"temperature\":"<<request.temperature<<",";
        body<<"\"stream\":"<<(streaming?"true":"false")<<thinking_fields<<"}";
        return body.str();
    };
    HttpRequest wire{.url=url,.body=make_body(with_temperature),.timeout=request.timeout};
    if(!request.api_key.empty())wire.headers["Authorization"]="Bearer "+request.api_key;
    if(streaming)wire.headers["Accept"]="text/event-stream, application/json";
    // OpenCode Go uses the session header to route requests to the selected
    // Go backend. Keep it tied to the request so concurrent generations do
    // not accidentally share a provider session.
    if(wire.url.find("/zen/go/")!=std::string::npos)
        wire.headers["x-opencode-session"]=request.request_id;
    // Each attempt gets a fresh parser; a rejected attempt (HTTP 400) sends no
    // events, so nothing reaches on_update before the retry.
    ChatStreamParser parser;
    const auto send=[&]{
        if(!streaming)return transport_->post_json(wire);
        parser=ChatStreamParser{};
        std::string shown;
        return transport_->post_stream(wire,[&](std::string_view bytes){
            parser.feed(bytes);
            auto text=visible_stream_text(parser.content());
            if(text!=shown||parser.content().empty()){
                shown=text;
                on_update(TextStreamUpdate{.text=std::move(text),.reasoning_chars=parser.reasoning_chars()});
            }
            return true;
        });
    };
    auto response=send();
    if(with_temperature&&response.status==400&&response.body.find("temperature")!=std::string::npos){
        with_temperature=false;
        wire.body=make_body(false);
        response=send();
        if(response.status>=200&&response.status<300){std::lock_guard lock(no_temperature_mutex);no_temperature_endpoints.insert(url);}
    }
    // Strict servers reject unknown members with 400 and varied wording; one
    // retry without the hints keeps them working.
    if(!thinking_fields.empty()&&response.status==400){
        thinking_fields.clear();
        wire.body=make_body(with_temperature);
        response=send();
        if(response.status>=200&&response.status<300){std::lock_guard lock(no_temperature_mutex);no_thinking_hint_endpoints.insert(url);}
    }
    result.http_status=response.status;
    const auto root=parse_json(response.body);
    if(!response.transport_error.empty()){result.error="LLM request failed: "+response.transport_error;return result;}
    if(response.status<200||response.status>=300){
        if(root){const auto* error=root->get("error");const auto* message=error?error->get("message"):nullptr;if(message&&message->string())result.error=*message->string();}
        if(result.error.empty()){result.error="LLM request failed (HTTP "+std::to_string(response.status)+")";}return result;
    }
    if(streaming&&parser.saw_events()){
        if(!parser.error().empty()){result.error=parser.error();return result;}
        result.content=strip_thinking_block(parser.content());result.ok=true;return result;
    }
    // Not an event stream: the server ignored "stream" (or streaming was off).
    if(!root){result.error="LLM response was not valid JSON";return result;}
    const auto* choices=root->get("choices");
    if(!choices||!choices->array()||choices->array()->empty()){result.error="LLM response did not include choices[0].message.content";return result;}
    const auto* message=choices->array()->front().get("message");const auto* content=message?message->get("content"):nullptr;
    if(!content||!content->string()){result.error="LLM response did not include choices[0].message.content";return result;}
    result.content=strip_thinking_block(*content->string());result.ok=true;return result;
}
}
