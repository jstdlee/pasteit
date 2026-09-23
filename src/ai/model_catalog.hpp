#pragma once

#include "net/http_client.hpp"

#include <memory>
#include <string>
#include <vector>

namespace pastit {

struct ModelListRequest {
    std::string endpoint;
    std::string api_key;
};

struct ModelListResult {
    bool ok = false;
    int http_status = 0;
    std::vector<std::string> model_ids;
    std::string error;
};

std::string normalize_models_endpoint(std::string endpoint);

class OpenAiCompatibleModelClient {
public:
    explicit OpenAiCompatibleModelClient(std::shared_ptr<HttpTransport> transport = make_default_http_transport())
        : transport_(std::move(transport)) {}

    ModelListResult list_models(const ModelListRequest& request) const;

private:
    std::shared_ptr<HttpTransport> transport_;
};

}  // namespace pastit
