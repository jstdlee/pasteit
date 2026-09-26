#include "ai/model_catalog.hpp"

#include "util/json.hpp"

#include <algorithm>

namespace pasteit {

std::string normalize_models_endpoint(std::string endpoint) {
    while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
    constexpr std::string_view completions = "/v1/chat/completions";
    constexpr std::string_view models = "/v1/models";
    constexpr std::string_view version = "/v1";
    if (endpoint.ends_with(models)) return endpoint;
    if (endpoint.ends_with(completions)) return endpoint.substr(0, endpoint.size() - completions.size()) + std::string{models};
    if (endpoint.ends_with(version)) return endpoint + "/models";
    return endpoint + std::string{models};
}

ModelListResult OpenAiCompatibleModelClient::list_models(const ModelListRequest& request) const {
    ModelListResult result;
    if (request.endpoint.empty()) {
        result.error = "LLM endpoint is empty";
        return result;
    }
    HttpRequest wire{.url=normalize_models_endpoint(request.endpoint), .timeout=std::chrono::milliseconds{5000}};
    if (!request.api_key.empty()) wire.headers["Authorization"] = "Bearer " + request.api_key;
    const auto response = transport_->get_json(wire);
    result.http_status = response.status;
    if (!response.transport_error.empty()) {
        result.error = "Model list request failed: " + response.transport_error;
        return result;
    }
    const auto root = parse_json(response.body);
    if (response.status < 200 || response.status >= 300) {
        if (root) {
            const auto* error = root->get("error");
            const auto* message = error ? error->get("message") : nullptr;
            if (message && message->string()) result.error = *message->string();
        }
        if (result.error.empty()) result.error = "Model list request failed (HTTP " + std::to_string(response.status) + ")";
        return result;
    }
    if (!root || root->object() == nullptr) {
        result.error = "Model list response was not valid JSON";
        return result;
    }
    const auto* array = root->get("data") ? root->get("data")->array() : nullptr;
    if (array == nullptr) array = root->get("models") ? root->get("models")->array() : nullptr;
    if (array == nullptr) {
        result.error = "Model list response did not include data[] or models[]";
        return result;
    }
    for (const auto& value : *array) {
        const auto* id = value.object() ? value.get("id") : nullptr;
        if (id == nullptr || !id->string() || id->string()->empty()) continue;
        result.model_ids.push_back(*id->string());
    }
    std::sort(result.model_ids.begin(), result.model_ids.end());
    result.model_ids.erase(std::unique(result.model_ids.begin(), result.model_ids.end()), result.model_ids.end());
    if (result.model_ids.empty()) {
        result.error = "Model list response contained no model IDs";
        return result;
    }
    result.ok = true;
    return result;
}

}  // namespace pasteit
