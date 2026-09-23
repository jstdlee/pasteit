#include "ui/ai_result_panel.hpp"
#include "ui/image_preview_panel.hpp"
#include "ui/main_popup_panel.hpp"
#include <cassert>
int main(){pastit::MainPopupPanelState main;for(int i=0;i<9;++i)main.smart_rows.push_back({.action_id=std::to_string(i)});main.clamp_rows();assert(main.smart_rows.size()==8);pastit::ImagePreviewState image;image.set_zoom(9);assert(image.zoom==4.0F);image.set_zoom(0);assert(image.zoom==0.25F);image.open=true;pastit::AiResultPanelState ai;ai.editable_text=std::string(10000,'x');assert(ai.editable_text.size()==10000);ai.open=true;}
