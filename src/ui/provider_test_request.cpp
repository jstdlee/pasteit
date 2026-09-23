#include "ui/provider_test_request.hpp"

#include <utility>

namespace pastit {

DecisionRequest provider_test_request() {
    DecisionRequest request;
    request.request_id = "settings-djev-test";
    request.snapshot.clipboard_hash = "settings-test-clipboard";
    request.snapshot.focused_target_hash = "settings-test-target";
    ClipboardItem test_clipboard;
    test_clipboard.ref = "settings_test";
    test_clipboard.mime_types = {"text/plain"};
    test_clipboard.kind = ContentKind::Text;
    test_clipboard.preview = "Jev provider connection test";
    test_clipboard.size_bytes = test_clipboard.preview.size();
    request.snapshot.clipboard_items.push_back(std::move(test_clipboard));
    ActionInstance test_action;
    test_action.id = "settings_test_action";
    test_action.kind = ActionKind::PasteText;
    test_action.source_ref = "settings_test";
    test_action.label = "Provider test";
    test_action.description = "Use this option to validate that the configured Djev provider returns a structured choice.";
    test_action.enabled = true;
    request.snapshot.available_actions.push_back(std::move(test_action));
    ActionInstance alternative;
    alternative.id = "settings_test_alternative";
    alternative.kind = ActionKind::PasteText;
    alternative.source_ref = "settings_test";
    alternative.label = "Provider test alternative";
    alternative.description = "Use this alternative when the provider considers the first connectivity-test option less useful.";
    alternative.enabled = true;
    request.snapshot.available_actions.push_back(std::move(alternative));
    return request;
}

}  // namespace pastit
