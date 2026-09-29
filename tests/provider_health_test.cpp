#include "net/provider_health.hpp"

#include <cassert>
#include <memory>
#include <string>

namespace {

// Records the request and answers with a canned response.
class FakeTransport final : public pasteit::HttpTransport {
public:
    pasteit::HttpResponse response;
    std::string last_url;
    std::string last_auth;
    pasteit::HttpResponse post_json(const pasteit::HttpRequest& request) override { return get_json(request); }
    pasteit::HttpResponse get_json(const pasteit::HttpRequest& request) override {
        last_url = request.url;
        const auto auth = request.headers.find("Authorization");
        last_auth = auth == request.headers.end() ? "" : auth->second;
        return response;
    }
};

}  // namespace

int main() {
    using namespace pasteit;
    assert(jev_health_url("http://127.0.0.1:8011") == "http://127.0.0.1:8011/health");
    assert(jev_health_url("http://127.0.0.1:8011/v1/systemone") == "http://127.0.0.1:8011/health");
    assert(jev_health_url("https://jev.example/api/v1") == "https://jev.example/health");
    assert(jev_health_url("localhost:8011") == "http://localhost:8011/health");

    auto transport = std::make_shared<FakeTransport>();
    ProviderSettings jev{.endpoint = "http://127.0.0.1:8011/v1/systemone", .model_id = "typed-decisions", .api_key = "k"};

    transport->response = {.status = 200, .body = "{}"};
    auto health = check_jev_health(jev, transport);
    assert(health.state == ProviderHealthState::Ok && health.checked_at_ms > 0);
    assert(transport->last_url == "http://127.0.0.1:8011/health" && transport->last_auth == "Bearer k");

    transport->response = {.status = 404};  // server up, no /health route
    assert(check_jev_health(jev, transport).state == ProviderHealthState::Ok);
    transport->response = {.status = 503};
    assert(check_jev_health(jev, transport).state == ProviderHealthState::Down);
    transport->response = {.transport_error = "connection refused"};
    health = check_jev_health(jev, transport);
    assert(health.state == ProviderHealthState::Down && health.detail == "connection refused");
    assert(check_jev_health(ProviderSettings{}, transport).state == ProviderHealthState::Unconfigured);

    ProviderSettings llm{.endpoint = "https://llm.example/v1", .model_id = "m", .api_key = "secret"};
    transport->response = {.status = 200, .body = R"({"data":[{"id":"a"},{"id":"b"}]})"};
    health = check_llm_health(llm, transport);
    assert(health.state == ProviderHealthState::Ok && health.detail.find("2 models") == 0);
    assert(transport->last_url.find("/models") != std::string::npos && transport->last_auth == "Bearer secret");
    transport->response = {.status = 401, .body = R"({"error":{"message":"bad key"}})"};
    assert(check_llm_health(llm, transport).state == ProviderHealthState::Down);
    assert(check_llm_health(ProviderSettings{.endpoint = "https://x"}, transport).state == ProviderHealthState::Unconfigured);
}
