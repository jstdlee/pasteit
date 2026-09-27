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
TextGenerationResult OpenAiCompatibleClient::generate(const TextGenerationRequest& request)const{
    TextGenerationResult result{.request_id=request.request_id};
    if(request.endpoint.find_first_not_of(" \t")==std::string::npos){result.error="LLM endpoint is not configured (Settings > General LLM)";return result;}
    // Some servers (e.g. diffusion LLMs) reject sampling parameters; remember
    // such endpoints and send them requests without temperature.
    static std::mutex no_temperature_mutex;
    static std::set<std::string> no_temperature_endpoints;
    const auto url=normalize_chat_completions_endpoint(request.endpoint);
    bool with_temperature=true;
    {std::lock_guard lock(no_temperature_mutex);with_temperature=!no_temperature_endpoints.contains(url);}
    const auto make_body=[&](bool temperature){
        std::ostringstream body;body<<"{\"model\":"<<json_quote(request.model_id)<<",\"messages\":["
            <<"{\"role\":\"system\",\"content\":"<<json_quote(request.system_message)<<"},"
            <<"{\"role\":\"user\",\"content\":"<<json_quote(request.user_message)<<"}],";
        if(temperature)body<<"\"temperature\":"<<request.temperature<<",";
        body<<"\"stream\":false}";
        return body.str();
    };
    HttpRequest wire{.url=url,.body=make_body(with_temperature),.timeout=request.timeout};
    if(!request.api_key.empty())wire.headers["Authorization"]="Bearer "+request.api_key;
    // OpenCode Go uses the session header to route requests to the selected
    // Go backend. Keep it tied to the request so concurrent generations do
    // not accidentally share a provider session.
    if(wire.url.find("/zen/go/")!=std::string::npos)
        wire.headers["x-opencode-session"]=request.request_id;
    auto response=transport_->post_json(wire);
    if(with_temperature&&response.status==400&&response.body.find("temperature")!=std::string::npos){
        wire.body=make_body(false);
        response=transport_->post_json(wire);
        if(response.status>=200&&response.status<300){std::lock_guard lock(no_temperature_mutex);no_temperature_endpoints.insert(url);}
    }
    result.http_status=response.status;
    const auto root=parse_json(response.body);
    if(!response.transport_error.empty()){result.error="LLM request failed: "+response.transport_error;return result;}
    if(response.status<200||response.status>=300){
        if(root){const auto* error=root->get("error");const auto* message=error?error->get("message"):nullptr;if(message&&message->string())result.error=*message->string();}
        if(result.error.empty()){result.error="LLM request failed (HTTP "+std::to_string(response.status)+")";}return result;
    }
    if(!root){result.error="LLM response was not valid JSON";return result;}
    const auto* choices=root->get("choices");
    if(!choices||!choices->array()||choices->array()->empty()){result.error="LLM response did not include choices[0].message.content";return result;}
    const auto* message=choices->array()->front().get("message");const auto* content=message?message->get("content"):nullptr;
    if(!content||!content->string()){result.error="LLM response did not include choices[0].message.content";return result;}
    result.content=*content->string();result.ok=true;return result;
}
}
