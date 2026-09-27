#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace pasteit {

// Initial value for a prompt placeholder: languages get sensible defaults.
std::string default_prompt_parameter(std::string_view name);
// Fills missing names with their defaults (remembered values are kept).
void fill_prompt_parameter_defaults(const std::vector<std::string>& names, std::map<std::string, std::string>& values);
bool is_language_parameter(std::string_view name);

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
// One labelled row per placeholder; language placeholders get a dropdown of
// common languages next to the free-text field. Returns true when edited.
bool draw_prompt_parameter_fields(const char* id, const std::vector<std::string>& names,
                                  std::map<std::string, std::string>& values);
#endif

}  // namespace pasteit
