#include "executor/fast_action_executor.hpp"

#include "util/json.hpp"
#include "render/renderer_service.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace pastit {
namespace {

std::vector<std::byte> bytes_from_string(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (unsigned char ch : text) {
        out.push_back(static_cast<std::byte>(ch));
    }
    return out;
}

ExecutionResult result_for(const ActionInstance& action,
                           const ExecutionContext& context,
                           ExecutionStatus status,
                           std::string message) {
    ExecutionResult result;
    result.request_id = context.request_id;
    result.action_id = action.id;
    result.status = status;
    result.message = std::move(message);
    return result;
}

std::optional<std::string> record_text_clipboard(ExecutionContext& context,
                                                 std::string text,
                                                 ContentKind kind = ContentKind::Text) {
    const auto item = context.clipboard_store.put(ClipboardData{
        .mime_types = {"text/plain"},
        .bytes = bytes_from_string(text),
        .kind = kind,
        .source_app = "pastit",
        .captured_at_ms = context.now_ms,
    });
    return item.ref;
}

std::string source_text(const ActionInstance& action, ExecutionContext& context) {
    if (action.source_ref.empty()) {
        return {};
    }
    return context.clipboard_store.read_text(action.source_ref);
}

std::string parameter_or(const ActionInstance& action, std::string_view key, std::string fallback = {}) {
    const auto found = action.parameters.find(std::string{key});
    return found == action.parameters.end() ? std::move(fallback) : found->second;
}

std::filesystem::path target_dir_for(const ActionInstance& action, const ExecutionContext& context) {
    if (const auto confirmed = action.parameters.find("confirmed_destination");
        confirmed != action.parameters.end() && !confirmed->second.empty()) {
        return confirmed->second;
    }
    if (const auto destination = action.parameters.find("destination_directory");
        destination != action.parameters.end() && !destination->second.empty()) {
        return destination->second;
    }
    const auto target = context.path_history.find(action.target_ref);
    if (!target.has_value() || target->kind != PathKind::Directory) {
        throw std::runtime_error("target directory not found");
    }
    return target->path;
}

std::filesystem::path available_generated_path(std::filesystem::path path) {
    if (!std::filesystem::exists(path)) {
        return path;
    }
    const auto parent = path.parent_path();
    const auto stem = path.stem().string();
    const auto extension = path.extension().string();
    for (std::size_t index = 2;; ++index) {
        auto candidate = parent / (stem + " (" + std::to_string(index) + ")" + extension);
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
}

void write_text_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("failed to open output file");
    }
    output << text;
}

void record_output_path(ExecutionContext& context, const std::filesystem::path& path, ExecutionResult& result) {
    result.output_path = path;
    result.output_paths.push_back(path);
    result.output_clipboard_ref = record_text_clipboard(context, path.string(), ContentKind::Path);
    context.path_history.observe(path.string(), "pastit", context.now_ms);
}

std::optional<NetworkProbe> probe_for(ActionKind kind) {
    switch (kind) {
        case ActionKind::PingIp:
            return NetworkProbe::Ping;
        case ActionKind::TraceRouteIp:
            return NetworkProbe::TraceRoute;
        case ActionKind::ReverseDnsIp:
            return NetworkProbe::ReverseDns;
        case ActionKind::DigIp:
            return NetworkProbe::Dig;
        default:
            return std::nullopt;
    }
}

HashAlgorithm hash_algorithm_for(ActionKind kind) {
    return kind == ActionKind::HashSha512 ? HashAlgorithm::Sha512 : HashAlgorithm::Sha256;
}

std::string hash_algorithm_label(HashAlgorithm algorithm) {
    return algorithm == HashAlgorithm::Sha512 ? "SHA-512" : "SHA-256";
}

