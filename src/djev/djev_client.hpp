#pragma once

#include "core/protocol.hpp"
#include "net/http_client.hpp"

#include <chrono>
#include <string>
#include <string_view>
#include <memory>

namespace pastit {

class DjevClient {
public:
    static constexpr std::chrono::milliseconds kDefaultTimeout{30000};

    explicit DjevClient(std::string url = endpoint_from_env(), std::string model = model_from_env(),
                        std::chrono::milliseconds timeout = kDefaultTimeout,
                        std::shared_ptr<HttpTransport> transport = {}, std::string api_key = {});

    DecisionResponse decide(const DecisionRequest& request) const;

    static std::string build_payload(const DecisionRequest& request, const std::string& model);
    static DecisionResponse parse_response(std::string_view body, const DecisionRequest& request);
    static std::string endpoint_from_env();
    static std::string normalize_endpoint(std::string endpoint);
    static std::string model_from_env();

private:
    std::string url_;
    std::string model_;
    std::chrono::milliseconds timeout_;
    std::shared_ptr<HttpTransport> transport_;
    std::string api_key_;
};

}  // namespace pastit
