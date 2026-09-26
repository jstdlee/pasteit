#include "ui/desktop_flow.hpp"
#include "decision/candidate_selector.hpp"
#include "detect/fast_content_detector.hpp"
#include "util/path_utf8.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <set>
#include <sstream>
#include <string_view>

namespace pastit {
namespace {

void hash_append(std::uint64_t& hash, std::string_view value) {
    for (const unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
}

std::string snapshot_hash(const std::vector<ClipboardItem>& items) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto& item : items) {
        hash_append(hash, item.ref);
        hash_append(hash, item.preview);
        hash_append(hash, std::to_string(item.size_bytes));
        hash_append(hash, std::to_string(item.captured_at_ms));
    }
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

std::string lowercase_ascii(std::string value) {
    for (auto& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool contains_algorithm(const std::vector<std::string>& algorithms, std::string_view expected) {
    return std::any_of(algorithms.begin(), algorithms.end(), [&](const auto& value) {
        return lowercase_ascii(value) == expected;
    });
}

std::string ranking_text(const ClipboardItem& item) {
    if (item.blob_path.empty()) return item.preview;
    std::ifstream input(item.blob_path, std::ios::binary);
    if (!input) return item.preview;
    std::string text(8192, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(input.gcount()));
    return text;
}

double local_match_bonus(std::string_view object, std::string_view input, std::uint64_t full_size) {
    if (object.empty() || input.empty() || input.find(object) == std::string_view::npos) return 0.0;
    const auto denominator = std::max<std::uint64_t>(full_size, input.size());
    const double coverage = std::clamp(static_cast<double>(object.size()) /
                                           static_cast<double>(denominator),
                                       0.0, 1.0);
    return std::clamp(0.01 + 0.04 * coverage, 0.0, 0.05);
}

constexpr std::size_t kMaximumUsageHints = 5;

}  // namespace

void record_batch_usage(UsageModel& usage, const DesktopDecisionBatch& batch, const ActionInstance& action) {
    if (batch.usage_context.kind == ContentKind::Unknown) return;
    record_usage(usage, batch.usage_context, usage_action_key(action, batch.request.snapshot));
}

bool mermaid_action_requires_generation(const ActionInstance& action) {
    if (action.kind != ActionKind::DrawMermaidDiagram) return false;
    const auto source = action.parameters.find("mermaid_source");
    return source == action.parameters.end() || source->second != "direct";
}

DesktopDecisionBatch build_desktop_decision(const DesktopDecisionInput& input) {
    DesktopDecisionBatch batch;
    batch.general_llm = input.general_llm;
    batch.request.protocol_version = 1;
    batch.request.request_id = input.request_id;

    auto& snapshot = batch.request.snapshot;
    snapshot.captured_at_ms = input.captured_at_ms;
    snapshot.focused_target_hash = input.focused_target_hash;
    snapshot.focused_app = input.focused_app;
    snapshot.focused_window_title = input.focused_window_title;
    snapshot.focused_current_directory = path_to_utf8_string(input.focused_current_directory);
    snapshot.clipboard_items = input.clipboard_items;
    std::stable_sort(snapshot.clipboard_items.begin(), snapshot.clipboard_items.end(), [](const ClipboardItem& left, const ClipboardItem& right) {
        return left.captured_at_ms > right.captured_at_ms;
    });
    if (snapshot.clipboard_items.size() > 1) {
        snapshot.clipboard_items.resize(1);
    }
    snapshot.clipboard_hash = snapshot_hash(snapshot.clipboard_items);
    snapshot.recent_paths = input.recent_paths;
    std::set<std::string> default_text_refs;
    std::set<std::string> default_image_refs;
    std::set<std::string> default_download_refs;
    const auto add_default = [&](const std::filesystem::path& path, std::string ref, std::string source,
                                 std::set<std::string>& refs) {
        if (path.empty()) return;
        std::error_code error;
        if (!std::filesystem::is_directory(path, error)) return;
        const auto duplicate=std::find_if(snapshot.recent_paths.begin(),snapshot.recent_paths.end(),[&](const auto& item){return item.path==path;});
        if (duplicate != snapshot.recent_paths.end()) {
            duplicate->last_seen_ms=input.captured_at_ms;
            duplicate->source=source;
            refs.insert(duplicate->ref);
        }
        else { refs.insert(ref); snapshot.recent_paths.push_back({.ref=std::move(ref),.path=path,.kind=PathKind::Directory,.last_seen_ms=input.captured_at_ms,.source=std::move(source),.exists=true}); }
    };
    add_default(input.default_text_directory,"default_text","configured-text",default_text_refs);
    add_default(input.default_image_directory,"default_image","configured-image",default_image_refs);
    add_default(input.downloads.resume_directory,"default_download","configured-download",default_download_refs);

    batch.catalog = build_catalog(snapshot, input.prompt_templates, input.general_llm);
    if (!snapshot.clipboard_items.empty()) {
        const auto& item = snapshot.clipboard_items.front();
        batch.ranking_context.input_kind = item.kind;
        // Image blobs are binary; text detectors would find nonsense in them.
        const auto local_text = item.kind == ContentKind::Image ? std::string{} : ranking_text(item);
        const auto signals = detect_fast_content(item.kind, local_text);
        batch.usage_context = UsageContext{
            .kind = item.kind,
            .signals = fast_signal_tags(signals),
            .app = input.focused_app,
            .now_ms = input.captured_at_ms,
        };
        // Content shapes recognised by the catalog also describe the usage
        // context, e.g. a user who always converts colors to RGB.
        for (const auto& [kind, tag] : std::initializer_list<std::pair<ActionKind, const char*>>{
                 {ActionKind::Graph, "numbers"}, {ActionKind::CopyColorRgb, "color"}, {ActionKind::DecodeJwt, "jwt"},
                 {ActionKind::GenerateUuid, "uuid"}, {ActionKind::Base64Decode, "base64"},
                 {ActionKind::ToMarkdownTable, "table"}, {ActionKind::CleanUrl, "tracking_url"}}) {
            if (std::any_of(batch.catalog.actions.begin(), batch.catalog.actions.end(),
                            [&](const ActionInstance& action) { return action.kind == kind; })) {
                batch.usage_context.signals.emplace_back(tag);
            }
        }
        if (item.kind == ContentKind::Image) {
            batch.ranking_context.local_action_bonus[ActionKind::AnnotateImage] = 0.04;
        }
        if (const auto graph = std::find_if(batch.catalog.actions.begin(), batch.catalog.actions.end(),
                                            [](const ActionInstance& action) { return action.kind == ActionKind::Graph; });
            graph != batch.catalog.actions.end()) {
            batch.ranking_context.local_action_bonus[ActionKind::Graph] = 0.05;
            batch.ranking_context.local_action_bonus_by_id[graph->id] = 0.02;
        }
        if (signals.date_time_value.has_value()) {
            const auto bonus = local_match_bonus(signals.date_time_value->original, local_text, item.size_bytes);
            for (const auto kind : {ActionKind::ConvertTimezone, ActionKind::ToUnixTimestamp,
                                    ActionKind::CopyNormalizedDateTime}) {
                batch.ranking_context.local_action_bonus[kind] = bonus;
            }
        }
        if (signals.ip) {
            for (const auto& action : batch.catalog.actions) {
                if (action.kind != ActionKind::PingIp && action.kind != ActionKind::TraceRouteIp &&
                    action.kind != ActionKind::ReverseDnsIp && action.kind != ActionKind::DigIp &&
                    action.kind != ActionKind::NetworkDiagnosticReport) continue;
                const auto target = action.parameters.find("ip");
                if (target != action.parameters.end()) {
                    batch.ranking_context.local_action_bonus[action.kind] =
                        local_match_bonus(target->second, local_text, item.size_bytes);
                }
            }
        }
        if (signals.code) {
            for (const auto& action : batch.catalog.actions) {
                if (action.kind == ActionKind::TransformText &&
                    action.parameters.find("content_signal") != action.parameters.end() &&
                    action.parameters.at("content_signal") == "code") {
                    batch.ranking_context.local_action_bonus_by_id[action.id] = 0.03;
                    break;
                }
            }
        }
        const auto newest_prompt = std::find_if(input.prompt_templates.rbegin(), input.prompt_templates.rend(),
                                                [](const PromptTemplate& prompt) { return prompt.enabled; });
        if (newest_prompt != input.prompt_templates.rend()) {
            for (const auto& action : batch.catalog.actions) {
                const auto template_id = action.parameters.find("template_id");
                if (action.kind == ActionKind::TransformText && template_id != action.parameters.end() &&
                    template_id->second == newest_prompt->id) {
                    // Keep the latest enabled template represented even when
                    // many prompt actions compete for the fixed Djev payload cap.
                    batch.ranking_context.candidate_priority_by_id[action.id] = 0.05;
                    break;
                }
            }
        }
    }
    const bool show_sha256 = contains_algorithm(input.hash.default_algorithms, "sha256");
    const bool show_sha512 = contains_algorithm(input.hash.default_algorithms, "sha512");
    std::erase_if(batch.catalog.actions, [&](const ActionInstance& action) {
        return (action.kind == ActionKind::HashSha256 && !show_sha256) ||
               (action.kind == ActionKind::HashSha512 && !show_sha512);
    });
    for (auto& action : batch.catalog.actions) {
        if (action.kind == ActionKind::SendEmail) {
            action.enabled = input.direct_send_available;
        }
        if (action.kind == ActionKind::ConvertTimezone || action.kind == ActionKind::ToUnixTimestamp ||
            action.kind == ActionKind::CopyNormalizedDateTime) {
            if (!input.date_time.source_zone.empty()) {
                action.parameters["source_zone"] = input.date_time.source_zone;
            }
        }
        if (action.kind == ActionKind::ConvertTimezone) {
            if (!input.date_time.target_zone.empty()) {
                action.parameters["target_zone"] = input.date_time.target_zone;
            }
        }
        const bool text_save = action.kind == ActionKind::SaveTextFile || action.kind == ActionKind::SaveUrlFile ||
                               action.kind == ActionKind::SaveEmailFile || action.kind == ActionKind::SaveJsonFile ||
                               action.kind == ActionKind::SaveJsonPrettyFile || action.kind == ActionKind::SaveResumeFile;
        if (default_text_refs.contains(action.target_ref) && !text_save) action.enabled = false;
        if (default_image_refs.contains(action.target_ref) && action.kind != ActionKind::SaveImageFile) action.enabled = false;
        const bool url_download = action.kind == ActionKind::DownloadUrl || action.kind == ActionKind::SaveUrlFile;
        if (default_download_refs.contains(action.target_ref) && !url_download) action.enabled = false;
    }
    if (input.usage != nullptr && !snapshot.clipboard_items.empty()) {
        for (const auto& action : batch.catalog.actions) {
            const double bonus = usage_bonus(*input.usage, batch.usage_context, usage_action_key(action, snapshot));
            if (bonus > 0.0) batch.ranking_context.usage_bonus_by_id[action.id] = bonus;
        }
    }
    const auto candidates = select_djev_candidates(batch.catalog, snapshot, 26, batch.ranking_context);
    snapshot.available_actions = candidates.actions;
    if (input.usage != nullptr && !snapshot.clipboard_items.empty()) {
        // Tell Djev which offered actions the user habitually picks here, so
        // the model can weigh frequency against what the content suggests.
        for (const auto& habit : usage_habits(*input.usage, batch.usage_context, 12)) {
            if (snapshot.usage_hints.size() >= kMaximumUsageHints) break;
            if (habit.share < 0.05) continue;
            const auto offered = std::find_if(snapshot.available_actions.begin(), snapshot.available_actions.end(),
                                              [&](const ActionInstance& action) {
                                                  return usage_action_key(action, snapshot) == habit.action_key;
                                              });
            if (offered == snapshot.available_actions.end()) continue;
            snapshot.usage_hints.push_back({offered->id, habit.share, habit.count});
        }
    }
    return batch;
}

}  // namespace pastit
