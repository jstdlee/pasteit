#pragma once

#include "config/app_settings.hpp"
#include "net/http_client.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace pasteit {

// Reachability of the two providers, shown as dots beside the Settings tab.
enum class ProviderHealthState { Unconfigured, Checking, Ok, Down };

struct ProviderHealth {
    ProviderHealthState state = ProviderHealthState::Unconfigured;
    std::string detail;             // e.g. "HTTP 200 in 13 ms" or the error
    std::int64_t checked_at_ms = 0;  // wall clock of the last finished check
};

// Jev: GET {scheme://host:port}/health; any HTTP answer below 500 means the
// server is up (older deployments without /health answer 404). No inference.
std::string jev_health_url(const std::string& endpoint);
ProviderHealth check_jev_health(const ProviderSettings& provider,
                                const std::shared_ptr<HttpTransport>& transport = make_default_http_transport());

// General LLM: GET /v1/models with the API key; 2xx means reachable and the
// key is accepted. Nothing is generated, so the check costs no tokens.
ProviderHealth check_llm_health(const ProviderSettings& provider,
                                const std::shared_ptr<HttpTransport>& transport = make_default_http_transport());

inline constexpr std::chrono::seconds kProviderHealthInterval{60};

}  // namespace pasteit
