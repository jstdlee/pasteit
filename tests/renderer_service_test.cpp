#include "config/settings_store.hpp"
#include "app/renderer_result_state.hpp"
#include "platform/fast_action_services.hpp"
#include "render/renderer_service.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

class FakeServices final : public pasteit::FastActionServices {
public:
    bool open_terminal(const std::filesystem::path&) override { return false; }
    pasteit::ProcessOutput run_network_probe(pasteit::NetworkProbe, std::string_view) override { return {}; }
    pasteit::ProcessOutput clone_repository(std::string_view, const std::filesystem::path&) override { return {}; }
    std::optional<std::int64_t> parse_datetime(std::string_view, std::string_view) override { return std::nullopt; }
    std::string format_datetime(std::int64_t, std::string_view) override { return {}; }
    std::string hash_file(const std::filesystem::path&, pasteit::HashAlgorithm) override { return {}; }

    pasteit::ProcessOutput run_argv(const std::vector<std::string>& argv) override {
        argv_calls.push_back(argv);
        for (std::size_t i = 0; i + 1 < argv.size(); ++i) {
            if (argv[i] == "-i") {
                std::ifstream input(argv[i + 1], std::ios::binary);
                std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
                mermaid_inputs.push_back(text);
            }
            if (argv[i] == "-o" && !mermaid_fixture.empty()) {
                std::filesystem::copy_file(mermaid_fixture, argv[i + 1],
                                           std::filesystem::copy_options::overwrite_existing);
            }
        }
        return next_output;
    }

    pasteit::ProcessOutput next_output{.exit_code = 127, .stderr_text = "execvp failed"};
    std::vector<std::vector<std::string>> argv_calls;
    std::vector<std::string> mermaid_inputs;
    std::filesystem::path mermaid_fixture;
};

std::filesystem::path make_unique_temp_dir(std::string_view name) {
    const auto base = std::filesystem::temp_directory_path();
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto path = base / (std::string{name} + "-" + std::to_string(std::random_device{}()) + "-" +
                            std::to_string(ticks) + "-" + std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directory(path, error)) {
            return path;
        }
    }
    assert(false && "failed to create temp directory");
    return {};
}

bool contains_arg(const std::vector<std::string>& argv, std::string_view value) {
    for (const auto& arg : argv) {
        if (arg == value) return true;
    }
    return false;
}

}  // namespace

