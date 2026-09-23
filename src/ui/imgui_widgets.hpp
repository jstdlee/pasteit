#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace pastit {

class PlatformServices;

void install_imgui_clipboard_bridge(PlatformServices& platform);

bool input_text_string(const char* label, std::string& value, bool multiline = false, int extra_flags = 0);
std::size_t multiline_editor_row_count(std::string_view value);
std::size_t multiline_editor_visible_rows(std::string_view value);
bool copyable_text(std::string_view value, bool wrapped = false);

}  // namespace pastit
