#include "ui/hash_result_panel.hpp"

#include "ui/imgui_widgets.hpp"
#include "util/path_utf8.hpp"

#include <sstream>

#if defined(PASTIT_HAS_DESKTOP_DEPS)
#include <imgui.h>
#endif

namespace pastit {
namespace {

std::string hash_label(HashAlgorithm algorithm) {
    return algorithm == HashAlgorithm::Sha512 ? "SHA-512" : "SHA-256";
}

}  // namespace

FastActionPanelModel build_hash_result_panel_model(const HashResultState& state) {
    const auto& active = state.active();
    std::ostringstream text;
    text << "Algorithm: " << hash_label(active.algorithm) << '\n'
         << "Path: " << path_to_utf8_string(active.path) << '\n';
    if (!active.digest.empty()) {
        text << "Digest: " << active.digest << '\n';
    }
    if (!active.error.empty()) {
        text << "Error: " << active.error << '\n';
    }
    FastActionPanelModel model;
    model.viewport = independent_panel_viewport("PasteIt Hash Result##" + active.action_id, {680.0F, 320.0F});
    model.toolbar = {
        {.id = "copy_digest", .label = "Copy digest", .value = active.digest, .enabled = !active.digest.empty()},
        {.id = "copy_algorithm_path", .label = "Copy algorithm path",
         .value = hash_label(active.algorithm) + "  " + path_to_utf8_string(active.path)},
    };
    model.status_text = active.error;
    model.primary_text = text.str();
    model.selectable_read_only_multiline = true;
    return model;
}

#if defined(PASTIT_HAS_DESKTOP_DEPS)
void draw_hash_result_panel(const HashResultState& state, bool& open, bool& focus_pending) {
    auto model = build_hash_result_panel_model(state);
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
        for (std::size_t index = 0; index < model.toolbar.size(); ++index) {
            if (index != 0) ImGui::SameLine();
            const auto& command = model.toolbar[index];
            if (!command.enabled) ImGui::BeginDisabled();
            if (ImGui::Button(command.label.c_str())) ImGui::SetClipboardText(command.value.c_str());
            if (!command.enabled) ImGui::EndDisabled();
        }
        std::string body = model.primary_text;
        input_text_string("##hash-result-body", body, true, ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::End();
}
#endif

}  // namespace pastit