int main() {
    using namespace pasteit;

    {
        FakeServices services;
        ExternalRendererService renderer(services, RendererSettings{});
        const auto root = make_unique_temp_dir("pasteit-offline-renderer-test");
        const auto html_path = root / "diagram.html";
        const auto mermaid = renderer.render_mermaid("```mermaid\nflowchart LR\nA-->B\n```", html_path);
        assert(mermaid.available && mermaid.success);
        std::ifstream html_file(html_path, std::ios::binary);
        const std::string html((std::istreambuf_iterator<char>(html_file)), {});
        assert(html.find("flowchart LR\nA--&gt;B") != std::string::npos);
        assert(html.find("mermaid.initialize") != std::string::npos);
        assert(html.find("https://cdn") == std::string::npos);
        assert(html.find("<script src=") == std::string::npos);
        const auto qr_path = root / "qr.png";
        const auto qr = renderer.render_qr("https://example.com", qr_path);
        assert(qr.available && qr.success);
        std::ifstream png_file(qr_path, std::ios::binary);
        char signature[8]{};
        png_file.read(signature, 8);
        assert(std::string_view(signature, 8) == std::string_view("\x89PNG\r\n\x1a\n", 8));
        assert(services.argv_calls.size() == 1);
        assert(services.argv_calls.front().front() == "mmdc");
        std::filesystem::remove_all(root);
    }

    {
        FakeServices services;
        RendererSettings settings;
        settings.qr_error_correction = "H";
        settings.qr_margin = 4;
        settings.qr_scale = 8;
        ExternalRendererService renderer(services, settings);
        const auto root = make_unique_temp_dir("pasteit-qr-configuration-test");
        const auto result = renderer.render_qr("A", root / "qr.png");
        assert(result.success);
        std::ifstream png(result.output, std::ios::binary);
        unsigned char header[24]{};
        png.read(reinterpret_cast<char*>(header), sizeof(header));
        assert(png.gcount() == sizeof(header));
        const unsigned int width = (static_cast<unsigned int>(header[16]) << 24) |
                                   (static_cast<unsigned int>(header[17]) << 16) |
                                   (static_cast<unsigned int>(header[18]) << 8) | header[19];
        assert(width == (21U + 2U * 4U) * 8U);
        assert(!renderer.render_qr("", root / "empty.png").success);
        assert(!renderer.render_qr(std::string(4000, 'x'), root / "too-big.png").success);
        assert(services.argv_calls.empty());
        std::filesystem::remove_all(root);
    }

    {
        FakeServices services;
        services.next_output = {.exit_code = 0};
        const auto root = make_unique_temp_dir("pasteit-mermaid-png-test");
        ExternalRendererService renderer(services);
        services.mermaid_fixture = root / "valid.png";
        assert(renderer.render_qr("fixture", services.mermaid_fixture).success);
        const auto result = renderer.render_mermaid("flowchart LR\nA-->B\n", root / "diagram.html");
        assert(result.success);
        assert(result.output.extension() == ".png");
        assert(rendered_output_decodes(RendererResultKind::Mermaid, result.output));
        assert(services.mermaid_inputs.front() == "flowchart LR\nA-->B\n");
        std::filesystem::remove_all(root);
    }

    {
        FakeServices services;
        RendererSettings settings;
        settings.mermaid_cli_path = "/opt/pasteit/mmdc";
        settings.mermaid_arguments = {"--theme", "neutral"};
        ExternalRendererService renderer(services, settings);
        const auto root = make_unique_temp_dir("pasteit-mermaid-source-test");
        const auto result = renderer.render_mermaid(
            "A generated diagram follows:\n```mermaid\nflowchart LR\nA-->B\n```\nextra prose",
            root / "diagram.html");
        assert(result.available);
        assert(result.success);
        assert(services.argv_calls.size() == 1);
        assert(services.argv_calls.front().front() == "/opt/pasteit/mmdc");
        assert(contains_arg(services.argv_calls.front(), "--theme"));
        std::ifstream input(result.output);
        const std::string html((std::istreambuf_iterator<char>(input)), {});
        assert(html.find("flowchart LR\nA--&gt;B") != std::string::npos);
        assert(html.find("extra prose") == std::string::npos);
        std::filesystem::remove_all(root);
    }

    {
        FakeServices services;
        ExternalRendererService renderer(services, RendererSettings{});
        const struct {
            std::string_view source;
            std::string_view expected;
        } cases[] = {
            {
                "Before the diagram\n```mermaid\n%%{init: {'theme': 'dark'}}%%\nflowchart LR\nA-->B\n```\nAfter the diagram",
                "%%{init: {'theme': 'dark'}}%%\nflowchart LR\nA-->B\n",
            },
            {
                "```mermaid\n---\nconfig:\n  theme: dark\n---\nflowchart LR\nA-->B\n```",
                "---\nconfig:\n  theme: dark\n---\nflowchart LR\nA-->B\n",
            },
            {
                "```mermaid\n%% A comment about the diagram\nflowchart LR\nA-->B\n```",
                "%% A comment about the diagram\nflowchart LR\nA-->B\n",
            },
        };
        const auto root = make_unique_temp_dir("pasteit-mermaid-fences-test");
        for (const auto& entry : cases) {
            const auto result = renderer.render_mermaid(entry.source, root / "fenced-diagram.html");
            assert(result.success);
            assert(ExternalRendererService::normalize_mermaid_source(entry.source) == entry.expected);
        }
        std::filesystem::remove_all(root);
        assert(ExternalRendererService::normalize_mermaid_source(
                   "An unfenced diagram follows:\nflowchart LR\nA-->B\n") ==
               "flowchart LR\nA-->B\n");
    }

    {
        const auto root = make_unique_temp_dir("pasteit-renderer-settings-test");
        SettingsStore store(root / "settings.json");
        auto settings = default_settings();
        settings.renderers.mermaid_cli_path = "/tools/mmdc";
        settings.renderers.mermaid_arguments = {"--theme", "forest"};
        settings.renderers.qrencode_path = "/tools/qrencode";
        settings.renderers.qr_error_correction = "H";
        settings.renderers.qr_margin = 4;
        settings.renderers.qr_scale = 8;
        settings.downloads.resume_directory = root / "downloads";
        settings.downloads.keep_part_files = false;
        settings.hash.default_algorithms = {"sha512"};
        settings.terminal.command = {"kgx", "--working-directory"};
        settings.terminal.profile = "PasteIt";
        settings.date_time.source_zone = "UTC";
        settings.date_time.target_zone = "Asia/Singapore";
        settings.date_time.use_24_hour_clock = false;
        settings.annotation.export_format = "jpg";
        settings.annotation.save_directory = root / "annotations";

        std::string error;
        assert(store.save(settings, error));
        const auto loaded = store.load();
        assert(loaded.loaded_from_disk);
        assert(loaded.settings.renderers.mermaid_cli_path == "/tools/mmdc");
        assert((loaded.settings.renderers.mermaid_arguments == std::vector<std::string>{"--theme", "forest"}));
        assert(loaded.settings.renderers.qrencode_path == "/tools/qrencode");
        assert(loaded.settings.renderers.qr_error_correction == "H");
        assert(loaded.settings.renderers.qr_margin == 4);
        assert(loaded.settings.renderers.qr_scale == 8);
        assert(loaded.settings.downloads.resume_directory == root / "downloads");
        assert(!loaded.settings.downloads.keep_part_files);
        assert((loaded.settings.hash.default_algorithms == std::vector<std::string>{"sha512"}));
        assert((loaded.settings.terminal.command == std::vector<std::string>{"kgx", "--working-directory"}));
        assert(loaded.settings.terminal.profile == "PasteIt");
        assert(loaded.settings.date_time.source_zone == "UTC");
        assert(loaded.settings.date_time.target_zone == "Asia/Singapore");
        assert(!loaded.settings.date_time.use_24_hour_clock);
        assert(loaded.settings.annotation.export_format == "svg");
        assert(loaded.settings.annotation.save_directory == root / "annotations");
        std::filesystem::remove_all(root);
    }
}
