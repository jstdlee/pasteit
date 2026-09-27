#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace pasteit {

class PlatformServices;

void install_imgui_clipboard_bridge(PlatformServices& platform);

bool input_text_string(const char* label, std::string& value, bool multiline = false,
                       int extra_flags = 0, float multiline_height = 0.0F, float width = -1.0F);
// Single-line field with placeholder text; honours the caller's item width.
bool input_text_hint(const char* id, const char* hint, std::string& value);
std::size_t multiline_editor_row_count(std::string_view value);
std::size_t multiline_editor_visible_rows(std::string_view value);
bool copyable_text(std::string_view value, bool wrapped = false);

}  // namespace pasteit
