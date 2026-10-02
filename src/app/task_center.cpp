#include "app/task_center.hpp"

#include "util/json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace pasteit {

bool task_finished(TaskState state) {
    return state == TaskState::Done || state == TaskState::Failed || state == TaskState::Canceled;
}

std::string task_state_name(TaskState state) {
    switch (state) {
        case TaskState::Queued: return "queued";
        case TaskState::Running: return "running";
        case TaskState::Paused: return "paused";
        case TaskState::Done: return "done";
        case TaskState::Failed: return "failed";
        case TaskState::Canceled: return "canceled";
    }
    return "unknown";
}

std::string format_eta(double seconds) {
    if (seconds < 60.0) return std::to_string(std::max(1, static_cast<int>(std::ceil(seconds)))) + " s left";
    if (seconds < 3600.0) return std::to_string(static_cast<int>(std::round(seconds / 60.0))) + " m left";
    return std::to_string(static_cast<int>(seconds / 3600.0)) + " h " + std::to_string(static_cast<int>(std::fmod(seconds, 3600.0) / 60.0)) +
           " m left";
}

TaskEntry& TaskCenter::start(std::string id, std::string kind, std::string title, std::int64_t now_ms, bool pausable,
                             bool cancellable, double weight) {
    auto* existing = find(id);
    if (existing == nullptr) {
        tasks_.push_back({});
        existing = &tasks_.back();
    }
    *existing = TaskEntry{.id = std::move(id), .kind = std::move(kind), .title = std::move(title), .state = TaskState::Running,
                          .pausable = pausable, .cancellable = cancellable, .weight = std::max(weight, 0.0),
                          .started_ms = now_ms, .sample_ms = now_ms};
    log(LogLevel::Info, "Started: " + existing->title, now_ms);
    return *existing;
}

TaskEntry* TaskCenter::find(std::string_view id) {
    const auto it = std::find_if(tasks_.begin(), tasks_.end(), [&](const TaskEntry& task) { return task.id == id; });
    return it == tasks_.end() ? nullptr : &*it;
}

const TaskEntry* TaskCenter::find(std::string_view id) const {
    const auto it = std::find_if(tasks_.begin(), tasks_.end(), [&](const TaskEntry& task) { return task.id == id; });
    return it == tasks_.end() ? nullptr : &*it;
}

void TaskCenter::progress(std::string_view id, std::uint64_t done, std::uint64_t total, std::int64_t now_ms) {
    auto* task = find(id);
    if (task == nullptr) return;
    task->done = done;
    task->total = total;
    const auto elapsed = now_ms - task->sample_ms;
    if (elapsed >= 1000) {
        const double instant = done >= task->sample_done ? static_cast<double>(done - task->sample_done) * 1000.0 / static_cast<double>(elapsed) : 0.0;
        task->rate = task->rate <= 0.0 ? instant : task->rate * 0.8 + instant * 0.2;
        task->sample_ms = now_ms;
        task->sample_done = done;
    }
}

void TaskCenter::set_state(std::string_view id, TaskState state, std::int64_t now_ms, std::string detail) {
    auto* task = find(id);
    if (task == nullptr || task->state == state) return;
    task->state = state;
    if (!detail.empty()) task->detail = std::move(detail);
    if (task_finished(state)) {
        task->finished_ms = now_ms;
        if (state == TaskState::Done && task->total > 0) task->done = task->total;
    }
    if (state == TaskState::Running) task->sample_ms = now_ms, task->sample_done = task->done;
    const auto level = state == TaskState::Failed ? LogLevel::Error : LogLevel::Info;
    std::string message = task_state_name(state);
    message[0] = static_cast<char>(message[0] - 'a' + 'A');
    message += ": " + task->title;
    if (!task->detail.empty() && task_finished(state)) message += " \xE2\x80\x94 " + task->detail;
    log(level, std::move(message), now_ms);
}

void TaskCenter::remove(std::string_view id) {
    std::erase_if(tasks_, [&](const TaskEntry& task) { return task.id == id; });
}

void TaskCenter::clear_finished() {
    std::erase_if(tasks_, [](const TaskEntry& task) { return task_finished(task.state); });
}

