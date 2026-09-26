#include "core/action.hpp"
#include "core/protocol.hpp"
#include "core/types.hpp"

#include <cassert>
#include <filesystem>
#include <string>
#include <vector>

int main() {
    using namespace pasteit;

    ClipboardItem text_item;
    text_item.ref = "clip_42";
    text_item.mime_types = {"text/plain"};
    text_item.kind = ContentKind::Text;
    text_item.blob_path = std::filesystem::path{"/tmp/pasteit/blob.txt"};
    text_item.preview = "hello";
    text_item.size_bytes = 5;
    text_item.captured_at_ms = 1726800000123;
    text_item.source_app = "test";
    text_item.tags = {SemanticTag::PlainText};

    assert(text_item.ref == "clip_42");
    assert(text_item.kind == ContentKind::Text);
    assert(text_item.tags.front() == SemanticTag::PlainText);

    PathLocation recent_path;
    recent_path.ref = "path_01";
    recent_path.path = std::filesystem::path{"/tmp"};
    recent_path.kind = PathKind::Directory;
    recent_path.last_seen_ms = 1726800000456;
    recent_path.source = "fixture";
    recent_path.exists = true;

    assert(recent_path.ref == "path_01");
    assert(recent_path.kind == PathKind::Directory);
    assert(recent_path.exists);

    ActionInstance image_action;
    image_action.id = "a_save_image_path_01";
    image_action.kind = ActionKind::SaveImageFile;
    image_action.source_ref = "clip_99";
    image_action.target_ref = recent_path.ref;
    image_action.filename = "clipboard.png";
    image_action.representation = "raw";
    image_action.label = "Save image";
    image_action.description = "Save original image bytes to /tmp";
    image_action.enabled = true;

    assert(image_action.id == "a_save_image_path_01");
    assert(image_action.kind == ActionKind::SaveImageFile);
    assert(image_action.target_ref == "path_01");

    DecisionSnapshot snapshot;
    snapshot.clipboard_hash = "hash_clip";
    snapshot.focused_target_hash = "hash_target";
    snapshot.captured_at_ms = 1726800000999;
    snapshot.clipboard_items = {text_item};
    snapshot.recent_paths = {recent_path};
    snapshot.available_actions = {image_action};

    DecisionRequest request;
    request.protocol_version = 1;
    request.request_id = "req_184";
    request.snapshot = snapshot;

    assert(request.protocol_version == 1);
    assert(request.request_id == "req_184");
    assert(request.snapshot.available_actions.size() == 1);
    assert(request.snapshot.available_actions.front().id == image_action.id);

    DecisionResponse response;
    response.request_id = request.request_id;
    response.choice = image_action.id;
    response.confidence = 0.86;
    response.probabilities[image_action.id] = 0.86;

    assert(response.request_id == "req_184");
    assert(response.choice == "a_save_image_path_01");
    assert(response.probabilities.at(image_action.id) == 0.86);

    ExecutionResult result;
    result.request_id = request.request_id;
    result.action_id = image_action.id;
    result.status = ExecutionStatus::Completed;
    result.message = "saved";
    result.output_path = std::filesystem::path{"/tmp/clipboard.png"};

    assert(result.status == ExecutionStatus::Completed);
    assert(result.output_path->filename() == "clipboard.png");
}
