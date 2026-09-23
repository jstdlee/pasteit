#include "ui/download_progress_panel.hpp"

#include "ui/imgui_widgets.hpp"

#include <sstream>

#if defined(PASTIT_HAS_DESKTOP_DEPS)
#include <imgui.h>
#endif

namespace pastit {
namespace {

std::string download_status_label(DownloadStatus status) {
    switch (status) {
        case DownloadStatus::Queued: return "Queued";
        case DownloadStatus::Running: return "Running";
        case DownloadStatus::Paused: return "Paused";
        case DownloadStatus::Completed: return "Completed";
        case DownloadStatus::Cancelled: return "Cancelled";
        case DownloadStatus::Failed: return "Failed";
    }
    return "Unknown";
}

std::string bytes_label(std::uint64_t bytes) {
    std::ostringstream out;
    out << bytes << " bytes";
    return out.str();
}

}  // namespace

FastActionPanelModel build_download_progress_panel_model(const DownloadManager& manager, std::string_view job_id) {
    FastActionPanelModel model;
    model.viewport = independent_panel_viewport("PasteIt Download##" + std::string{job_id}, {620.0F, 320.0F});
    model.toolbar = {
        {.id = "pause", .label = "Pause", .value = {}},
        {.id = "resume", .label = "Resume", .value = {}},
        {.id = "cancel", .label = "Cancel", .value = {}},
        {.id = "copy_path", .label = "Copy path", .value = {}},
    };
    const auto job = manager.get(job_id);
    if (!job.has_value()) {
        model.status_text = "Download job is no longer available";
        model.primary_text = model.status_text;
        return model;
    }
    std::ostringstream detail;
    detail << "URL: " << job->url << '\n'
           << "Destination: " << job->final_path.string() << '\n'
           << "Partial: " << job->part_path.string() << '\n'
           << "State: " << download_status_label(job->status) << '\n'
           << "Downloaded: " << bytes_label(job->downloaded);
    if (job->total > 0) {
        detail << " / " << bytes_label(job->total);
    }
    detail << '\n' << "Range resume: " << (job->range_supported ? "supported" : "unknown or unavailable") << '\n';
    if (!job->error.empty()) {
        detail << "Error: " << job->error << '\n';
    }
    model.status_text = download_status_label(job->status);
    model.primary_text = detail.str();
    return model;
}

void close_download_progress_panel(const FastActionPanelModel&) {
}

#if defined(PASTIT_HAS_DESKTOP_DEPS)
void draw_download_progress_panel(DownloadManager& manager,
                                  std::string_view job_id,
                                  bool& open,
                                  bool& focus_pending) {
    auto model = build_download_progress_panel_model(manager, job_id);
    if (focus_pending) {
        ImGui::SetNextWindowFocus();
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    }
    ImGui::SetNextWindowSize(ImVec2(model.viewport.initial_width, model.viewport.initial_height), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(model.viewport.title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
        if (focus_pending) {
            ImGui::SetWindowFocus();
            focus_pending = false;
        }
        if (ImGui::Button("Pause")) manager.pause(job_id);
        ImGui::SameLine();
        if (ImGui::Button("Resume")) manager.resume(job_id);
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) manager.cancel(job_id);
        ImGui::SameLine();
        if (const auto job = manager.get(job_id); job.has_value() && ImGui::Button("Copy path")) {
            ImGui::SetClipboardText(job->final_path.string().c_str());
        }
        ImGui::BeginChild("download-progress-body", ImVec2(0.0F, 0.0F), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        copyable_text(model.primary_text, true);
        ImGui::EndChild();
    }
    ImGui::End();
    if (!open) {
        close_download_progress_panel(model);
    }
}
#endif

}  // namespace pastit
