#include "app/task_center.hpp"
#include "test_env.hpp"

#include <cassert>
#include <filesystem>
#include <string>

using namespace pasteit;

int main() {
    TaskCenter center;
    assert(center.overall_progress() == 0.0 && center.active_count() == 0);

    // Weighted overall progress: a big download at half, a small job done.
    center.start("download:1", "download", "big.iso", 0, true, true, 3.0);
    center.progress("download:1", 50, 100, 0);
    center.start("hash:1", "hash", "SHA-256", 0);
    center.set_state("hash:1", TaskState::Done, 500, "abc…");
    assert(center.active_count() == 1);
    const double overall = center.overall_progress();
    assert(overall > 0.62 && overall < 0.63);  // (3 * 0.5 + 1) / 4

    // Rate and ETA: 1 s windows, nothing before 3 s.
    center.progress("download:1", 60, 100, 1000);
    assert(!center.eta_seconds(*center.find("download:1"), 1000));
    center.progress("download:1", 70, 100, 3000);
    const auto eta = center.eta_seconds(*center.find("download:1"), 3000);
    assert(eta && *eta > 0.5 && *eta < 1.0);  // 30 units at a smoothed 49 per second
    assert(format_eta(45) == "45 s left" && format_eta(150) == "3 m left");

    // Canceled tasks leave the overall; failures count and log errors.
    center.start("llm:1", "llm", "Translate", 3000);
    center.set_state("llm:1", TaskState::Canceled, 3100);
    center.start("net:1", "network", "Ping", 3000);
    center.set_state("net:1", TaskState::Failed, 3200, "timeout");
    assert(center.failed_count() == 1 && center.unseen_warnings() == 1);
    assert(center.logs().back().level == LogLevel::Error && center.logs().back().message.find("timeout") != std::string::npos);
    center.mark_logs_seen();
    assert(center.unseen_warnings() == 0);

    // Persist: only unfinished resumable downloads come back, paused.
    center.find("download:1")->resume_url = "https://example.com/big.iso";
    center.find("download:1")->resume_path = "/tmp/big.iso";
    const auto file = std::filesystem::temp_directory_path() / "pasteit-task-queue-test.json";
    assert(save_task_queue(file.string(), center));
    TaskCenter restored;
    load_task_queue(file.string(), restored, 10'000);
    assert(restored.tasks().size() == 1);
    const auto* back = restored.find("download:1");
    assert(back && back->state == TaskState::Paused && back->resume_url == "https://example.com/big.iso" && back->done == 70);
    std::filesystem::remove(file);

    center.clear_finished();
    assert(center.tasks().size() == 1);
    for (int index = 0; index < 600; ++index) center.log(LogLevel::Debug, "line", index);
    assert(center.logs().size() == TaskCenter::kMaxLogs);
}
