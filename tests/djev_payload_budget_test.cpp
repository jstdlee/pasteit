#include "djev/djev_client.hpp"
#include "util/json.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>

namespace {

std::string repeat(std::string_view value, std::size_t count) {
    std::string out;
    out.reserve(value.size() * count);
    for (std::size_t index = 0; index < count; ++index) {
        out.append(value);
    }
    return out;
}

std::size_t count_occurrences(std::string_view text, std::string_view needle) {
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(needle, offset)) != std::string_view::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

std::string two_digit(int value) {
    std::ostringstream out;
    if (value < 10) {
        out << '0';
    }
    out << value;
    return out.str();
}

pastit::DecisionRequest base_request(std::string request_id, std::string clipboard_ref) {
    pastit::DecisionRequest request;
    request.protocol_version = 1;
    request.request_id = std::move(request_id);
    request.snapshot.clipboard_hash = "clip_hash_budget";
    request.snapshot.focused_target_hash = "focus_hash_budget";
    request.snapshot.focused_app = "Code";
    request.snapshot.captured_at_ms = 900;
    request.snapshot.clipboard_items.push_back(pastit::ClipboardItem{
        .ref = std::move(clipboard_ref),
        .mime_types = {"text/plain"},
        .kind = pastit::ContentKind::Text,
        .preview = "current text",
        .size_bytes = 12,
        .captured_at_ms = 900,
        .source_app = "test",
    });
    return request;
}

bool contains_choice(std::string_view payload, std::string_view id) {
    return payload.find("\"" + std::string{id} + "\"") != std::string_view::npos;
}

bool retained_targets_are_represented(
    std::string_view payload, const std::vector<pastit::ActionInstance>& actions) {
    for (const auto& action : actions) {
        if (!contains_choice(payload, action.id) || action.target_ref.empty()) {
            continue;
        }
        if (payload.find("\"ref\":\"" + action.target_ref + "\"") == std::string_view::npos) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    pastit::DecisionRequest request;
    request.protocol_version = 1;
    request.request_id = "req_budget";
    request.snapshot.clipboard_hash = "clip_hash_budget";
    request.snapshot.focused_target_hash = "focus_hash_budget";
    request.snapshot.focused_app = "Code";
    request.snapshot.focused_window_title = repeat("window-title-", 40) + "tail-window-marker";
    request.snapshot.focused_current_directory = "/tmp/pastit-budget";
    request.snapshot.captured_at_ms = 900;
    request.snapshot.clipboard_items.push_back(pastit::ClipboardItem{
        .ref = "clip_cjk_current",
        .mime_types = {"text/plain", "text/html"},
        .kind = pastit::ContentKind::Text,
        .preview = repeat("界", 700) + "tail-preview-marker",
        .size_bytes = 4096,
        .captured_at_ms = 900,
        .source_app = "test",
    });

    for (int index = 0; index < 6; ++index) {
        request.snapshot.recent_paths.push_back(pastit::PathLocation{
            .ref = "path_" + two_digit(index),
            .path = std::filesystem::path{"/tmp"} / repeat("destination-segment-", 20) / ("leaf_" + two_digit(index)),
            .kind = pastit::PathKind::Directory,
            .last_seen_ms = 800 - index,
            .source = "budget-test",
            .exists = true,
        });
    }

    for (int index = 0; index < 26; ++index) {
        request.snapshot.available_actions.push_back(pastit::ActionInstance{
            .id = "a_budget_" + two_digit(index),
            .kind = index % 2 == 0 ? pastit::ActionKind::SaveTextFile : pastit::ActionKind::TransformText,
            .source_ref = "clip_cjk_current",
            .target_ref = "path_" + two_digit(index % 6),
            .filename = repeat("long-filename-", 12) + ".txt",
            .representation = "raw",
            .label = repeat("Long action label ", 20),
            .description = repeat("Use this deliberately long destination-aware action description. ", 30) + repeat("界", 80),
            .enabled = true,
        });
    }

    const auto payload = pastit::DjevClient::build_payload(request, "jev-budget");
    assert(pastit::is_valid_json(payload));
    assert(payload.size() <= 10 * 1024);
    assert(payload.find(R"("history")") == std::string::npos);
    assert(payload.find(R"("available_actions")") == std::string::npos);
    assert(payload.find("tail-preview-marker") == std::string::npos);
    assert(payload.find("tail-window-marker") == std::string::npos);
    assert(count_occurrences(payload, R"("a_budget_)") == 26);
    for (const auto& action : request.snapshot.available_actions) {
        assert(count_occurrences(payload, "\"" + action.id + "\"") == 1);
    }

    auto path_cap_request = base_request("req_path_cap", "clip_path_cap");
    for (int index = 0; index < 6; ++index) {
        path_cap_request.snapshot.recent_paths.push_back(pastit::PathLocation{
            .ref = "recent_" + two_digit(index),
            .path = std::filesystem::path{"/tmp"} / ("recent_" + two_digit(index)),
            .kind = pastit::PathKind::Directory,
            .last_seen_ms = 800 - index,
            .source = "recent",
            .exists = true,
        });
    }
    path_cap_request.snapshot.recent_paths.push_back(pastit::PathLocation{
        .ref = "configured_default",
        .path = "/tmp/configured-default",
        .kind = pastit::PathKind::Directory,
        .last_seen_ms = 900,
        .source = "configured-text",
        .exists = true,
    });
    path_cap_request.snapshot.available_actions.push_back(pastit::ActionInstance{
        .id = "a_configured_default",
        .kind = pastit::ActionKind::SaveTextFile,
        .source_ref = "clip_path_cap",
        .target_ref = "configured_default",
        .description = "Save to the configured default",
        .enabled = true,
    });
    for (int index = 0; index < 6; ++index) {
        path_cap_request.snapshot.available_actions.push_back(pastit::ActionInstance{
            .id = "a_recent_" + two_digit(index),
            .kind = pastit::ActionKind::SaveTextFile,
            .source_ref = "clip_path_cap",
            .target_ref = "recent_" + two_digit(index),
            .description = "Save to a recent path",
            .enabled = true,
        });
    }

    const auto path_cap_payload = pastit::DjevClient::build_payload(path_cap_request, "jev-path-cap");
    assert(pastit::is_valid_json(path_cap_payload));
    const bool path_count_is_bounded = count_occurrences(path_cap_payload, R"("path":)") <= 6;
    const bool configured_action_is_retained = contains_choice(path_cap_payload, "a_configured_default");
    const bool configured_path_is_represented =
        path_cap_payload.find(R"("ref":"configured_default")") != std::string::npos;
    const bool path_cap_targets_are_represented =
        retained_targets_are_represented(path_cap_payload, path_cap_request.snapshot.available_actions);

    auto degradation_request = base_request("req_degradation", "clip_degradation");
    degradation_request.snapshot.focused_window_title = repeat("窗", 300) + "tail-title-marker";
    degradation_request.snapshot.focused_current_directory = "/" + repeat("目录", 180) + "tail-directory-marker";
    degradation_request.snapshot.clipboard_items.front().preview = repeat("界", 700) + "tail-preview-marker";
    degradation_request.snapshot.recent_paths = {
        pastit::PathLocation{
            .ref = "configured_unique",
            .path = std::filesystem::path{"/tmp"} / (repeat("唯", 300) + "tail-unique-path-marker"),
            .kind = pastit::PathKind::Directory,
            .last_seen_ms = 900,
            .source = "configured-text",
            .exists = true,
        },
        pastit::PathLocation{
            .ref = "duplicate_target",
            .path = std::filesystem::path{"/tmp"} / (repeat("复", 300) + "tail-duplicate-path-marker"),
            .kind = pastit::PathKind::Directory,
            .last_seen_ms = 800,
            .source = "recent",
            .exists = true,
        },
    };
    degradation_request.snapshot.available_actions.push_back(pastit::ActionInstance{
        .id = "a_unique_paste",
        .kind = pastit::ActionKind::PasteText,
        .source_ref = "clip_degradation",
        .description = repeat("粘", 200),
        .enabled = true,
    });
    degradation_request.snapshot.available_actions.push_back(pastit::ActionInstance{
        .id = "a_unique_file",
        .kind = pastit::ActionKind::SaveTextFile,
        .source_ref = "clip_degradation",
        .target_ref = "configured_unique",
        .description = repeat("存", 200),
        .enabled = true,
    });
    for (int index = 0; index < 8; ++index) {
        degradation_request.snapshot.available_actions.push_back(pastit::ActionInstance{
            .id = "a_destination_duplicate_" + two_digit(index),
            .kind = pastit::ActionKind::SaveJsonFile,
            .source_ref = "clip_degradation",
            .target_ref = "duplicate_target",
            .description = repeat("径", 200),
            .enabled = true,
        });
    }
    for (int index = 0; index < 16; ++index) {
        degradation_request.snapshot.available_actions.push_back(pastit::ActionInstance{
            .id = "a_prompt_duplicate_" + two_digit(index),
            .kind = pastit::ActionKind::TransformText,
            .source_ref = "clip_degradation",
            .description = repeat("问", 200),
            .enabled = true,
        });
    }

    const auto degradation_payload = pastit::DjevClient::build_payload(degradation_request, repeat("模", 400));
    assert(pastit::is_valid_json(degradation_payload));
    assert(degradation_payload.size() <= 10 * 1024);
    assert(contains_choice(degradation_payload, "a_unique_paste"));
    assert(contains_choice(degradation_payload, "a_unique_file"));
    assert(count_occurrences(degradation_payload, R"("a_destination_duplicate_)") == 1);
    const auto prompt_count = count_occurrences(degradation_payload, R"("a_prompt_duplicate_)");
    assert(prompt_count > 0);
    assert(prompt_count < 16);
    assert(degradation_payload.find("tail-preview-marker") == std::string::npos);
    assert(degradation_payload.find("tail-title-marker") == std::string::npos);
    assert(degradation_payload.find("tail-directory-marker") == std::string::npos);
    assert(degradation_payload.find("tail-unique-path-marker") == std::string::npos);
    assert(degradation_payload.find("tail-duplicate-path-marker") == std::string::npos);
    assert(retained_targets_are_represented(degradation_payload, degradation_request.snapshot.available_actions));

    auto malformed_utf8_request = base_request("req_malformed_utf8", "clip_malformed_utf8");
    malformed_utf8_request.snapshot.focused_window_title = "broken ";
    malformed_utf8_request.snapshot.focused_window_title.push_back(static_cast<char>(0xE9));
    malformed_utf8_request.snapshot.focused_window_title.push_back(static_cast<char>(0xAA));
    malformed_utf8_request.snapshot.focused_window_title.push_back('.');
    malformed_utf8_request.snapshot.available_actions.push_back(pastit::ActionInstance{
        .id = "a_malformed_utf8",
        .kind = pastit::ActionKind::PasteText,
        .source_ref = "clip_malformed_utf8",
        .description = "Paste malformed-boundary fixture",
        .enabled = true,
    });
    const auto malformed_utf8_payload = pastit::DjevClient::build_payload(malformed_utf8_request, "jev-utf8");
    const std::string malformed_sequence{static_cast<char>(0xE9), static_cast<char>(0xAA), '.'};
    assert(malformed_utf8_payload.find(malformed_sequence) == std::string::npos);
    assert(malformed_utf8_payload.find("\xEF\xBF\xBD") != std::string::npos);
    assert(path_count_is_bounded);
    assert(configured_action_is_retained);
    assert(configured_path_is_represented);
    assert(path_cap_targets_are_represented);
}
