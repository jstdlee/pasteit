#include "decision/usage_model.hpp"
#include "history/usage_store.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>

namespace {

constexpr std::int64_t kDay = 24LL * 60 * 60 * 1000;

pasteit::ActionInstance action(pasteit::ActionKind kind, std::string target = {}) {
    pasteit::ActionInstance value;
    value.kind = kind;
    value.id = "a_" + std::to_string(static_cast<int>(kind)) + target;
    value.target_ref = std::move(target);
    return value;
}

}  // namespace

int main() {
    using namespace pasteit;
    const std::int64_t now = 1'790'000'000'000;

    DecisionSnapshot snapshot;
    snapshot.recent_paths.push_back({.ref = "p1", .path = "/home/me/notes", .kind = PathKind::Directory});
    // Destination keys use the real path, not the session-local ref.
    assert(usage_action_key(action(ActionKind::SaveTextFile, "p1"), snapshot) == "save_text_file|dest:/home/me/notes");

    assert(usage_action_label("save_text_file|dest:/home/me/notes/") == "Save text file \xE2\x86\x92 notes");
    assert(usage_action_label("transform_text|template:builtin-translate") == "Transform text (builtin-translate)");

    UsageModel model;
    const UsageContext code{.kind = ContentKind::Text, .signals = {"code"}, .app = "Code", .now_ms = now};
    const UsageContext prose{.kind = ContentKind::Text, .signals = {}, .app = "Firefox", .now_ms = now};
    for (int i = 0; i < 6; ++i) record_usage(model, code, "explain_code");
    record_usage(model, code, "paste_text");
    for (int i = 0; i < 4; ++i) record_usage(model, prose, "paste_text");

    // Frequency within a matching context dominates.
    assert(usage_share(model, code, "explain_code") > usage_share(model, code, "paste_text"));
    assert(usage_share(model, prose, "paste_text") > usage_share(model, prose, "explain_code"));
    assert(usage_bonus(model, code, "explain_code") <= kMaximumUsageBonus);

    // One click is shrunk toward zero; repeated choice grows the share.
    UsageModel sparse;
    record_usage(sparse, prose, "open_url");
    const double once = usage_share(sparse, prose, "open_url");
    for (int i = 0; i < 9; ++i) record_usage(sparse, prose, "open_url");
    assert(once < 0.5 && usage_share(sparse, prose, "open_url") > once);

    const auto habits = usage_habits(model, code, 3);
    assert(!habits.empty() && habits.front().action_key == "explain_code");
    assert(habits.front().count == 6);

    // Old habits fade with a two-week half-life.
    UsageContext later = code;
    later.now_ms = now + 14 * kDay;
    record_usage(model, later, "copy_text");
    const double faded = usage_share(model, later, "explain_code");
    assert(faded < usage_share(model, code, "explain_code"));

    const auto summary = usage_summary(model, now, 5);
    assert(summary.contains("text"));

    const auto root = std::filesystem::temp_directory_path() / "pasteit-usage-test";
    std::filesystem::remove_all(root);
    UsageStore store(root / "usage.json");
    std::string error;
    assert(store.save(model, error));
    const auto loaded = store.load();
    assert(loaded.warning.empty());
    assert(std::fabs(usage_share(loaded.model, code, "explain_code") - usage_share(model, code, "explain_code")) < 1e-6);
    std::filesystem::remove_all(root);
}
