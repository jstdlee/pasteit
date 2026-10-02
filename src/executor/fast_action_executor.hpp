#pragma once

#include "app/contact_result_state.hpp"
#include "app/date_time_result_state.hpp"
#include "app/hash_result_state.hpp"
#include "app/network_report_state.hpp"
#include "app/renderer_result_state.hpp"
#include "ai/mermaid_prompt.hpp"
#include "executor/action_executor.hpp"
#include "platform/fast_action_services.hpp"
#include "config/app_settings.hpp"

#include <future>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pasteit {

class FastActionExecutor {
public:
    explicit FastActionExecutor(FastActionServices& services, RendererSettings renderer_settings = {});
    void set_renderer_settings(RendererSettings settings) { renderer_settings_ = std::move(settings); }

    ExecutionResult execute(const ActionInstance& action, ExecutionContext& context);
    ExecutionResult render_generated_mermaid(const ActionInstance& action, ExecutionContext& context,
                                             std::string clipboard_source_ref, std::string original_clipboard_source,
                                             const MermaidNormalizationResult& source);
    std::vector<ExecutionResult> poll(ExecutionContext& context);

    // Background jobs still running, for the Tasks view.
    struct PendingJob {
        std::string job_id;
        ActionInstance action;
    };
    std::vector<PendingJob> pending_jobs() const;
    // Cancel from the Tasks view: the job cannot be interrupted, so its
    // result is dropped when it finishes.
    void abandon(std::string_view job_id);

    const std::optional<ContactResultState>& contact_result() const { return contact_result_; }
    const std::optional<DateTimeResultState>& date_time_result() const { return date_time_result_; }
    const std::optional<HashResultState>& hash_result() const { return hash_result_; }
    const std::optional<NetworkReportState>& network_report() const { return network_report_; }
    const std::optional<RendererResultState>& renderer_result() const { return renderer_result_; }

private:
    struct AsyncResult {
        std::string job_id;
        std::string request_id;
        ActionInstance action;
        ExecutionStatus status = ExecutionStatus::Completed;
        std::string message;
        std::string clipboard_text;
        ContentKind clipboard_kind = ContentKind::Text;
        std::optional<std::filesystem::path> output_path;
        std::vector<NetworkProbeResult> network_results;
        std::string network_target;
        HashAlgorithm hash_algorithm = HashAlgorithm::Sha256;
        std::filesystem::path hash_path;
        std::string hash_digest;
        std::string error;
        std::optional<RendererResult> renderer;
    };

    struct AsyncJob {
        std::future<AsyncResult> future;
        ActionInstance action;
        std::string request_id;
        std::string network_target;
        HashAlgorithm hash_algorithm = HashAlgorithm::Sha256;
        std::filesystem::path hash_path;
    };

    std::string next_job_id();
    ExecutionResult start_async(ActionInstance action, ExecutionContext& context);
    ExecutionResult start_renderer_async(ActionInstance action, ExecutionContext& context,
                                         RendererResultKind kind, std::string source,
                                         std::string payload, std::string started_message,
                                         std::optional<GeneratedMermaidSource> generated_mermaid = std::nullopt);
    ExecutionResult finish_async(AsyncResult result, ExecutionContext& context);

    FastActionServices& services_;
    RendererSettings renderer_settings_;
    std::optional<ContactResultState> contact_result_;
    std::optional<DateTimeResultState> date_time_result_;
    std::optional<HashResultState> hash_result_;
    std::optional<NetworkReportState> network_report_;
    std::optional<RendererResultState> renderer_result_;
    std::unordered_map<std::string, AsyncJob> jobs_;
    std::unordered_set<std::string> abandoned_;
    std::uint64_t next_job_id_ = 0;
};

}  // namespace pasteit
