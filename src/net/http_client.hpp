#pragma once
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
namespace pasteit {
struct HttpRequest {
    std::string url;
    std::string body;
    std::map<std::string,std::string> headers;
    std::chrono::milliseconds timeout{1500};
    bool follow_redirects = false;
    std::size_t max_body_bytes = 0;  // 0 = unlimited; larger bodies are cut
};
// Receives response body bytes as they arrive; return false to cancel.
using HttpChunkSink = std::function<bool(std::string_view)>;
struct HttpResponse { int status=0; std::string body; std::string transport_error; bool truncated=false; std::string content_type; };
class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    virtual HttpResponse post_json(const HttpRequest& request) = 0;
    // Like post_json, but hands the body to on_data while it downloads (for
    // server-sent events). The full body is still returned. The default
    // delivers it in one piece once the request completes.
    virtual HttpResponse post_stream(const HttpRequest& request, const HttpChunkSink& on_data) {
        auto response = post_json(request);
        if (!response.body.empty()) (void)on_data(response.body);
        return response;
    }
    virtual HttpResponse get_json(const HttpRequest&) {
        return {.status = 0, .body = {},
                .transport_error = "HTTP GET is not supported by this transport"};
    }
};
std::shared_ptr<HttpTransport> make_default_http_transport();
}
