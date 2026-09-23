#pragma once

#include "app/download_job.hpp"
#include "ui/multi_viewport.hpp"

#include <string_view>

namespace pastit {

FastActionPanelModel build_download_progress_panel_model(const DownloadManager& manager, std::string_view job_id);
void close_download_progress_panel(const FastActionPanelModel& model);

#if defined(PASTIT_HAS_DESKTOP_DEPS)
void draw_download_progress_panel(DownloadManager& manager,
                                  std::string_view job_id,
                                  bool& open,
                                  bool& focus_pending);
#endif

}  // namespace pastit
