#pragma once
#include <algorithm>
namespace pasteit {struct ImagePreviewState{bool open=false;bool focus_pending=false;float zoom=1.0F;void set_zoom(float value){zoom=std::clamp(value,0.25F,4.0F);}};}
