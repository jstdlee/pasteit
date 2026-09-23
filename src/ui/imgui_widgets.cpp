#include "ui/imgui_widgets.hpp"

#include <algorithm>
#include <cstring>

#if defined(PASTIT_HAS_DESKTOP_DEPS)
#include <imgui.h>
#endif

namespace pastit {
namespace {

constexpr std::size_t kMinimumEditorRows = 3;
constexpr std::size_t kMaximumEditorRows = 10;

#if defined(PASTIT_HAS_DESKTOP_DEPS)
int resize_string_callback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* value = static_cast<std::string*>(data->UserData);
        value->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = value->data();
    }
    return 0;
}
#endif

}  // namespace

std::size_t multiline_editor_row_count(std::string_view value) {
    return 1U + static_cast<std::size_t>(std::count(value.begin(), value.end(), '\n'));
}

std::size_t multiline_editor_visible_rows(std::string_view value) {
    return std::clamp(multiline_editor_row_count(value), kMinimumEditorRows, kMaximumEditorRows);
}

bool copyable_text(std::string_view value, bool wrapped) {
#if defined(PASTIT_HAS_DESKTOP_DEPS)
    const std::string text{value};
    const bool clicked = wrapped
        ? (ImGui::TextWrapped("%s", text.c_str()), ImGui::IsItemClicked(ImGuiMouseButton_Left))
        : ImGui::Selectable(text.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
    if (clicked) {
        ImGui::SetClipboardText(text.c_str());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Click to copy");
    }
    return clicked;
#else
    (void)value;
    (void)wrapped;
    return false;
#endif
}

bool input_text_string(const char* label, std::string& value, bool multiline, int extra_flags) {
#if defined(PASTIT_HAS_DESKTOP_DEPS)
    if (value.capacity() < value.size() + 256) {
        value.reserve(value.size() + 256);
    }
    const std::string label_text{label == nullptr ? "" : label};
    const auto marker = label_text.find("##");
    const auto visible_label = marker == std::string::npos ? label_text : label_text.substr(0, marker);
    std::string widget_label = marker == std::string::npos
        ? "##input-" + label_text
        : label_text.substr(marker);
    if (!visible_label.empty()) {
        copyable_text(visible_label);
        if (!multiline) {
            ImGui::SameLine();
        }
    }
    if (!multiline) {
        ImGui::SetNextItemWidth(-1.0F);
    }
    const auto flags = static_cast<ImGuiInputTextFlags>(extra_flags) | ImGuiInputTextFlags_CallbackResize;
    const auto editor_height = static_cast<float>(multiline_editor_visible_rows(value)) *
                               ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().FramePadding.y;
    const bool changed = multiline
        ? ImGui::InputTextMultiline(widget_label.c_str(), value.data(), value.capacity() + 1, ImVec2(-1.0F, editor_height), flags,
                                    resize_string_callback, &value)
        : ImGui::InputText(widget_label.c_str(), value.data(), value.capacity() + 1, flags, resize_string_callback, &value);
    value.resize(std::strlen(value.c_str()));
    return changed;
#else
    (void)label;
    (void)value;
    (void)multiline;
    (void)extra_flags;
    return false;
#endif
}

}  // namespace pastit
