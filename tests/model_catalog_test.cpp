#include "ai/model_catalog.hpp"

#include <cassert>
#include <memory>

namespace {
class FakeTransport final : public pastit::HttpTransport {
public:
    pastit::HttpResponse post_json(const pastit::HttpRequest&) override { return {}; }
    pastit::HttpResponse get_json(const pastit::HttpRequest& request) override {
        seen_url = request.url;
        return {.status=200, .body=R"({"data":[{"id":"z-model"},{"id":"a-model"},{"id":"z-model"}]})"};
    }
    std::string seen_url;
};
}

int main() {
    auto transport = std::make_shared<FakeTransport>();
    pastit::OpenAiCompatibleModelClient client(transport);
    const auto result = client.list_models({.endpoint="http://provider.example/v1/chat/completions", .api_key="secret"});
    assert(result.ok);
    const std::vector<std::string> expected{"a-model", "z-model"};
    assert(result.model_ids == expected);
    assert(transport->seen_url == "http://provider.example/v1/models");
}
