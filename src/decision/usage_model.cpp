#include "decision/usage_model.hpp"

#include "actions/action_catalog.hpp"
#include "util/path_utf8.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <functional>
#include <tuple>
#include <utility>

namespace pastit {
namespace {

constexpr double kMillisecondsPerDay = 24.0 * 60.0 * 60.0 * 1000.0;
constexpr double kEvidencePrior = 2.0;
constexpr double kFadedWeight = 0.05;
constexpr std::size_t kMaximumContexts = 400;
constexpr std::size_t kMaximumActionsPerContext = 40;

std::string kind_name(ContentKind kind) {
    switch (kind) {
        case ContentKind::Text: return "text";
        case ContentKind::Url: return "url";
        case ContentKind::Email: return "email";
        case ContentKind::Image: return "image";
        case ContentKind::Path: return "path";
        case ContentKind::Json: return "json";
        case ContentKind::DateTime: return "datetime";
        case ContentKind::Unknown: break;
    }
    return "unknown";
}

std::string lowercase(std::string value) {
    for (auto& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

std::string time_bucket(std::int64_t now_ms) {
    const std::time_t seconds = static_cast<std::time_t>(now_ms / 1000);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    if (local.tm_hour < 6) return "night";
    if (local.tm_hour < 12) return "morning";
    if (local.tm_hour < 18) return "afternoon";
    return "evening";
}

double decayed(const UsageCell& cell, std::int64_t now_ms) {
    if (!std::isfinite(cell.weight) || cell.weight <= 0.0) return 0.0;
    const double age_days = std::max<double>(0.0, static_cast<double>(now_ms - cell.last_ms)) / kMillisecondsPerDay;
    return cell.weight * std::pow(0.5, age_days / kUsageHalfLifeDays);
}

// Context keys paired with how much each one should count. Signals are the
// most specific description of the clipboard, so they weigh the most.
std::vector<std::pair<std::string, double>> context_keys(const UsageContext& context) {
    const auto kind = "k:" + kind_name(context.kind);
    std::vector<std::pair<std::string, double>> keys{{kind, 1.0}};
    for (const auto& signal : context.signals) keys.emplace_back(kind + "|s:" + signal, 1.5);
    if (!context.app.empty()) keys.emplace_back(kind + "|app:" + lowercase(context.app), 1.0);
    if (context.now_ms > 0) keys.emplace_back(kind + "|t:" + time_bucket(context.now_ms), 0.5);
    return keys;
}

double context_total(const std::map<std::string, UsageCell>& cells, std::int64_t now_ms) {
    double total = 0.0;
    for (const auto& [key, cell] : cells) total += decayed(cell, now_ms);
    return total;
}

}  // namespace

std::string usage_action_key(const ActionInstance& action, const DecisionSnapshot& snapshot) {
    std::string key = action_kind_slug(action.kind);
    if (!action.target_ref.empty()) {
        const auto target = std::find_if(snapshot.recent_paths.begin(), snapshot.recent_paths.end(),
                                         [&](const PathLocation& path) { return path.ref == action.target_ref; });
        key += "|dest:" + (target != snapshot.recent_paths.end() ? path_to_utf8_string(target->path)
                                                                 : action.target_ref);
    }
    if (const auto template_id = action.parameters.find("template_id"); template_id != action.parameters.end()) {
        key += "|template:" + template_id->second;
    }
    if (const auto recipe = action.parameters.find("recipe_id"); recipe != action.parameters.end()) {
        key += "|recipe:" + recipe->second;
    }
    return key;
}

std::string usage_action_label(std::string_view action_key) {
    const auto bar = action_key.find('|');
    std::string label{action_key.substr(0, bar)};
    std::replace(label.begin(), label.end(), '_', ' ');
    if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
    for (auto rest = bar == std::string_view::npos ? std::string_view{} : action_key.substr(bar + 1); !rest.empty();) {
        const auto next = rest.find('|');
        const auto part = rest.substr(0, next);
        if (part.starts_with("dest:")) {
            auto path = part.substr(5);
            while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) path.remove_suffix(1);
            const auto slash = path.find_last_of("/\\");
            label += " \xE2\x86\x92 " + std::string{slash == std::string_view::npos ? path : path.substr(slash + 1)};
        } else if (part.starts_with("recipe:")) {
            label += " (" + std::string{part.substr(7)} + ")";
        } else if (part.starts_with("template:")) {
            label += " (" + std::string{part.substr(9)} + ")";
        }
        rest = next == std::string_view::npos ? std::string_view{} : rest.substr(next + 1);
    }
    return label;
}

void record_usage(UsageModel& model, const UsageContext& context, const std::string& action_key) {
    for (const auto& [key, alpha] : context_keys(context)) {
        auto& cell = model.contexts[key][action_key];
        cell.weight = decayed(cell, context.now_ms) + 1.0;
        cell.last_ms = context.now_ms;
        ++cell.count;
    }
    prune_usage(model, context.now_ms);
}

double usage_share(const UsageModel& model, const UsageContext& context, const std::string& action_key) {
    double score = 0.0;
    double weights = 0.0;
    for (const auto& [key, alpha] : context_keys(context)) {
        const auto found = model.contexts.find(key);
        if (found == model.contexts.end()) continue;
        const double total = context_total(found->second, context.now_ms);
        if (total <= 0.0) continue;
        const auto cell = found->second.find(action_key);
        const double share = cell == found->second.end() ? 0.0 : decayed(cell->second, context.now_ms) / total;
        score += alpha * share * (total / (total + kEvidencePrior));
        weights += alpha;
    }
    return weights > 0.0 ? std::clamp(score / weights, 0.0, 1.0) : 0.0;
}

double usage_bonus(const UsageModel& model, const UsageContext& context, const std::string& action_key) {
    return kMaximumUsageBonus * usage_share(model, context, action_key);
}

std::vector<UsageHabit> usage_habits(const UsageModel& model, const UsageContext& context, std::size_t limit) {
    std::map<std::string, std::uint32_t> counts;
    for (const auto& [key, alpha] : context_keys(context)) {
        const auto found = model.contexts.find(key);
        if (found == model.contexts.end()) continue;
        for (const auto& [action_key, cell] : found->second) {
            counts[action_key] = std::max(counts[action_key], cell.count);
        }
    }
    std::vector<UsageHabit> habits;
    for (const auto& [action_key, count] : counts) {
        const double share = usage_share(model, context, action_key);
        if (share > 0.0) habits.push_back({action_key, share, count});
    }
    std::sort(habits.begin(), habits.end(), [](const UsageHabit& left, const UsageHabit& right) {
        return std::tie(left.share, right.action_key) > std::tie(right.share, left.action_key);
    });
    if (habits.size() > limit) habits.resize(limit);
    return habits;
}

std::map<std::string, std::vector<UsageHabit>> usage_summary(const UsageModel& model, std::int64_t now_ms,
                                                             std::size_t per_kind) {
    std::map<std::string, std::vector<UsageHabit>> summary;
    for (const auto& [key, cells] : model.contexts) {
        if (!key.starts_with("k:") || key.find('|') != std::string::npos) continue;
        const double total = context_total(cells, now_ms);
        if (total <= 0.0) continue;
        auto& habits = summary[key.substr(2)];
        for (const auto& [action_key, cell] : cells) {
            habits.push_back({action_key, decayed(cell, now_ms) / total, cell.count});
        }
        std::sort(habits.begin(), habits.end(), [](const UsageHabit& left, const UsageHabit& right) {
            return left.share > right.share;
        });
        if (habits.size() > per_kind) habits.resize(per_kind);
    }
    return summary;
}

void prune_usage(UsageModel& model, std::int64_t now_ms) {
    std::vector<std::pair<std::int64_t, std::string>> recency;
    for (auto it = model.contexts.begin(); it != model.contexts.end();) {
        auto& cells = it->second;
        std::erase_if(cells, [&](const auto& entry) { return decayed(entry.second, now_ms) < kFadedWeight; });
        if (cells.size() > kMaximumActionsPerContext) {
            std::vector<std::pair<double, std::string>> ordered;
            for (const auto& [key, cell] : cells) ordered.emplace_back(decayed(cell, now_ms), key);
            std::sort(ordered.begin(), ordered.end(), std::greater<>());
            for (std::size_t index = kMaximumActionsPerContext; index < ordered.size(); ++index) {
                cells.erase(ordered[index].second);
            }
        }
        if (cells.empty()) {
            it = model.contexts.erase(it);
            continue;
        }
        std::int64_t newest = 0;
        for (const auto& [key, cell] : cells) newest = std::max(newest, cell.last_ms);
        recency.emplace_back(newest, it->first);
        ++it;
    }
    if (recency.size() > kMaximumContexts) {
        std::sort(recency.begin(), recency.end());
        for (std::size_t index = 0; index < recency.size() - kMaximumContexts; ++index) {
            model.contexts.erase(recency[index].second);
        }
    }
}

}  // namespace pastit
