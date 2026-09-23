#include "actions/action_catalog.hpp"
#include "djev/decision_ranker.hpp"

#include <cassert>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    assert(in.good());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::string> ids_for(const pastit::ActionCatalog& catalog) {
    std::vector<std::string> ids;
    ids.reserve(catalog.actions.size());
    for (const auto& action : catalog.actions) {
        ids.push_back(action.id);
    }
    return ids;
}

pastit::DecisionSnapshot fixture_snapshot() {
    const auto fixture_root = std::filesystem::path{__FILE__}.parent_path() / "fixtures";
    assert(std::filesystem::exists(fixture_root / "clipboard_text.txt"));
    assert(std::filesystem::exists(fixture_root / "sample_resume.txt"));
    assert(std::filesystem::exists(fixture_root / "sample.json"));
    assert(std::filesystem::exists(fixture_root / "sample.png"));

    pastit::DecisionSnapshot snapshot;
    snapshot.clipboard_hash = "fixture_clipboard_hash";
    snapshot.focused_target_hash = "fixture_target_hash";
    snapshot.captured_at_ms = 1726800000123;
    snapshot.recent_paths.push_back(pastit::PathLocation{
        .ref = "path_01",
        .path = std::filesystem::temp_directory_path(),
        .kind = pastit::PathKind::Directory,
        .last_seen_ms = 10,
        .source = "fixture",
        .exists = true,
    });
    snapshot.clipboard_items.push_back(pastit::ClipboardItem{
        .ref = "clip_text",
        .mime_types = {"text/plain"},
        .kind = pastit::ContentKind::Text,
        .preview = read_text_file(fixture_root / "clipboard_text.txt"),
        .size_bytes = 0,
    });
    snapshot.clipboard_items.push_back(pastit::ClipboardItem{
        .ref = "clip_resume",
        .mime_types = {"text/plain"},
        .kind = pastit::ContentKind::Text,
        .preview = read_text_file(fixture_root / "sample_resume.txt"),
        .size_bytes = 0,
    });
    snapshot.clipboard_items.push_back(pastit::ClipboardItem{
        .ref = "clip_json",
        .mime_types = {"application/json"},
        .kind = pastit::ContentKind::Json,
        .preview = read_text_file(fixture_root / "sample.json"),
        .size_bytes = 0,
    });
    snapshot.clipboard_items.push_back(pastit::ClipboardItem{
        .ref = "clip_image",
        .mime_types = {"image/png"},
        .kind = pastit::ContentKind::Image,
        .preview = "sample.png thumbnail",
        .size_bytes = static_cast<std::uint64_t>(std::filesystem::file_size(fixture_root / "sample.png")),
    });
    return snapshot;
}

}  // namespace

int main() {
    using namespace pastit;

    const auto snapshot = fixture_snapshot();
    const auto first_catalog = build_catalog(snapshot);
    const auto second_catalog = build_catalog(snapshot);
    assert(ids_for(first_catalog) == ids_for(second_catalog));

    DecisionRequest request;
    request.request_id = "req_fixture";
    request.snapshot = snapshot;
    request.snapshot.available_actions = first_catalog.actions;

    DecisionResponse response;
    response.request_id = request.request_id;
    response.choice = first_catalog.actions.at(1).id;
    response.valid = true;
    for (std::size_t index = 0; index < first_catalog.actions.size(); ++index) {
        response.probabilities[first_catalog.actions[index].id] = 1.0 / static_cast<double>(index + 1);
    }

    const auto ranked = rank_top_actions(response, first_catalog);
    assert(ranked.size() == std::min<std::size_t>(5, first_catalog.actions.size()));
    assert(ranked[0].action.id == first_catalog.actions[0].id);
    assert(ranked[1].action.id == first_catalog.actions[1].id);
    assert(ranked[1].selected);

    auto changed = snapshot;
    changed.clipboard_hash = "changed_fixture_hash";
    assert(response_is_stale(request, changed));
}
