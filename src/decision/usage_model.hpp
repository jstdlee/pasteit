#pragma once

#include "core/action.hpp"
#include "core/protocol.hpp"
#include "core/types.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace pasteit {

// One decayed tally of how often an action was chosen in a context.
struct UsageCell {
    double weight = 0.0;
    std::int64_t last_ms = 0;
    std::uint32_t count = 0;
};

// context key -> action key -> tally. Context keys describe the clipboard
// (content kind, detected signal, focused app, time of day) and never contain
// clipboard text, so the model can be shared with Djev as a habit hint.
struct UsageModel {
    std::map<std::string, std::map<std::string, UsageCell>> contexts;
};

struct UsageContext {
    ContentKind kind = ContentKind::Unknown;
    std::vector<std::string> signals;  // e.g. "code", "ip", "json", "color"
    std::string app;
    std::int64_t now_ms = 0;
};

struct UsageHabit {
    std::string action_key;
    double share = 0.0;
    std::uint32_t count = 0;
};

inline constexpr double kUsageHalfLifeDays = 14.0;
inline constexpr double kMaximumUsageBonus = 0.20;

// Stable across sessions: kind slug + destination path + prompt template.
std::string usage_action_key(const ActionInstance& action, const DecisionSnapshot& snapshot);

// Readable form of a usage key, e.g. "Save text file → notes".
std::string usage_action_label(std::string_view action_key);

void record_usage(UsageModel& model, const UsageContext& context, const std::string& action_key);

// Smoothed share of choices in similar contexts, 0..1. Sparse evidence is
// shrunk toward zero so a single click does not dominate the ranking.
double usage_share(const UsageModel& model, const UsageContext& context, const std::string& action_key);
double usage_bonus(const UsageModel& model, const UsageContext& context, const std::string& action_key);

// Most frequent actions for a context, most frequent first.
std::vector<UsageHabit> usage_habits(const UsageModel& model, const UsageContext& context, std::size_t limit);

// Per content kind: most frequent actions across all its contexts, for the
// Settings insight view.
std::map<std::string, std::vector<UsageHabit>> usage_summary(const UsageModel& model, std::int64_t now_ms,
                                                             std::size_t per_kind);

// Drops faded entries and caps model size.
void prune_usage(UsageModel& model, std::int64_t now_ms);

}  // namespace pasteit
