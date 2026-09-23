#include "executor/fast_action_executor.hpp"

#include "ai/mermaid_prompt.hpp"
#include "executor/action_executor.hpp"
#include "storage/clipboard_store.hpp"
#include "storage/path_history.hpp"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (unsigned char ch : text) {
        out.push_back(static_cast<std::byte>(ch));
    }
    return out;
}

std::filesystem::path sample_png_fixture() {
    if (std::filesystem::exists("tests/fixtures/sample.png")) return "tests/fixtures/sample.png";
    return "../tests/fixtures/sample.png";
}

pastit::ClipboardItem put_text(pastit::ClipboardStore& store,
                               std::string text,
                               pastit::ContentKind kind = pastit::ContentKind::Text) {
    return store.put(pastit::ClipboardData{
        .mime_types = {"text/plain"},
        .bytes = bytes(text),
        .kind = kind,
        .source_app = "fast-action-executor-test",
        .captured_at_ms = 10,
    });
}

pastit::ActionInstance action(pastit::ActionKind kind, const std::string& source_ref = {}) {
    pastit::ActionInstance out;
    out.id = "action";
    out.kind = kind;
    out.source_ref = source_ref;
    out.label = "test";
    out.enabled = true;
    return out;
}

class FakeServices final : public pastit::FastActionServices {
public:
    bool open_terminal(const std::filesystem::path& directory) override {
        terminal_directory = directory;
        return true;
    }

    pastit::ProcessOutput run_network_probe(pastit::NetworkProbe probe, std::string_view host) override {
        probes.emplace_back(probe, std::string{host});
        switch (probe) {
            case pastit::NetworkProbe::Ping:
                return {.exit_code = 0, .stdout_text = "ping ok"};
            case pastit::NetworkProbe::TraceRoute:
                return {.exit_code = 0, .stdout_text = "trace ok"};
            case pastit::NetworkProbe::ReverseDns:
                return {.exit_code = 2, .stderr_text = "rdns miss"};
            case pastit::NetworkProbe::Dig:
                return {.exit_code = 0, .stdout_text = "dig ok"};
        }
        return {};
    }

    pastit::ProcessOutput clone_repository(std::string_view url,
                                           const std::filesystem::path& destination) override {
        clone_url = std::string{url};
        clone_destination = destination;
        return {.exit_code = 0, .stdout_text = "cloned"};
    }

    std::optional<std::int64_t> parse_datetime(std::string_view value,
                                               std::string_view source_zone) override {
        parsed_value = std::string{value};
        parsed_zone = std::string{source_zone};
        return 123;
    }

    std::string format_datetime(std::int64_t epoch_seconds, std::string_view target_zone) override {
        formatted_epoch = epoch_seconds;
        formatted_zone = std::string{target_zone};
        return "1970-01-01T00:02:03Z";
    }

    std::string hash_file(const std::filesystem::path& path, pastit::HashAlgorithm algorithm) override {
        hashed_path = path;
        hashed_algorithm = algorithm;
        return algorithm == pastit::HashAlgorithm::Sha512 ? "sha512-digest" : "sha256-digest";
    }

    pastit::ProcessOutput run_argv(const std::vector<std::string>& argv) override {
        argv_calls.push_back(argv);
        if (render_mode == RenderMode::MissingTool) {
            return {.exit_code = 127, .stderr_text = "tool unavailable"};
        }
        std::filesystem::path output;
        for (std::size_t i = 0; i + 1 < argv.size(); ++i) {
            if (argv[i] == "-o") output = argv[i + 1];
        }
        if ((render_mode == RenderMode::ProduceOutput || render_mode == RenderMode::CorruptOutput) && !output.empty()) {
            if (output.extension() == ".svg") {
                std::ofstream stream(output);
                stream << "<svg xmlns=\"http://www.w3.org/2000/svg\"><rect width=\"1\" height=\"1\"/></svg>";
            } else {
                std::filesystem::copy_file(sample_png_fixture(), output,
                                           std::filesystem::copy_options::overwrite_existing);
                // The shared 1x1 fixture has a bad IDAT CRC; repair it for a decodable renderer output.
                std::fstream png(output, std::ios::binary | std::ios::in | std::ios::out);
                if (render_mode == RenderMode::ProduceOutput) {
                    png.seekp(52);
                    png.write("\xEF\xA2\xA7\x5B", 4);
                }
            }
        }
        return {.exit_code = 0, .stdout_text = "argv ok"};
    }

