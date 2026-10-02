#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pasteit {

// Long work shown in the Tasks view (polish-app task queue). The owners of
// the work (downloads, async actions, LLM calls) report into it; the center
// keeps the list, the overall progress and a short log.
enum class TaskState { Queued, Running, Paused, Done, Failed, Canceled };

struct TaskEntry {
    std::string id;        // unique, e.g. "download:3" or "llm:7"
    std::string kind;      // download, network, llm, render, hash, clone, fetch, test
    std::string title;
    TaskState state = TaskState::Running;
    bool pausable = false;     // can pause / resume (downloads)
    bool cancellable = true;
    double weight = 1.0;       // share of the overall progress
    std::uint64_t done = 0;    // units done; total 0 means indeterminate
    std::uint64_t total = 0;
    std::int64_t started_ms = 0;
    std::int64_t finished_ms = 0;
    double rate = 0.0;         // units per second, smoothed
    std::string detail;        // result or error, one line
    // Resumable downloads: enough to start them again after a restart.
    std::string resume_url;
    std::string resume_path;
    std::string retry_action;  // action id to run again on Retry
    // Rate sampling.
    std::int64_t sample_ms = 0;
    std::uint64_t sample_done = 0;
};

enum class LogLevel { Debug, Info, Warning, Error };

struct LogEntry {
    std::int64_t time_ms = 0;
    LogLevel level = LogLevel::Info;
    std::string message;
};

class TaskCenter {
public:
    // Adds (or restarts) a running task and logs it.
    TaskEntry& start(std::string id, std::string kind, std::string title, std::int64_t now_ms, bool pausable = false,
                     bool cancellable = true, double weight = 1.0);
    TaskEntry* find(std::string_view id);
    const TaskEntry* find(std::string_view id) const;
    // Progress in units; the rate is an exponential moving average over >= 1 s windows.
    void progress(std::string_view id, std::uint64_t done, std::uint64_t total, std::int64_t now_ms);
    void set_state(std::string_view id, TaskState state, std::int64_t now_ms, std::string detail = {});
    void remove(std::string_view id);
    void clear_finished();

    const std::vector<TaskEntry>& tasks() const { return tasks_; }
    std::size_t active_count() const;   // queued, running or paused
    std::size_t failed_count() const;
    bool any_paused() const;
    // Weighted 0..1 over the tasks that are not canceled; 0 with nothing to do.
    double overall_progress() const;
    // Seconds left for a task, when the rate is known and it ran for 3 s or more.
    std::optional<double> eta_seconds(const TaskEntry& task, std::int64_t now_ms) const;

    void log(LogLevel level, std::string message, std::int64_t now_ms);
    const std::deque<LogEntry>& logs() const { return logs_; }  // oldest first
    std::size_t unseen_warnings() const { return unseen_warnings_; }
    // Lines logged since start (not capped), to append new ones to a file.
    std::uint64_t log_serial() const { return log_serial_; }
    void mark_logs_seen() { unseen_warnings_ = 0; }

    static constexpr std::size_t kMaxLogs = 500;

private:
    std::vector<TaskEntry> tasks_;
    std::deque<LogEntry> logs_;
    std::size_t unseen_warnings_ = 0;
    std::uint64_t log_serial_ = 0;
};

bool task_finished(TaskState state);
std::string task_state_name(TaskState state);
// "2 m left", "45 s left".
std::string format_eta(double seconds);

// Unfinished pausable tasks with resume data are kept across restarts and
// come back paused; other running work is not resumable and is not kept.
bool save_task_queue(const std::string& file, const TaskCenter& center);
void load_task_queue(const std::string& file, TaskCenter& center, std::int64_t now_ms);

}  // namespace pasteit
