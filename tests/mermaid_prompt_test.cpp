#include "ai/mermaid_prompt.hpp"
#include "djev/djev_client.hpp"

#include <cassert>
#include <string>

namespace {

pastit::DecisionRequest mermaid_request() {
    pastit::ActionInstance action;
    action.id = "a_draw_mermaid_diagram_clip_current";
    action.kind = pastit::ActionKind::DrawMermaidDiagram;
    action.source_ref = "clip_current";
    action.label = "Draw Mermaid diagram";
    action.description = "Generate Mermaid diagram source from this relationship-like text";
    action.enabled = true;

    pastit::DecisionRequest request;
    request.request_id = "req_mermaid";
    request.snapshot.clipboard_hash = "clip_hash";
    request.snapshot.focused_target_hash = "target_hash";
    request.snapshot.clipboard_items.push_back(pastit::ClipboardItem{
        .ref = "clip_current",
        .mime_types = {"text/plain"},
        .kind = pastit::ContentKind::Text,
        .preview = "A depends on B",
        .size_bytes = 14,
        .captured_at_ms = 1,
    });
    request.snapshot.available_actions.push_back(std::move(action));
    return request;
}

}  // namespace

int main() {
    using namespace pastit;

    ProviderSettings provider{
        .endpoint = "http://localhost:11434/v1",
        .model_id = "diagram-model",
        .api_key = "secret",
    };
    const auto long_source = std::string(9000, 'A') + " final detail that should be outside the budget";
    const auto request = build_mermaid_generation_request("req_mermaid_ai", provider, long_source);
    assert(request.request_id == "req_mermaid_ai");
    assert(request.endpoint == provider.endpoint);
    assert(request.model_id == provider.model_id);
    assert(request.api_key == provider.api_key);
    assert(request.temperature == 0.0);
    assert(request.system_message.find("Mermaid source only") != std::string::npos);
    assert(request.system_message.find("without Markdown fences") != std::string::npos);
    assert(request.system_message.find("no explanatory prose") != std::string::npos);
    assert(request.user_message.size() <= kMermaidInputBudgetBytes + 512);
    assert(request.user_message.find("final detail that should be outside the budget") == std::string::npos);

    const auto fenced = normalize_mermaid_response(
        "Sure, here is the diagram:\n```mermaid\nflowchart TD\n  A --> B\n```\nHope this helps.");
    assert(fenced.ok);
    assert(fenced.raw_source.find("Sure, here is") != std::string::npos);
    assert(fenced.normalized_source == "flowchart TD\n  A --> B\n");

    const auto bare_fence = normalize_mermaid_response("```\nsequenceDiagram\n  Alice->>Bob: Hi\n```");
    assert(bare_fence.ok);
    assert(bare_fence.normalized_source == "sequenceDiagram\n  Alice->>Bob: Hi\n");

    const auto prose_wrapped = normalize_mermaid_response(
        "Here is a clean diagram:\nflowchart TD\n  Input --> Model\nThanks.");
    assert(prose_wrapped.ok);
    assert(prose_wrapped.normalized_source == "flowchart TD\n  Input --> Model\n");

    const auto unsupported = normalize_mermaid_response("Here is a diagram: A -> B");
    assert(!unsupported.ok);
    assert(unsupported.normalized_source.empty());
    assert(unsupported.error.find("supported Mermaid diagram header") != std::string::npos);

    const auto header_prefix_word = normalize_mermaid_response("piece of prose, not a pie chart");
    assert(!header_prefix_word.ok);

    const auto payload = DjevClient::build_payload(mermaid_request(), "jev-test");
    assert(payload.find(R"("criteria":{"a_draw_mermaid_diagram_clip_current":"Generate Mermaid diagram source)") !=
           std::string::npos);
    assert(payload.find(R"("criteria":[)") == std::string::npos);
    assert(payload.find(R"("available_actions")") == std::string::npos);
}
