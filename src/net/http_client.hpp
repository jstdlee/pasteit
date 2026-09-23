#pragma once
#include <chrono>
#include <map>
#include <memory>
#include <string>
namespace pastit {
struct HttpRequest {
    std::string url;
    std::string body;
    std::map<std::string,std::string> headers;
    std::chrono::milliseconds timeout{1500};
};
struct HttpResponse { int status=0; std::string body; std::string transport_error; };
class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    virtual HttpResponse post_json(const HttpRequest& request) = 0;
    virtual HttpResponse get_json(const HttpRequest&) {
        return {.status = 0, .body = {},
                .transport_error = "HTTP GET is not supported by this transport"};
    }
};
std::shared_ptr<HttpTransport> make_default_http_transport();
}
