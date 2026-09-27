#include "ui/multi_viewport.hpp"

#include "ui/imgui_widgets.hpp"
#include "app/renderer_result_state.hpp"
#include "util/path_utf8.hpp"

#include <chrono>
#include <fstream>

namespace pasteit {

IndependentViewportDescriptor independent_panel_viewport(std::string title, ViewportSize size) {
    return IndependentViewportDescriptor{
        .title = std::move(title),
        .independent = true,
        .is_root_child = false,
        .initial_width = size.width,
        .initial_height = size.height,
    };
}

std::size_t panel_text_visible_rows(std::string_view value) {
    return multiline_editor_visible_rows(value);
}

PanelSaveResult save_panel_command(const FastActionPanelCommand& command,
                                   const std::filesystem::path& destination,
                                   bool confirm_overwrite,
                                   std::string_view required_extension) {
    PanelSaveResult result;
    result.destination = destination;
    try {
        if (command.kind != PanelCommandKind::SaveText &&
            command.kind != PanelCommandKind::SaveRenderedOutput) {
            result.error = "Command is not a save operation";
        } else if (destination.empty() || destination.extension() != required_extension) {
            result.error = "Choose a destination ending in " + std::string(required_extension);
        } else if (!std::filesystem::is_directory(destination.parent_path())) {
            result.error = "Destination directory does not exist";
        } else if (std::filesystem::exists(destination) && !confirm_overwrite) {
            result.error = "Destination exists; confirm overwrite to save";
        } else if (command.kind == PanelCommandKind::SaveRenderedOutput) {
            const auto source = path_from_utf8_string(command.value);
            const auto kind = required_extension == ".png" ? RendererResultKind::Qr : RendererResultKind::Mermaid;
            if (!rendered_output_decodes(kind, source)) {
                result.error = "Rendered output is missing or invalid";
            } else {
                std::filesystem::copy_file(source, destination,
                    confirm_overwrite ? std::filesystem::copy_options::overwrite_existing
                                      : std::filesystem::copy_options::none);
                result.success = true;
            }
        } else {
            std::ofstream output(destination, std::ios::binary | std::ios::trunc);
            if (!output || !(output << command.value) || !(output.flush())) {
                result.error = "Could not write destination";
            } else {
                result.success = true;
            }
        }
    } catch (const std::exception& error) {
        result.error = error.what();
    }
    return result;
}

bool RendererPreviewPanelState::select_view(const FastActionPanelCommand& command, bool preview_available) {
    if (command.kind == PanelCommandKind::ShowSource) {
        mode = Mode::Source;
        return true;
    }
    if (command.kind == PanelCommandKind::ShowPreview && preview_available && command.enabled) {
        mode = Mode::Preview;
        return true;
    }
    if (command.kind == PanelCommandKind::ShowPreview) status_text = "Rendered preview unavailable";
    return false;
}

bool RendererPreviewPanelState::save(const FastActionPanelCommand& command, std::string_view extension) {
    if (pending_save.has_value()) return false;
    status_text = "Saving...";
    auto path = path_from_utf8_string(destination);
    if (!path.empty() && path.extension() != extension) {
        path.replace_extension(extension);
        destination = path_to_utf8_string(path);
    }
    const auto overwrite = confirm_overwrite;
    pending_save.emplace(std::async(std::launch::async,
        [command, path, overwrite, extension = std::string(extension)] {
            return save_panel_command(command, path, overwrite, extension);
        }));
    return true;
}

void RendererPreviewPanelState::open_preview(
    std::filesystem::path path, std::function<bool(const std::filesystem::path&)> opener) {
    if (pending_open.has_value()) return;
    status_text = "Opening rendered preview...";
    pending_open.emplace(std::async(std::launch::async,
        [path = std::move(path), opener = std::move(opener)] {
            const auto kind = path.extension() == ".png" ? RendererResultKind::Qr : RendererResultKind::Mermaid;
            return rendered_output_decodes(kind, path) && opener(path);
        }));
}

void RendererPreviewPanelState::poll() {
    using namespace std::chrono_literals;
    if (pending_save && pending_save->wait_for(0ms) == std::future_status::ready) {
        try {
            const auto result = pending_save->get();
            status_text = result.success ? "Saved: " + path_to_utf8_string(result.destination) : "Save failed: " + result.error;
        } catch (const std::exception& error) {
            status_text = std::string{"Save failed: "} + error.what();
        }
        pending_save.reset();
    }
    if (pending_open && pending_open->wait_for(0ms) == std::future_status::ready) {
        try {
            status_text = pending_open->get() ? "Rendered preview opened" : "Could not open rendered preview";
        } catch (const std::exception& error) {
            status_text = std::string{"Could not open rendered preview: "} + error.what();
        }
        pending_open.reset();
    }
}

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
void configure_independent_viewports(ImGuiIO& io, ImGuiStyle& style) {
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    style.WindowRounding = 8.0F;
    style.Colors[ImGuiCol_WindowBg].w = 1.0F;
}

ImGuiWindowClass independent_window_class() {
    ImGuiWindowClass window_class;
    window_class.ViewportFlagsOverrideSet |= ImGuiViewportFlags_NoAutoMerge;
    // Sub-windows are not TopMost: the platform layer makes each one owned by
    // (transient for) the popup, which keeps it in front of the popup only.
    return window_class;
}

#endif

}  // namespace pasteit