    enum class RenderMode { ProduceOutput, CorruptOutput, MissingTool, SuccessWithoutOutput };
    RenderMode render_mode = RenderMode::ProduceOutput;

    std::filesystem::path terminal_directory;
    std::vector<std::pair<pastit::NetworkProbe, std::string>> probes;
    std::string clone_url;
    std::filesystem::path clone_destination;
    std::string parsed_value;
    std::string parsed_zone;
    std::int64_t formatted_epoch = 0;
    std::string formatted_zone;
    std::filesystem::path hashed_path;
    pastit::HashAlgorithm hashed_algorithm = pastit::HashAlgorithm::Sha256;
    std::vector<std::vector<std::string>> argv_calls;
};

class BlockingServices final : public pastit::FastActionServices {
public:
    bool open_terminal(const std::filesystem::path&) override { return true; }

    pastit::ProcessOutput run_network_probe(pastit::NetworkProbe probe, std::string_view host) override {
        {
            std::lock_guard lock(mutex_);
            ++started_;
            last_probe = probe;
            last_host = std::string{host};
        }
        changed_.notify_all();
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [&] { return released_; });
        return {.exit_code = 0, .stdout_text = "async probe done"};
    }

    pastit::ProcessOutput clone_repository(std::string_view, const std::filesystem::path&) override {
        return {.exit_code = 0, .stdout_text = "async clone done"};
    }

    std::optional<std::int64_t> parse_datetime(std::string_view, std::string_view) override { return 0; }
    std::string format_datetime(std::int64_t, std::string_view) override { return {}; }

    std::string hash_file(const std::filesystem::path&, pastit::HashAlgorithm) override {
        {
            std::lock_guard lock(mutex_);
            ++started_;
        }
        changed_.notify_all();
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [&] { return released_; });
        return "async-digest";
    }

    pastit::ProcessOutput run_argv(const std::vector<std::string>&) override {
        {
            std::lock_guard lock(mutex_);
            ++started_;
        }
        changed_.notify_all();
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [&] { return released_; });
        return {.exit_code = 127, .stderr_text = "renderer missing"};
    }

    void wait_started() {
        std::unique_lock lock(mutex_);
        const bool ready = changed_.wait_for(lock, std::chrono::seconds(2), [&] { return started_ > 0; });
        assert(ready);
    }

    void release() {
        {
            std::lock_guard lock(mutex_);
            released_ = true;
        }
        changed_.notify_all();
    }

    pastit::NetworkProbe last_probe = pastit::NetworkProbe::Ping;
    std::string last_host;

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    int started_ = 0;
    bool released_ = false;
};

class ThrowingServices final : public pastit::FastActionServices {
public:
    bool open_terminal(const std::filesystem::path&) override { return true; }
    pastit::ProcessOutput run_network_probe(pastit::NetworkProbe, std::string_view) override {
        throw std::runtime_error("probe exploded");
    }
    pastit::ProcessOutput clone_repository(std::string_view, const std::filesystem::path&) override {
        throw std::runtime_error("clone exploded");
    }
    std::optional<std::int64_t> parse_datetime(std::string_view, std::string_view) override { return 0; }
    std::string format_datetime(std::int64_t, std::string_view) override { return {}; }
    std::string hash_file(const std::filesystem::path&, pastit::HashAlgorithm) override {
        throw std::runtime_error("hash exploded");
    }
    pastit::ProcessOutput run_argv(const std::vector<std::string>&) override { return {}; }
};

std::vector<pastit::ExecutionResult> poll_until_done(pastit::FastActionExecutor& executor,
                                                     pastit::ExecutionContext& context) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto results = executor.poll(context);
        if (!results.empty()) {
            return results;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(false && "async job did not complete");
    return {};
}

}  // namespace