std::size_t TaskCenter::active_count() const {
    return static_cast<std::size_t>(std::count_if(tasks_.begin(), tasks_.end(), [](const TaskEntry& task) { return !task_finished(task.state); }));
}

std::size_t TaskCenter::failed_count() const {
    return static_cast<std::size_t>(std::count_if(tasks_.begin(), tasks_.end(), [](const TaskEntry& task) { return task.state == TaskState::Failed; }));
}

bool TaskCenter::any_paused() const {
    return std::any_of(tasks_.begin(), tasks_.end(), [](const TaskEntry& task) { return task.state == TaskState::Paused; });
}

double TaskCenter::overall_progress() const {
    double weights = 0.0;
    double done = 0.0;
    for (const auto& task : tasks_) {
        if (task.state == TaskState::Canceled) continue;
        weights += task.weight;
        if (task.state == TaskState::Done || task.state == TaskState::Failed) {
            done += task.weight;
        } else if (task.total > 0) {
            done += task.weight * std::min(1.0, static_cast<double>(task.done) / static_cast<double>(task.total));
        }
    }
    return weights > 0.0 ? done / weights : 0.0;
}

std::optional<double> TaskCenter::eta_seconds(const TaskEntry& task, std::int64_t now_ms) const {
    if (task.state != TaskState::Running || task.total == 0 || task.rate <= 0.001 || now_ms - task.started_ms < 3000) return std::nullopt;
    if (task.done >= task.total) return std::nullopt;
    return static_cast<double>(task.total - task.done) / task.rate;
}

void TaskCenter::log(LogLevel level, std::string message, std::int64_t now_ms) {
    logs_.push_back({.time_ms = now_ms, .level = level, .message = std::move(message)});
    ++log_serial_;
    while (logs_.size() > kMaxLogs) logs_.pop_front();
    if (level == LogLevel::Warning || level == LogLevel::Error) ++unseen_warnings_;
}

namespace {

std::string json_escape_text(std::string_view text) {
    std::string out;
    for (const char ch : text) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) continue;
                out += ch;
        }
    }
    return out;
}

}  // namespace

bool save_task_queue(const std::string& file, const TaskCenter& center) {
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << "{\"tasks\":[";
    bool first = true;
    for (const auto& task : center.tasks()) {
        if (!task.pausable || task_finished(task.state) || task.resume_url.empty()) continue;
        output << (first ? "" : ",") << "\n{\"id\":\"" << json_escape_text(task.id) << "\",\"kind\":\"" << json_escape_text(task.kind)
               << "\",\"title\":\"" << json_escape_text(task.title) << "\",\"url\":\"" << json_escape_text(task.resume_url)
               << "\",\"path\":\"" << json_escape_text(task.resume_path) << "\",\"done\":" << task.done << ",\"total\":" << task.total << "}";
        first = false;
    }
    output << "\n]}\n";
    return static_cast<bool>(output);
}

void load_task_queue(const std::string& file, TaskCenter& center, std::int64_t now_ms) {
    std::ifstream input(file, std::ios::binary);
    if (!input) return;
    std::stringstream buffer;
    buffer << input.rdbuf();
    const auto root = parse_json(buffer.str());
    const auto* tasks = root ? root->get("tasks") : nullptr;
    if (tasks == nullptr || tasks->array() == nullptr) return;
    for (const auto& item : *tasks->array()) {
        const auto text = [&](const char* key) {
            const auto* value = item.get(key);
            return value && value->string() ? *value->string() : std::string{};
        };
        if (text("id").empty() || text("url").empty()) continue;
        auto& task = center.start(text("id"), text("kind"), text("title"), now_ms, true, true);
        task.resume_url = text("url");
        task.resume_path = text("path");
        const auto* done = item.get("done");
        const auto* total = item.get("total");
        task.done = done ? static_cast<std::uint64_t>(done->number().value_or(0.0)) : 0;
        task.total = total ? static_cast<std::uint64_t>(total->number().value_or(0.0)) : 0;
        center.set_state(task.id, TaskState::Paused, now_ms, "Interrupted; resume to continue");
    }
}

}  // namespace pasteit
