#include "test_env.hpp"
#include "annotation/annotation_export.hpp"
#include "app/contact_result_state.hpp"
#include "app/download_job.hpp"
#include "app/hash_result_state.hpp"
#include "app/network_report_state.hpp"
#include "app/renderer_result_state.hpp"
#include "ui/contact_result_panel.hpp"
#include "ui/download_progress_panel.hpp"
#include "ui/hash_result_panel.hpp"
#include "ui/image_annotation_panel.hpp"
#include "ui/mermaid_preview_panel.hpp"
#include "ui/network_report_panel.hpp"
#include "ui/qr_preview_panel.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace {

class HoldingTransport final : public pasteit::DownloadTransport {
public:
    pasteit::DownloadResponse fetch(const pasteit::DownloadRequest& request,
                                   const std::atomic_bool&) override {
        last_request = request;
        return {.status_code = 206, .total_size = 8192, .range_supported = true, .body = "partial"};
    }

    pasteit::DownloadRequest last_request;
};

bool has_command(const pasteit::FastActionPanelModel& model, std::string_view label) {
    for (const auto& command : model.toolbar) {
        if (command.label == label) {
            return true;
        }
    }
    return false;
}

std::filesystem::path sample_png_fixture() {
    return std::filesystem::path{__FILE__}.parent_path() / "fixtures" / "sample.png";
}

}  // namespace