int main() {
    using namespace pastit;

    const auto root = std::filesystem::temp_directory_path() / "pastit-fast-action-executor-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "target");

    ClipboardStore store(root / "store");
    PathHistory history;
    ExecutionContext context{store, history, "req-fast"};
    FakeServices services;
    FastActionExecutor executor(services);

    auto terminal = action(ActionKind::OpenTerminalAtPath);
    terminal.parameters["working_directory"] = (root / "target").string();
    assert(executor.execute(terminal, context).status == ExecutionStatus::Completed);
    assert(services.terminal_directory == root / "target");

    auto report = action(ActionKind::NetworkDiagnosticReport);
    report.parameters["ip"] = "192.0.2.10";
    const auto report_result = executor.execute(report, context);
    assert(report_result.status == ExecutionStatus::Executing);
    assert(report_result.job_id.has_value());
    auto report_completed = poll_until_done(executor, context);
    assert(report_completed.size() == 1);
    assert(report_completed.front().status == ExecutionStatus::Completed);
    assert(services.probes.size() == 4);
    assert(services.probes.front().first == NetworkProbe::Ping);
    assert(services.probes.front().second == "192.0.2.10");
    assert(executor.network_report().has_value());
    assert(executor.network_report()->copy_text().find("ping ok") != std::string::npos);
    assert(executor.network_report()->copy_text().find("rdns miss") != std::string::npos);
    assert(report_completed.front().output_clipboard_ref.has_value());
    assert(store.read_text(*report_completed.front().output_clipboard_ref).find("trace ok") != std::string::npos);

    auto clone = action(ActionKind::CloneGithubHttps);
    clone.parameters["https_url"] = "https://github.com/acme/widget.git";
    clone.parameters["github_repo"] = "widget";
    clone.parameters["destination_directory"] = (root / "target").string();
    const auto clone_started = executor.execute(clone, context);
    assert(clone_started.status == ExecutionStatus::Executing);
    const auto clone_completed = poll_until_done(executor, context);
    assert(clone_completed.front().status == ExecutionStatus::Completed);
    assert(services.clone_url == "https://github.com/acme/widget.git");
    assert(services.clone_destination == root / "target" / "widget");

    auto datetime = action(ActionKind::ConvertTimezone);
    datetime.parameters["original"] = "1970-01-01T00:02:03Z";
    datetime.parameters["source_zone"] = "UTC";
    datetime.parameters["target_zone"] = "Asia/Singapore";
    const auto datetime_result = executor.execute(datetime, context);
    assert(datetime_result.status == ExecutionStatus::Completed);
    assert(services.parsed_value == "1970-01-01T00:02:03Z");
    assert(services.parsed_zone == "UTC");
    assert(services.formatted_epoch == 123);
    assert(services.formatted_zone == "Asia/Singapore");
    assert(executor.date_time_result()->copy_text() == "1970-01-01T00:02:03Z");

    const auto hash_file = root / "hash.txt";
    {
        std::ofstream output(hash_file);
        output << "hash me";
    }
    auto hash = action(ActionKind::HashSha512);
    hash.parameters["path"] = hash_file.string();
    const auto hash_result = executor.execute(hash, context);
    assert(hash_result.status == ExecutionStatus::Executing);
    const auto hash_completed = poll_until_done(executor, context);
    assert(hash_completed.front().status == ExecutionStatus::Completed);
    assert(services.hashed_path == hash_file);
    assert(services.hashed_algorithm == HashAlgorithm::Sha512);
    assert(executor.hash_result()->copy_text() == "sha512-digest");

    const auto contact_json =
        "{\"fields\":[{\"kind\":\"email\",\"label\":\"Email\",\"value\":\"ada@example.com\"}]}";
    auto contact = action(ActionKind::ExtractContactInfo);
    contact.parameters["contact_json"] = contact_json;
    assert(executor.execute(contact, context).status == ExecutionStatus::Completed);
    assert(executor.contact_result()->active().fields.front().value == "ada@example.com");

    auto field = action(ActionKind::CopyContactField);
    field.parameters["field_value"] = "ada@example.com";
    const auto field_result = executor.execute(field, context);
    assert(field_result.status == ExecutionStatus::Completed);
    assert(store.read_text(*field_result.output_clipboard_ref) == "ada@example.com");

    const auto mermaid_source = put_text(store, "A -> B", ContentKind::Text);
    auto mermaid = action(ActionKind::DrawMermaidDiagram, mermaid_source.ref);
    assert(executor.execute(mermaid, context).status == ExecutionStatus::Executing);
    const auto mermaid_done = poll_until_done(executor, context).front();
    assert(mermaid_done.status == ExecutionStatus::Completed);
    assert(mermaid_done.output_clipboard_ref.has_value());
    assert(store.read_text(*mermaid_done.output_clipboard_ref) == "graph TD\n  A --> B\n");
    assert(executor.renderer_result()->active().source == "A -> B");
    assert(executor.renderer_result()->active().payload == "graph TD\n  A --> B\n");
    assert(executor.renderer_result()->active().available);
    assert(executor.renderer_result()->active().output_path.has_value());
    assert(std::filesystem::exists(*executor.renderer_result()->active().output_path));

    MermaidNormalizationResult generated_mermaid;
    generated_mermaid.raw_source = "Here is the diagram:\n```mermaid\nflowchart LR\n  LLM --> Renderer\n```";
    generated_mermaid.normalized_source = "flowchart LR\n  LLM --> Renderer\n";
    generated_mermaid.ok = true;
    const auto generated_started = executor.render_generated_mermaid(mermaid, context, mermaid.source_ref,
                                                                     "A -> B", generated_mermaid);
    assert(generated_started.status == ExecutionStatus::Executing);
    assert(executor.renderer_result()->active().generated_mermaid.has_value());
    assert(executor.renderer_result()->active().generated_mermaid->clipboard_source_ref == mermaid_source.ref);
    assert(executor.renderer_result()->active().generated_mermaid->original_clipboard_source == "A -> B");
    const auto generated_done = poll_until_done(executor, context).front();
    assert(generated_done.status == ExecutionStatus::Completed);
    assert(executor.renderer_result()->active().source == generated_mermaid.raw_source);
    assert(executor.renderer_result()->active().payload == generated_mermaid.normalized_source);
    assert(executor.renderer_result()->active().generated_mermaid->raw_model_response == generated_mermaid.raw_source);
    assert(executor.renderer_result()->active().generated_mermaid->normalized_mermaid_source == generated_mermaid.normalized_source);
    assert(store.read_text(*generated_done.output_clipboard_ref) == generated_mermaid.normalized_source);

    const auto argv_calls_before_invalid = services.argv_calls.size();
    MermaidNormalizationResult invalid_mermaid;
    invalid_mermaid.raw_source = "Here is a diagram: A -> B";
    invalid_mermaid.error = "Mermaid response did not contain a supported Mermaid diagram header";
    const auto invalid_result = executor.render_generated_mermaid(mermaid, context, mermaid.source_ref,
                                                                  "A -> B", invalid_mermaid);
    assert(invalid_result.status == ExecutionStatus::Completed);
    assert(store.read_text(*invalid_result.output_clipboard_ref) == invalid_mermaid.raw_source);
    assert(services.argv_calls.size() == argv_calls_before_invalid);
    assert(executor.renderer_result()->active().source == invalid_mermaid.raw_source);
    assert(executor.renderer_result()->active().payload.empty());
    assert(executor.renderer_result()->active().status == RendererResultStatus::Failed);
    assert(executor.renderer_result()->active().error.find("supported Mermaid diagram header") != std::string::npos);
    assert(executor.renderer_result()->active().generated_mermaid->clipboard_source_ref == mermaid_source.ref);
    assert(executor.renderer_result()->active().generated_mermaid->original_clipboard_source == "A -> B");

    MermaidNormalizationResult llm_failure;
    llm_failure.error = "General LLM request failed: service unavailable";
    const auto llm_failed = executor.render_generated_mermaid(mermaid, context, mermaid.source_ref,
                                                              "A -> B", llm_failure);
    assert(llm_failed.status == ExecutionStatus::Completed);
    assert(executor.renderer_result()->active().status == RendererResultStatus::Failed);
    assert(executor.renderer_result()->active().generated_mermaid->clipboard_source_ref == mermaid_source.ref);
    assert(executor.renderer_result()->active().generated_mermaid->original_clipboard_source == "A -> B");
    assert(executor.renderer_result()->active().generated_mermaid->raw_model_response.empty());
    assert(executor.renderer_result()->copy_text() == "A -> B");
    assert(store.read_text(*llm_failed.output_clipboard_ref) == "A -> B");

    services.render_mode = FakeServices::RenderMode::MissingTool;
    assert(executor.render_generated_mermaid(mermaid, context, mermaid.source_ref, "A -> B", generated_mermaid).status ==
           ExecutionStatus::Executing);
    (void)poll_until_done(executor, context);
    assert(executor.renderer_result()->active().status == RendererResultStatus::Unavailable);
    assert(executor.renderer_result()->active().generated_mermaid->clipboard_source_ref == mermaid_source.ref);
    assert(executor.renderer_result()->active().generated_mermaid->original_clipboard_source == "A -> B");
    assert(executor.renderer_result()->active().generated_mermaid->raw_model_response == generated_mermaid.raw_source);
    assert(executor.renderer_result()->active().generated_mermaid->normalized_mermaid_source == generated_mermaid.normalized_source);
    services.render_mode = FakeServices::RenderMode::ProduceOutput;

    const auto er_source = put_text(store, "erDiagram\n  CUSTOMER ||--o{ ORDER : places", ContentKind::Text);
    auto er = action(ActionKind::DrawMermaidDiagram, er_source.ref);
    assert(executor.execute(er, context).status == ExecutionStatus::Executing);
    (void)poll_until_done(executor, context);
    assert(executor.renderer_result()->active().payload.starts_with("erDiagram\n"));

    auto qr = action(ActionKind::GenerateQr);
    qr.parameters["payload"] = "https://example.test";
    assert(executor.execute(qr, context).status == ExecutionStatus::Executing);
    assert(poll_until_done(executor, context).front().status == ExecutionStatus::Completed);
    assert(executor.renderer_result()->active().payload == "https://example.test");
    assert(executor.renderer_result()->active().available);
    assert(executor.renderer_result()->active().output_path.has_value());
    assert(std::filesystem::exists(*executor.renderer_result()->active().output_path));

    services.render_mode = FakeServices::RenderMode::MissingTool;
    assert(executor.execute(mermaid, context).status == ExecutionStatus::Executing);
    (void)poll_until_done(executor, context);
    assert(!executor.renderer_result()->active().available);
    assert(!executor.renderer_result()->active().output_path.has_value());
    assert(executor.renderer_result()->copy_text() == "graph TD\n  A --> B\n");

    services.render_mode = FakeServices::RenderMode::SuccessWithoutOutput;
    assert(executor.execute(qr, context).status == ExecutionStatus::Executing);
    (void)poll_until_done(executor, context);
    assert(!executor.renderer_result()->active().available);
    assert(!executor.renderer_result()->active().output_path.has_value());

    services.render_mode = FakeServices::RenderMode::CorruptOutput;
    assert(executor.execute(qr, context).status == ExecutionStatus::Executing);
    (void)poll_until_done(executor, context);
    assert(!executor.renderer_result()->active().available);
    assert(!executor.renderer_result()->active().output_path.has_value());

    const auto explain_source = put_text(store, "std::vector<int> values;", ContentKind::Text);
    auto explain = action(ActionKind::ExplainCode, explain_source.ref);
    explain.parameters["template_name"] = "Explain code";
    assert(executor.execute(explain, context).status == ExecutionStatus::Completed);
    assert(executor.renderer_result()->active().source == "std::vector<int> values;");

    {
        FakeServices persistent_services;
        FastActionExecutor persistent_executor(persistent_services);
        ExecutionContext persistent_context{store, history, "req-persistent"};
        persistent_context.fast_action_executor = &persistent_executor;

        auto persistent_mermaid = action(ActionKind::DrawMermaidDiagram, mermaid_source.ref);
        assert(execute_action(persistent_mermaid, persistent_context).status == ExecutionStatus::Executing);
        (void)poll_until_done(persistent_executor, persistent_context);
        assert(persistent_executor.renderer_result().has_value());
        assert(persistent_executor.renderer_result()->active().source == "A -> B");
    }

    {
        FakeServices fallback_services;
        ExecutionContext no_persistent_executor{store, history, "req-no-persistent"};
        no_persistent_executor.fast_action_services = &fallback_services;
        auto unsupported_async = action(ActionKind::NetworkDiagnosticReport);
        unsupported_async.parameters["ip"] = "203.0.113.10";
        const auto unsupported = execute_action(unsupported_async, no_persistent_executor);
        assert(unsupported.status == ExecutionStatus::Unsupported);
        assert(!unsupported.job_id.has_value());
        assert(fallback_services.probes.empty());

        for (const auto& renderer_action : {mermaid, qr}) {
            const auto renderer_result = execute_action(renderer_action, no_persistent_executor);
            assert(renderer_result.status == ExecutionStatus::Unsupported);
            assert(!renderer_result.job_id.has_value());
            assert(fallback_services.argv_calls.empty());
        }
    }

    {
        ThrowingServices throwing_services;
        FastActionExecutor throwing_executor(throwing_services);
        bool exception_metadata_preserved = true;

        auto throwing_report = action(ActionKind::NetworkDiagnosticReport);
        throwing_report.parameters["ip"] = "203.0.113.11";
        const auto started = throwing_executor.execute(throwing_report, context);
        assert(started.status == ExecutionStatus::Executing);
        const auto completed = poll_until_done(throwing_executor, context);
        assert(completed.size() == 1);
        assert(completed.front().status == ExecutionStatus::Failed);
        assert(completed.front().message.find("probe exploded") != std::string::npos);
        assert(throwing_executor.network_report().has_value());
        assert(throwing_executor.network_report()->active().status == NetworkReportStatus::Failed);
        const auto network_target = throwing_executor.network_report()->active().target;
        if (network_target != "203.0.113.11") {
            std::cerr << "throwing network target was '" << network_target << "'\n";
            exception_metadata_preserved = false;
        }

        auto throwing_hash = action(ActionKind::HashSha512);
        throwing_hash.parameters["path"] = hash_file.string();
        const auto hash_started = throwing_executor.execute(throwing_hash, context);
        assert(hash_started.status == ExecutionStatus::Executing);
        const auto hash_completed = poll_until_done(throwing_executor, context);
        assert(hash_completed.size() == 1);
        assert(hash_completed.front().status == ExecutionStatus::Failed);
        assert(hash_completed.front().message.find("hash exploded") != std::string::npos);
        assert(throwing_executor.hash_result().has_value());
        assert(throwing_executor.hash_result()->active().status == HashResultStatus::Failed);
        const auto& failed_hash = throwing_executor.hash_result()->active();
        if (failed_hash.algorithm != HashAlgorithm::Sha512) {
            std::cerr << "throwing hash algorithm was not SHA-512\n";
            exception_metadata_preserved = false;
        }
        if (failed_hash.path != hash_file) {
            std::cerr << "throwing hash path was '" << failed_hash.path.string() << "'\n";
            exception_metadata_preserved = false;
        }
        assert(exception_metadata_preserved && "async exception metadata should be preserved");
    }

    {
        BlockingServices blocking_services;
        FastActionExecutor async_executor(blocking_services);
        auto async_report = action(ActionKind::NetworkDiagnosticReport);
        async_report.parameters["ip"] = "203.0.113.9";
        const auto started = async_executor.execute(async_report, context);
        assert(started.status == ExecutionStatus::Executing);
        assert(started.job_id.has_value());
        blocking_services.wait_started();
        assert(async_executor.poll(context).empty());
        blocking_services.release();
        const auto completed = poll_until_done(async_executor, context);
        assert(completed.front().status == ExecutionStatus::Completed);
        assert(completed.front().job_id == started.job_id);
        assert(blocking_services.last_host == "203.0.113.9");
    }

    {
        BlockingServices blocking_services;
        FastActionExecutor async_executor(blocking_services);
        const auto started = async_executor.execute(mermaid, context);
        assert(started.status == ExecutionStatus::Executing);
        blocking_services.wait_started();
        assert(async_executor.poll(context).empty());
        assert(async_executor.renderer_result()->active().status == RendererResultStatus::Rendering);
        blocking_services.release();
        const auto completed = poll_until_done(async_executor, context);
        assert(completed.front().status == ExecutionStatus::Completed);
        assert(async_executor.renderer_result()->active().status == RendererResultStatus::Unavailable);
        assert(async_executor.renderer_result()->copy_text() == "graph TD\n  A --> B\n");
    }

    std::filesystem::remove_all(root);
}
