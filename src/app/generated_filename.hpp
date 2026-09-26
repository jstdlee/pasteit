#pragma once

#include "core/action.hpp"
#include "core/types.hpp"

#include <chrono>
#include <filesystem>
#include <string>

namespace pasteit {

std::string generated_filename(
    ActionKind action_kind,
    const ClipboardItem& item,
    std::chrono::system_clock::time_point when,
    const std::filesystem::path& directory);

}  // namespace pasteit
