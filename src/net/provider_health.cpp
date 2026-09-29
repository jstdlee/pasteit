#include "net/provider_health.hpp"

#include "ai/model_catalog.hpp"

#include <chrono>

namespace pasteit {
namespace {

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string elapsed_text(std::chrono::steady_clock::time_point started) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    return std::to_string(ms) + " ms";
}

bool blank(const std::string& value) {
    return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

}  // namespace

std::string jev_health_url(const std::string& endpoint) {
    // Keep scheme://host[:port]; drop any path such as /v1/systemone.
    const auto scheme = endpoint.find("://");
    const auto host_start = scheme == std::string::npos ? 0 : scheme + 3;
    const auto path = endpoint.find('/', host_start);
    const auto base = path == std::string::npos ? endpoint : endpoint.substr(0, path);
    return (scheme == std::string::npos ? "http://" + base : base) + "/health";
}

ProviderHealth check_jev_health(const ProviderSettings& provider, const std::shared_ptr<HttpTransport>& transport) {
    ProviderHealth health;
    if (blank(provider.endpoint)) {
        health.detail = "not configured";
        return health;
    }
    const auto started = std::chrono::steady_clock::now();
    HttpRequest request{.url = jev_health_url(provider.endpoint), .timeout = std::chrono::milliseconds{5000}};
    if (!provider.api_key.empty()) request.headers["Authorization"] = "Bearer " + provider.api_key;
    const auto response = transport->get_json(request);
    health.checked_at_ms = now_ms();
    if (!response.transport_error.empty()) {
        health.state = ProviderHealthState::Down;
        health.detail = response.transport_error;
    } else if (response.status <= 0 || response.status >= 500) {
        health.state = ProviderHealthState::Down;
        health.detail = "HTTP " + std::to_string(response.status);
    } else {
        health.state = ProviderHealthState::Ok;
        health.detail = "HTTP " + std::to_string(response.status) + " in " + elapsed_text(started);
    }
    return health;
}

ProviderHealth check_llm_health(const ProviderSettings& provider, const std::shared_ptr<HttpTransport>& transport) {
    ProviderHealth health;
    if (blank(provider.endpoint) || blank(provider.model_id)) {
        health.detail = "not configured";
        return health;
    }
    const auto started = std::chrono::steady_clock::now();
    const auto result = OpenAiCompatibleModelClient{transport}.list_models({.endpoint = provider.endpoint, .api_key = provider.api_key});
    health.checked_at_ms = now_ms();
    if (result.ok) {
        health.state = ProviderHealthState::Ok;
        health.detail = std::to_string(result.model_ids.size()) + " models in " + elapsed_text(started);
    } else {
        health.state = ProviderHealthState::Down;
        health.detail = result.error.empty() ? "HTTP " + std::to_string(result.http_status) : result.error;
    }
    return health;
}

}  // namespace pasteit