int main() {
    using namespace pasteit;

    {
        RendererResultState state;
        state.prepare(RendererResult{
            .action_id = "mermaid-action",
            .kind = RendererResultKind::Mermaid,
            .source = "A -> B",
            .payload = "graph TD\n  A --> B\n",
            .available = false,
            .status = RendererResultStatus::Unavailable,
            .output_path = std::nullopt,
            .error = "mmdc unavailable",
        });
        const auto model = build_mermaid_preview_panel_model(state);
        assert(model.viewport.independent);
        assert(model.viewport.title == "PasteIt Mermaid Preview##mermaid-action");
        assert(model.toolbar.size() >= 4);
        assert(model.toolbar[0].label == "Source");
        assert(model.toolbar[1].label == "Preview");
        assert(model.toolbar[2].label == "Copy source");
        assert(model.toolbar[3].label == "Save source");
        assert(model.primary_text.find("graph TD") != std::string::npos);
        assert(model.fallback_text.find("A --> B") != std::string::npos);
        assert(model.max_visible_rows == 10);
        assert(model.toolbar[0].kind == PanelCommandKind::ShowSource);
        assert(model.toolbar[1].kind == PanelCommandKind::ShowPreview);
        assert(model.toolbar[2].kind == PanelCommandKind::CopyText);
        assert(model.toolbar[3].kind == PanelCommandKind::SaveText);
        assert(!model.preview_available);
        RendererPreviewPanelState panel;
        assert(panel.select_view(model.toolbar[0], false));
        assert(panel.mode == RendererPreviewPanelState::Mode::Source);
        assert(!panel.select_view(model.toolbar[1], false));
        assert(panel.mode == RendererPreviewPanelState::Mode::Source);
    }

    {
        RendererResultState state;
        state.prepare(RendererResult{
            .action_id = "mermaid-generation-failed",
            .kind = RendererResultKind::Mermaid,
            .status = RendererResultStatus::Failed,
            .error = "General LLM request failed",
            .generated_mermaid = GeneratedMermaidSource{
                .clipboard_source_ref = "clip-original",
                .original_clipboard_source = "A -> B",
                .raw_model_response = {},
                .normalized_mermaid_source = {},
            },
        });
        const auto model = build_mermaid_preview_panel_model(state);
        assert(model.primary_text == "A -> B");
        assert(model.status_text == "General LLM request failed");
        assert(model.toolbar[2].value == "A -> B");
        assert(model.toolbar.back().id == "copy_original");
        assert(model.toolbar.back().value == "A -> B");
        assert(model.rows.size() == 1);
        assert(model.rows.front().value == "A -> B");

        state.prepare(RendererResult{
            .action_id = "mermaid-normalization-failed",
            .kind = RendererResultKind::Mermaid,
            .source = "Model prose without Mermaid",
            .status = RendererResultStatus::Failed,
            .error = "No supported Mermaid header",
            .generated_mermaid = GeneratedMermaidSource{
                .clipboard_source_ref = "clip-original",
                .original_clipboard_source = "A -> B",
                .raw_model_response = "Model prose without Mermaid",
            },
        });
        const auto normalization_model = build_mermaid_preview_panel_model(state);
        assert(normalization_model.primary_text == "Model prose without Mermaid");
        assert(normalization_model.toolbar.back().value == "A -> B");
        assert(normalization_model.rows.front().value == "A -> B");

        state.prepare(RendererResult{
            .action_id = "mermaid-render-failed",
            .kind = RendererResultKind::Mermaid,
            .source = "Model prose with Mermaid",
            .payload = "flowchart LR\n A-->B\n",
            .status = RendererResultStatus::Unavailable,
            .error = "Renderer unavailable",
            .generated_mermaid = GeneratedMermaidSource{
                .clipboard_source_ref = "clip-original",
                .original_clipboard_source = "A -> B",
                .raw_model_response = "Model prose with Mermaid",
                .normalized_mermaid_source = "flowchart LR\n A-->B\n",
            },
        });
        const auto render_model = build_mermaid_preview_panel_model(state);
        assert(render_model.primary_text == "flowchart LR\n A-->B\n");
        assert(render_model.toolbar.back().value == "A -> B");
        assert(render_model.rows.front().value == "A -> B");
    }

    {
        RendererResultState state;
        state.prepare(RendererResult{
            .action_id = "mermaid-missing-output",
            .kind = RendererResultKind::Mermaid,
            .source = "flowchart LR\nA-->B\n",
            .payload = "flowchart LR\nA-->B\n",
            .available = true,
            .status = RendererResultStatus::Ready,
            .output_path = std::filesystem::temp_directory_path() / "pasteit-definitely-missing-mermaid.html",
            .error = {},
        });
        const auto model = build_mermaid_preview_panel_model(state);
        assert(!model.preview_available);
        assert(model.fallback_text == "flowchart LR\nA-->B\n");
    }

    {
        const auto root = std::filesystem::temp_directory_path() /
            ("pasteit-panel-command-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        const auto source = root / "diagram.mmd";
        const FastActionPanelCommand save_source{.id = "save_source", .label = "Save source",
            .value = "graph TD\n A-->B\n", .kind = PanelCommandKind::SaveText};
        assert(save_panel_command(save_source, source, false, ".mmd").success);
        std::ifstream source_file(source);
        assert(std::string((std::istreambuf_iterator<char>(source_file)), {}) == save_source.value);
        assert(!save_panel_command(save_source, source, false, ".mmd").success);
        assert(!save_panel_command(save_source, root / "wrong.txt", false, ".mmd").success);
        const auto rendered = root / "rendered.html";
        {
            std::ofstream stream(rendered);
            stream << "<html><script src=\"https://cdn.example/mermaid.js\"></script></html>";
        }
        assert(!rendered_output_decodes(RendererResultKind::Mermaid, rendered));
        {
            std::ofstream stream(rendered);
            stream << "<!doctype html><html><pre class=\"mermaid\">graph TD</pre>"
                      "<script>mermaid.initialize({startOnLoad:false});mermaid.run()</script></html>";
        }
        assert(rendered_output_decodes(RendererResultKind::Mermaid, rendered));
        RendererPreviewPanelState panel;
        const FastActionPanelCommand preview{.id = "preview", .label = "Preview", .value = rendered.string(),
                                              .kind = PanelCommandKind::ShowPreview};
        assert(panel.select_view(preview, true));
        assert(panel.mode == RendererPreviewPanelState::Mode::Preview);
        const FastActionPanelCommand source_view{.id = "source", .label = "Source",
                                                  .kind = PanelCommandKind::ShowSource};
        assert(panel.select_view(source_view, true));
        assert(panel.mode == RendererPreviewPanelState::Mode::Source);
        const auto image = root / "diagram.html";
        const FastActionPanelCommand save_image{.id = "save_rendered", .label = "Save rendered HTML",
            .value = rendered.string(), .kind = PanelCommandKind::SaveRenderedOutput};
        assert(save_panel_command(save_image, image, false, ".html").success);
        std::ifstream image_file(image);
        assert(std::string((std::istreambuf_iterator<char>(image_file)), {}).find("<html>") != std::string::npos);
        assert(!save_panel_command(save_image, image, false, ".html").success);
        assert(save_panel_command(save_image, image, true, ".html").success);
        panel.destination = (root / "queued.mmd").string();
        assert(panel.save(save_source, ".mmd"));
        assert(panel.pending_save.has_value());
        for (int attempt = 0; attempt < 100 && panel.pending_save.has_value(); ++attempt) {
            panel.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        assert(!panel.pending_save.has_value());
        assert(panel.status_text.find("Saved:") != std::string::npos);
        assert(panel.save(save_image, ".html"));
        assert(panel.destination == (root / "queued.html").string());
        for (int attempt = 0; attempt < 100 && panel.pending_save.has_value(); ++attempt) {
            panel.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        assert(!panel.pending_save.has_value());
        assert(std::filesystem::is_regular_file(root / "queued.html"));
        assert(panel.status_text.find("Saved:") != std::string::npos);
        assert(panel.save(save_source, ".mmd"));
        assert(panel.destination == (root / "queued.mmd").string());
        while (panel.pending_save.has_value()) {
            panel.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        pasteit_test::remove_tree(root);
    }

    {
        RendererResultState state;
        state.prepare(RendererResult{
            .action_id = "qr-action",
            .kind = RendererResultKind::Qr,
            .source = "https://example.test/deep/path",
            .payload = "https://example.test/deep/path",
            .available = false,
            .status = RendererResultStatus::Unavailable,
            .output_path = std::nullopt,
            .error = "qrencode unavailable",
        });
        const auto model = build_qr_preview_panel_model(state);
        assert(model.viewport.independent);
        assert(model.toolbar.size() >= 2);
        assert(model.toolbar[0].label == "Copy payload");
        assert(model.toolbar[1].label == "Save payload");
        assert(model.toolbar[0].kind == PanelCommandKind::CopyText);
        assert(model.toolbar[1].kind == PanelCommandKind::SaveText);
        assert(model.primary_text == "https://example.test/deep/path");
        assert(model.fallback_text == "https://example.test/deep/path");
    }

    {
        RendererResultState state;
        state.prepare(RendererResult{
            .action_id = "qr-missing-output",
            .kind = RendererResultKind::Qr,
            .source = "mailto:ada@example.com",
            .payload = "mailto:ada@example.com",
            .available = true,
            .status = RendererResultStatus::Ready,
            .output_path = std::filesystem::temp_directory_path() / "pasteit-definitely-missing-qr.png",
            .error = {},
        });
        const auto model = build_qr_preview_panel_model(state);
        assert(!model.preview_available);
        assert(model.fallback_text == "mailto:ada@example.com");
    }

    {
        ContactResultState contact;
        contact.set_fields("contact-action",
                           {ContactField{"email", "Email", "ada@example.com"},
                            ContactField{"phone", "Phone", "+1 415 555 0100"}},
                           "{\"fields\":[]}");
        const auto model = build_contact_result_panel_model(contact);
        assert(model.viewport.independent);
        assert(model.rows.size() == 2);
        assert(model.rows.front().selectable);
        assert(model.rows.front().copy_command.label == "Copy");
        assert(model.rows.front().value == "ada@example.com");
    }

    {
        NetworkReportState report;
        report.start("network-action", "192.0.2.10");
        report.add_probe(NetworkProbe::Ping, ProcessOutput{.exit_code = 0, .stdout_text = "pong detail"}, 12);
        report.complete();
        const auto model = build_network_report_panel_model(report);
        assert(model.viewport.independent);
        assert(model.toolbar[0].label == "Copy report");
        assert(model.primary_text.find("pong detail") != std::string::npos);
        assert(model.primary_text.find("192.0.2.10") != std::string::npos);
        assert(model.selectable_read_only_multiline);
    }

    {
        HashResultState hash;
        hash.complete(HashResult{
            .action_id = "hash-action",
            .algorithm = HashAlgorithm::Sha512,
            .path = "/tmp/source.txt",
            .digest = "abcdef0123456789",
        });
        const auto model = build_hash_result_panel_model(hash);
        assert(model.viewport.independent);
        assert(has_command(model, "Copy digest"));
        assert(has_command(model, "Copy algorithm path"));
        assert(model.primary_text.find("abcdef0123456789") != std::string::npos);
        assert(model.selectable_read_only_multiline);
    }

    {
        auto transport = std::make_shared<HoldingTransport>();
        DownloadManager downloads(transport);
        const auto job_id = downloads.start("https://example.test/file.bin", "/tmp/pasteit-panel.bin");
        downloads.pause(job_id);
        auto before_close = downloads.get(job_id);
        assert(before_close.has_value());
        const auto model = build_download_progress_panel_model(downloads, job_id);
        assert(model.viewport.independent);
        assert(model.toolbar[0].label == "Pause");
        assert(model.toolbar[1].label == "Resume");
        assert(model.toolbar[2].label == "Cancel");
        assert(model.primary_text.find("https://example.test/file.bin") != std::string::npos);
        close_download_progress_panel(model);
        auto after_close = downloads.get(job_id);
        assert(after_close.has_value());
        assert(after_close->id == job_id);
        downloads.cancel(job_id);
        downloads.wait(job_id);
    }

    {
        ImageAnnotationPanelState panel;
        panel.document.set_original_image("/tmp/original.png");
        panel.document.add_line({0.0F, 0.0F}, {12.0F, 8.0F});
        const auto model = build_image_annotation_panel_model(panel);
        assert(model.viewport.independent);
        assert(model.annotation_section_expanded);
        assert(has_command(model, "Pen"));
        assert(has_command(model, "Line"));
        assert(has_command(model, "Rectangle"));
        assert(has_command(model, "Arrow"));
        assert(has_command(model, "Comment"));
        assert(has_command(model, "Undo"));
        assert(has_command(model, "Clear"));
        assert(has_command(model, "Save annotated SVG"));
        assert(model.primary_text.find("/tmp/original.png") != std::string::npos);
        assert(!panel.export_running());
        const auto output = std::filesystem::temp_directory_path() / "pasteit-async-annotation-panel-test.svg";
        panel.document.set_original_image(sample_png_fixture());
        assert(panel.start_export(output));
        assert(panel.export_running());
        for (int attempt = 0; attempt < 100 && panel.export_running(); ++attempt) {
            panel.poll_export();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        assert(!panel.export_running());
        assert(panel.last_export_path == output);
        std::filesystem::remove(output);
    }

    {
        ImageAnnotationPanelState panel;
        panel.document.set_original_image(sample_png_fixture());
        const auto output = std::filesystem::temp_directory_path() / "pasteit-annotation-gated-export.svg";
        std::promise<void> started;
        std::promise<void> release;
        const auto released = release.get_future().share();
        assert(panel.start_export(output, [&](const AnnotationDocument& doc, const std::filesystem::path& path) {
            started.set_value();
            released.wait();
            return export_annotation_svg(doc, path);
        }));
        assert(started.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        panel.poll_export();
        assert(panel.export_running());
        assert(panel.status_text.find("Saving") != std::string::npos);
        release.set_value();
        for (int attempt = 0; attempt < 100 && panel.export_running(); ++attempt) {
            panel.poll_export();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        assert(!panel.export_running());
        assert(panel.last_export_path == output);
        std::filesystem::remove(output);
    }

    {
        const auto output = std::filesystem::temp_directory_path() / "pasteit-annotation-export-test.svg";
        AnnotationDocument doc(sample_png_fixture());
        doc.add_line({1.0F, 2.0F}, {12.0F, 8.0F});
        doc.add_comment("note", {4.0F, 5.0F});
        const auto exported = export_annotation_svg(doc, output);
        assert(exported.success);
        assert(exported.output_path == output);
        assert(exported.format == "svg");
        std::ifstream input(output);
        const std::string svg((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        assert(svg.find("<svg") != std::string::npos);
        assert(svg.find("data:image/png;base64,") != std::string::npos);
        assert(svg.find("<line") != std::string::npos);
        assert(svg.find("note") != std::string::npos);
        std::filesystem::remove(output);
    }
}
