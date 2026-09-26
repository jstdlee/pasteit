#include "djev/djev_client.hpp"
#include "net/http_client.hpp"

#include <cassert>
#include <memory>

namespace {
class FixedTransport final : public pasteit::HttpTransport {
public:
    explicit FixedTransport(pasteit::HttpResponse response) : response_(std::move(response)) {}
    pasteit::HttpResponse post_json(const pasteit::HttpRequest&) override { return response_; }
private:
    pasteit::HttpResponse response_;
};

pasteit::DecisionRequest request() {
    pasteit::DecisionRequest value;
    value.request_id = "req-422";
    value.snapshot.available_actions.push_back({.id="a", .label="A", .description="A", .enabled=true});
    return value;
}
}

int main() {
    auto json = std::make_shared<FixedTransport>(pasteit::HttpResponse{
        .status=422,
        .body=R"({"error":{"message":"question 'best_action': at most 26 alternatives","type":"validation_error"}})"});
    pasteit::DjevClient first("http://local/v1/systemone", "djev", std::chrono::milliseconds{50}, json, "secret-key");
    const auto parsed = first.decide(request());
    assert(!parsed.valid);
    assert(parsed.http_status == 422);
    assert(parsed.error == "question 'best_action': at most 26 alternatives");
    assert(parsed.error.find("secret-key") == std::string::npos);

    auto plain = std::make_shared<FixedTransport>(pasteit::HttpResponse{.status=422, .body="unprocessable"});
    pasteit::DjevClient second("http://local/v1/systemone", "djev", std::chrono::milliseconds{50}, plain, "secret-key");
    const auto fallback = second.decide(request());
    assert(!fallback.valid);
    assert(fallback.error == "Djev request failed (HTTP 422)");
}