std::optional<std::int64_t> parse_epoch_parameter(const ActionInstance& action) {
    const auto value = parameter_or(action, "epoch_seconds");
    if (value.empty()) {
        return std::nullopt;
    }
    try {
        return std::stoll(value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string trim_copy(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string normalize_mermaid(std::string source) {
    auto trimmed = trim_copy(std::move(source));
    if (trimmed.starts_with("graph ") || trimmed.starts_with("flowchart ") ||
        trimmed.starts_with("sequenceDiagram") || trimmed.starts_with("classDiagram") ||
        trimmed.starts_with("stateDiagram") || trimmed.starts_with("stateDiagram-v2") ||
        trimmed.starts_with("erDiagram") || trimmed.starts_with("gantt") ||
        trimmed.starts_with("journey") || trimmed.starts_with("pie") ||
        trimmed.starts_with("gitGraph") || trimmed.starts_with("mindmap") ||
        trimmed.starts_with("timeline")) {
        if (!trimmed.ends_with('\n')) {
            trimmed.push_back('\n');
        }
        return trimmed;
    }
    std::string normalized = trimmed;
    for (std::size_t pos = 0; (pos = normalized.find("->", pos)) != std::string::npos; pos += 3) {
        normalized.replace(pos, 2, "-->");
    }
    return "graph TD\n  " + normalized + "\n";
}

RendererResultKind explain_kind_for(const ActionInstance& action) {
    if (action.kind == ActionKind::ExplainCode) {
        return RendererResultKind::ExplainCode;
    }
    if (parameter_or(action, "content_signal") == "code") {
        return RendererResultKind::ExplainCode;
    }
    return RendererResultKind::ExplainText;
}

bool is_async_action(ActionKind kind) {
    switch (kind) {
        case ActionKind::PingIp:
        case ActionKind::TraceRouteIp:
        case ActionKind::ReverseDnsIp:
        case ActionKind::DigIp:
        case ActionKind::NetworkDiagnosticReport:
        case ActionKind::CloneGithubHttps:
        case ActionKind::CloneGithubSsh:
        case ActionKind::HashSha256:
        case ActionKind::HashSha512:
        case ActionKind::DrawMermaidDiagram:
        case ActionKind::GenerateQr:
            return true;
        default:
            return false;
    }
}

}  // namespace

FastActionExecutor::FastActionExecutor(FastActionServices& services, RendererSettings renderer_settings)
    : services_(services), renderer_settings_(std::move(renderer_settings)) {}

std::string FastActionExecutor::next_job_id() {
    return "fast-action-" + std::to_string(++next_job_id_);
}

ExecutionResult FastActionExecutor::start_async(ActionInstance action, ExecutionContext& context) {
    if (action.kind == ActionKind::DrawMermaidDiagram || action.kind == ActionKind::GenerateQr) {
        const bool mermaid = action.kind == ActionKind::DrawMermaidDiagram;
        const auto source = mermaid ? source_text(action, context)
                                    : parameter_or(action, "payload", source_text(action, context));
        const auto payload = mermaid ? normalize_mermaid(source) : source;
        const auto kind = mermaid ? RendererResultKind::Mermaid : RendererResultKind::Qr;
        return start_renderer_async(std::move(action), context, kind, source, payload,
                                    mermaid ? "Mermaid rendering started" : "QR rendering started");
    }

    const auto job_id = next_job_id();
    auto started = result_for(action, context, ExecutionStatus::Executing, "action is running");
    started.job_id = job_id;

    if (probe_for(action.kind).has_value() || action.kind == ActionKind::NetworkDiagnosticReport) {
        const auto target = parameter_or(action, "ip", source_text(action, context));
        if (target.empty()) {
            return result_for(action, context, ExecutionStatus::Failed, "network target missing");
        }
        network_report_.emplace();
        network_report_->start(action.id, target);
        started.message = "network probe is running";
        jobs_.emplace(job_id, AsyncJob{std::async(std::launch::async, [this, job_id, request_id = context.request_id,
                                                                         action, target] {
            AsyncResult result;
            result.job_id = job_id;
            result.request_id = request_id;
            result.action = action;
            result.status = ExecutionStatus::Completed;
            result.message = "network report ready";
            result.network_target = target;
            std::vector<NetworkProbe> probes;
            if (const auto single = probe_for(action.kind)) {
                probes.push_back(*single);
            } else {
                probes = {NetworkProbe::Ping, NetworkProbe::TraceRoute, NetworkProbe::ReverseDns, NetworkProbe::Dig};
            }
            for (const auto probe : probes) {
                const auto started_at = std::chrono::steady_clock::now();
                auto output = services_.run_network_probe(probe, target);
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started_at).count();
                result.network_results.push_back(NetworkProbeResult{
                    .probe = probe,
                    .output = std::move(output),
                    .elapsed_ms = static_cast<int>(elapsed),
                });
            }
            return result;
        }), action, context.request_id, target, HashAlgorithm::Sha256, {}});
        return started;
    }

    if (action.kind == ActionKind::CloneGithubHttps || action.kind == ActionKind::CloneGithubSsh) {
        const auto url_key = action.kind == ActionKind::CloneGithubHttps ? "https_url" : "ssh_url";
        const auto url = parameter_or(action, url_key);
        const auto repo = parameter_or(action, "github_repo");
        const auto destination_root = target_dir_for(action, context);
        const auto destination = repo.empty() ? destination_root : destination_root / repo;
        started.message = "clone is running";
        started.output_path = destination;
        started.output_paths.push_back(destination);
        jobs_.emplace(job_id, AsyncJob{std::async(std::launch::async, [this, job_id, request_id = context.request_id,
                                                                         action, url, destination] {
            AsyncResult result;
            result.job_id = job_id;
            result.request_id = request_id;
            result.action = action;
            const auto output = services_.clone_repository(url, destination);
            result.status = output.exit_code == 0 ? ExecutionStatus::Completed : ExecutionStatus::Failed;
            result.message = output.exit_code == 0 ? "cloned repository" : "clone failed";
            result.output_path = destination;
            result.error = output.stderr_text;
            return result;
        }), action, context.request_id, {}, HashAlgorithm::Sha256, {}});
        return started;
    }

    const auto algorithm = hash_algorithm_for(action.kind);
    const std::filesystem::path path = parameter_or(action, "path", source_text(action, context));
    started.message = hash_algorithm_label(algorithm) + " hash is running";
    jobs_.emplace(job_id, AsyncJob{std::async(std::launch::async, [this, job_id, request_id = context.request_id,
                                                                     action, algorithm, path] {
        AsyncResult result;
        result.job_id = job_id;
        result.request_id = request_id;
        result.action = action;
        result.hash_algorithm = algorithm;
        result.hash_path = path;
        result.hash_digest = services_.hash_file(path, algorithm);
        result.status = result.hash_digest.empty() ? ExecutionStatus::Failed : ExecutionStatus::Completed;
        result.message = result.hash_digest.empty() ? "hash unavailable" : hash_algorithm_label(algorithm) + " hash ready";
        result.clipboard_text = result.hash_digest;
        result.clipboard_kind = ContentKind::Text;
        return result;
    }), action, context.request_id, {}, algorithm, path});
    return started;
}

ExecutionResult FastActionExecutor::start_renderer_async(ActionInstance action, ExecutionContext& context,
                                                         RendererResultKind kind, std::string source,
                                                         std::string payload, std::string started_message,
                                                         std::optional<GeneratedMermaidSource> generated_mermaid) {
    const auto job_id = next_job_id();
    auto started = result_for(action, context, ExecutionStatus::Executing, std::move(started_message));
    started.job_id = job_id;

    renderer_result_.emplace();
    renderer_result_->prepare(RendererResult{
        .action_id = action.id,
        .kind = kind,
        .source = source,
        .payload = payload,
        .available = false,
        .status = RendererResultStatus::Rendering,
        .output_path = std::nullopt,
        .error = "Rendering...",
        .generated_mermaid = generated_mermaid,
    });
    const bool mermaid = kind == RendererResultKind::Mermaid;
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto output = std::filesystem::temp_directory_path() /
        ("pastit-render-" + unique + "-" + job_id + (mermaid ? ".svg" : ".png"));
    jobs_.emplace(job_id, AsyncJob{std::async(std::launch::async,
        [this, job_id, request_id = context.request_id, action, source = std::move(source),
         payload = std::move(payload), generated_mermaid = std::move(generated_mermaid),
         kind, output, settings = renderer_settings_] {
            ExternalRendererService service(services_, settings);
            const auto rendered = kind == RendererResultKind::Mermaid
                ? service.render_mermaid(payload, output)
                : service.render_qr(payload, output);
            const bool valid = rendered.success && rendered_output_decodes(kind, output);
            if (!valid) {
                std::error_code ignored;
                std::filesystem::remove(output, ignored);
            }
            AsyncResult result;
            result.job_id = job_id;
            result.request_id = request_id;
            result.action = action;
            result.status = ExecutionStatus::Completed;
            result.message = valid ? "Rendered preview ready" : "Source ready; rendered preview unavailable";
            result.output_path = valid ? std::optional<std::filesystem::path>{output} : std::nullopt;
            result.renderer = RendererResult{
                .action_id = action.id,
                .kind = kind,
                .source = source,
                .payload = payload,
                .available = valid,
                .status = valid ? RendererResultStatus::Ready
                                : (rendered.available ? RendererResultStatus::Failed : RendererResultStatus::Unavailable),
                .output_path = result.output_path,
                .error = valid ? std::string{} :
                    (rendered.success ? "Renderer did not produce a decodable output" : rendered.status),
                .generated_mermaid = generated_mermaid,
            };
            return result;
        }), action, context.request_id, {}, HashAlgorithm::Sha256, {}});
    return started;
}

ExecutionResult FastActionExecutor::render_generated_mermaid(const ActionInstance& action, ExecutionContext& context,
                                                             std::string clipboard_source_ref,
                                                             std::string original_clipboard_source,
                                                             const MermaidNormalizationResult& source) {
    GeneratedMermaidSource generated{
        .clipboard_source_ref = std::move(clipboard_source_ref),
        .original_clipboard_source = std::move(original_clipboard_source),
        .raw_model_response = source.raw_source,
        .normalized_mermaid_source = source.normalized_source,
    };
    if (source.ok && !source.normalized_source.empty()) {
        return start_renderer_async(action, context, RendererResultKind::Mermaid, source.raw_source,
                                    source.normalized_source, "Mermaid rendering started", std::move(generated));
    }

    renderer_result_.emplace();
    renderer_result_->prepare(RendererResult{
        .action_id = action.id,
        .kind = RendererResultKind::Mermaid,
        .source = source.raw_source,
        .payload = {},
        .available = false,
        .status = RendererResultStatus::Failed,
        .output_path = std::nullopt,
        .error = source.error.empty() ? "Mermaid source normalization failed" : source.error,
        .generated_mermaid = std::move(generated),
    });
    auto result = result_for(action, context, ExecutionStatus::Completed,
                             "Mermaid source fallback ready: " + renderer_result_->active().error);
    result.output_clipboard_ref = record_text_clipboard(context, renderer_result_->copy_text());
    return result;
}

ExecutionResult FastActionExecutor::finish_async(AsyncResult async, ExecutionContext& context) {
    ExecutionResult result;
    result.request_id = async.request_id;
    result.action_id = async.action.id;
    result.status = async.status;
    result.message = async.message;
    result.job_id = async.job_id;
    if (async.output_path.has_value()) {
        result.output_path = *async.output_path;
        result.output_paths.push_back(*async.output_path);
    }

    switch (async.action.kind) {
        case ActionKind::DrawMermaidDiagram:
        case ActionKind::GenerateQr:
            if (async.renderer.has_value()) {
                renderer_result_.emplace();
                renderer_result_->prepare(std::move(*async.renderer));
            } else {
                const auto kind = async.action.kind == ActionKind::DrawMermaidDiagram
                    ? RendererResultKind::Mermaid : RendererResultKind::Qr;
                auto previous = renderer_result_.has_value() ? renderer_result_->active() : RendererResult{};
                renderer_result_.emplace();
                previous.action_id = async.action.id;
                previous.kind = kind;
                previous.available = false;
                previous.status = RendererResultStatus::Failed;
                previous.output_path.reset();
                previous.error = async.error.empty() ? async.message : async.error;
                renderer_result_->prepare(std::move(previous));
            }
            result.output_clipboard_ref = record_text_clipboard(context, renderer_result_->copy_text());
            break;
        case ActionKind::PingIp:
        case ActionKind::TraceRouteIp:
        case ActionKind::ReverseDnsIp:
        case ActionKind::DigIp:
        case ActionKind::NetworkDiagnosticReport:
            network_report_.emplace();
            network_report_->start(async.action.id, async.network_target);
            for (auto& probe : async.network_results) {
                network_report_->add_probe(probe.probe, std::move(probe.output), probe.elapsed_ms);
            }
            if (result.status == ExecutionStatus::Completed) {
                network_report_->complete();
                result.output_clipboard_ref = record_text_clipboard(context, network_report_->copy_text());
            } else {
                network_report_->fail(async.error.empty() ? async.message : async.error);
            }
            break;
        case ActionKind::CloneGithubHttps:
        case ActionKind::CloneGithubSsh:
            if (result.status == ExecutionStatus::Completed && async.output_path.has_value()) {
                result.output_clipboard_ref = record_text_clipboard(context, async.output_path->string(), ContentKind::Path);
                context.path_history.observe(async.output_path->string(), "pastit", context.now_ms);
            }
            break;
        case ActionKind::HashSha256:
        case ActionKind::HashSha512:
            if (result.status == ExecutionStatus::Completed) {
                hash_result_.emplace();
                hash_result_->complete(HashResult{
                    .action_id = async.action.id,
                    .algorithm = async.hash_algorithm,
                    .path = async.hash_path,
                    .digest = async.hash_digest,
                    .status = HashResultStatus::Completed,
                    .error = {},
                });
                result.output_clipboard_ref = record_text_clipboard(context, async.hash_digest);
            } else {
                hash_result_.emplace();
                hash_result_->fail(async.action.id, async.hash_algorithm, async.hash_path,
                                   async.error.empty() ? async.message : async.error);
            }
            break;
        default:
            break;
    }
    return result;
}

std::vector<ExecutionResult> FastActionExecutor::poll(ExecutionContext& context) {
    std::vector<ExecutionResult> completed;
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        if (it->second.future.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) {
            ++it;
            continue;
        }
        try {
            auto async = it->second.future.get();
            completed.push_back(finish_async(std::move(async), context));
        } catch (const std::exception& error) {
            AsyncResult async;
            async.job_id = it->first;
            async.request_id = it->second.request_id;
            async.action = it->second.action;
            async.network_target = it->second.network_target;
            async.hash_algorithm = it->second.hash_algorithm;
            async.hash_path = it->second.hash_path;
            async.status = ExecutionStatus::Failed;
            async.message = error.what();
            async.error = error.what();
            completed.push_back(finish_async(std::move(async), context));
        } catch (...) {
            AsyncResult async;
            async.job_id = it->first;
            async.request_id = it->second.request_id;
            async.action = it->second.action;
            async.network_target = it->second.network_target;
            async.hash_algorithm = it->second.hash_algorithm;
            async.hash_path = it->second.hash_path;
            async.status = ExecutionStatus::Failed;
            async.message = "async action failed with an unknown error";
            async.error = async.message;
            completed.push_back(finish_async(std::move(async), context));
        }
        it = jobs_.erase(it);
    }
    return completed;
}

ExecutionResult FastActionExecutor::execute(const ActionInstance& action, ExecutionContext& context) {
    try {
        if (is_async_action(action.kind)) {
            return start_async(action, context);
        }
        switch (action.kind) {
            case ActionKind::ExtractContactInfo: {
                contact_result_.emplace();
                const auto json = parameter_or(action, "contact_json");
                if (!contact_result_->set_from_json(action.id, json)) {
                    return result_for(action, context, ExecutionStatus::Failed, "contact fields are missing");
                }
                auto result = result_for(action, context, ExecutionStatus::Completed, "contact info ready");
                result.output_clipboard_ref = record_text_clipboard(context, contact_result_->copy_text(), ContentKind::Text);
                return result;
            }
            case ActionKind::CopyContactAsJson: {
                const auto json = parameter_or(action, "contact_json");
                contact_result_.emplace();
                (void)contact_result_->set_from_json(action.id, json);
                auto result = result_for(action, context, ExecutionStatus::Completed, "copied contact JSON");
                result.output_clipboard_ref = record_text_clipboard(context, json, ContentKind::Json);
                return result;
            }
            case ActionKind::SaveContactAsJson: {
                const auto json = parameter_or(action, "contact_json");
                const auto filename = action.filename.empty() ? "contact.json" : action.filename;
                const auto output = available_generated_path(target_dir_for(action, context) / filename);
                write_text_file(output, json);
                contact_result_.emplace();
                (void)contact_result_->set_from_json(action.id, json);
                auto result = result_for(action, context, ExecutionStatus::Completed, "saved contact JSON");
                record_output_path(context, output, result);
                return result;
            }
            case ActionKind::CopyContactField: {
                auto result = result_for(action, context, ExecutionStatus::Completed, "copied contact field");
                result.output_clipboard_ref = record_text_clipboard(context, parameter_or(action, "field_value"));
                return result;
            }
            case ActionKind::OpenTerminalAtPath: {
                const auto directory = parameter_or(action, "working_directory", parameter_or(action, "path"));
                const auto ok = services_.open_terminal(directory);
                return result_for(action, context, ok ? ExecutionStatus::Completed : ExecutionStatus::Failed,
                                  ok ? "opened terminal" : "terminal launcher failed");
            }
            case ActionKind::PingIp:
            case ActionKind::TraceRouteIp:
            case ActionKind::ReverseDnsIp:
            case ActionKind::DigIp:
            case ActionKind::NetworkDiagnosticReport: {
                const auto target = parameter_or(action, "ip", source_text(action, context));
                if (target.empty()) {
                    return result_for(action, context, ExecutionStatus::Failed, "network target missing");
                }
                network_report_.emplace();
                network_report_->start(action.id, target);
                std::vector<NetworkProbe> probes;
                if (const auto single = probe_for(action.kind)) {
                    probes.push_back(*single);
                } else {
                    probes = {NetworkProbe::Ping, NetworkProbe::TraceRoute, NetworkProbe::ReverseDns, NetworkProbe::Dig};
                }
                for (const auto probe : probes) {
                    const auto started = std::chrono::steady_clock::now();
                    auto output = services_.run_network_probe(probe, target);
                    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started).count();
                    network_report_->add_probe(probe, std::move(output), static_cast<int>(elapsed));
                }
                network_report_->complete();
                auto result = result_for(action, context, ExecutionStatus::Completed, "network report ready");
                result.output_clipboard_ref = record_text_clipboard(context, network_report_->copy_text());
                return result;
            }
            case ActionKind::CloneGithubHttps:
            case ActionKind::CloneGithubSsh: {
                const auto url_key = action.kind == ActionKind::CloneGithubHttps ? "https_url" : "ssh_url";
                const auto url = parameter_or(action, url_key);
                const auto repo = parameter_or(action, "github_repo");
                const auto destination_root = target_dir_for(action, context);
                const auto destination = repo.empty() ? destination_root : destination_root / repo;
                const auto output = services_.clone_repository(url, destination);
                auto result = result_for(action, context, output.exit_code == 0 ? ExecutionStatus::Completed : ExecutionStatus::Failed,
                                         output.exit_code == 0 ? "cloned repository" : "clone failed");
                if (output.exit_code == 0) {
                    record_output_path(context, destination, result);
                }
                return result;
            }
            case ActionKind::CopyGithubHttpsUrl:
            case ActionKind::CopyGithubSshUrl: {
                const auto key = action.kind == ActionKind::CopyGithubHttpsUrl ? "https_url" : "ssh_url";
                auto result = result_for(action, context, ExecutionStatus::Completed, "copied GitHub URL");
                result.output_clipboard_ref = record_text_clipboard(context, parameter_or(action, key), ContentKind::Url);
                return result;
            }
            case ActionKind::ConvertTimezone:
            case ActionKind::ToUnixTimestamp:
            case ActionKind::CopyNormalizedDateTime: {
                const auto original = parameter_or(action, "original", source_text(action, context));
                const auto source_zone = parameter_or(action, "source_zone", "UTC");
                const auto target_zone = parameter_or(action, "target_zone", "UTC");
                std::optional<std::int64_t> epoch = parse_epoch_parameter(action);
                if (!epoch.has_value() && action.kind != ActionKind::CopyNormalizedDateTime) {
                    epoch = services_.parse_datetime(original, source_zone);
                }
                if (!epoch.has_value() && action.kind != ActionKind::CopyNormalizedDateTime) {
                    date_time_result_.emplace();
                    date_time_result_->fail(action.id, "date/time parse failed");
                    return result_for(action, context, ExecutionStatus::Failed, "date/time parse failed");
                }
                std::string formatted;
                if (action.kind == ActionKind::ConvertTimezone) {
                    formatted = services_.format_datetime(*epoch, target_zone);
                } else if (action.kind == ActionKind::ToUnixTimestamp) {
                    formatted = std::to_string(*epoch);
                } else {
                    formatted = parameter_or(action, "normalized", original);
                }
                date_time_result_.emplace();
                date_time_result_->complete(DateTimeResult{
                    .action_id = action.id,
                    .original = original,
                    .source_zone = source_zone,
                    .target_zone = target_zone,
                    .epoch_seconds = epoch.value_or(0),
                    .formatted = formatted,
                    .status = DateTimeResultStatus::Completed,
                    .error = {},
                });
                auto result = result_for(action, context, ExecutionStatus::Completed, "date/time result ready");
                result.output_clipboard_ref = record_text_clipboard(context, formatted, ContentKind::DateTime);
                return result;
            }
            case ActionKind::HashSha256:
            case ActionKind::HashSha512: {
                const auto algorithm = hash_algorithm_for(action.kind);
                const std::filesystem::path path = parameter_or(action, "path", source_text(action, context));
                const auto digest = services_.hash_file(path, algorithm);
                if (digest.empty()) {
                    hash_result_.emplace();
                    hash_result_->fail(action.id, algorithm, path, "hash unavailable");
                    return result_for(action, context, ExecutionStatus::Failed, "hash unavailable");
                }
                hash_result_.emplace();
                hash_result_->complete(HashResult{
                    .action_id = action.id,
                    .algorithm = algorithm,
                    .path = path,
                    .digest = digest,
                    .status = HashResultStatus::Completed,
                    .error = {},
                });
                auto result = result_for(action, context, ExecutionStatus::Completed,
                                         hash_algorithm_label(algorithm) + " hash ready");
                result.output_clipboard_ref = record_text_clipboard(context, digest);
                return result;
            }
            case ActionKind::AnnotateImage: {
                renderer_result_.emplace();
                renderer_result_->prepare(RendererResult{
                    .action_id = action.id,
                    .kind = RendererResultKind::Annotation,
                    .source = action.source_ref,
                    .payload = action.source_ref,
                    .available = true,
                    .status = RendererResultStatus::Ready,
                    .output_path = std::nullopt,
                    .error = {},
                });
                return result_for(action, context, ExecutionStatus::Completed, "annotation state ready");
            }
            case ActionKind::ExplainText:
            case ActionKind::ExplainCode:
            case ActionKind::TransformText: {
                const auto source = source_text(action, context);
                const auto kind = explain_kind_for(action);
                renderer_result_.emplace();
                renderer_result_->prepare(RendererResult{
                    .action_id = action.id,
                    .kind = kind,
                    .source = source,
                    .payload = parameter_or(action, "template_name", kind == RendererResultKind::ExplainCode ? "Explain code" : "Explain text"),
                    .available = true,
                    .status = RendererResultStatus::Ready,
                    .output_path = std::nullopt,
                    .error = {},
                });
                return result_for(action, context, ExecutionStatus::Completed, "explain transform ready");
            }
            default:
                return result_for(action, context, ExecutionStatus::Unsupported, "unsupported fast action");
        }
    } catch (const std::exception& error) {
        return result_for(action, context, ExecutionStatus::Failed, error.what());
    }
}

}  // namespace pastit
