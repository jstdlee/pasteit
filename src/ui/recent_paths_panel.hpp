#pragma once

#include "ui/localization.hpp"
#include "ui/recent_paths_model.hpp"

namespace pastit {

RecentPathCommand render_recent_paths_panel(RecentPathsState& state, const RecentPathsModel& model,
                                            UiLanguage language);

}  // namespace pastit
