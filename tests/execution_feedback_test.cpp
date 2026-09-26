#include "app/execution_feedback.hpp"
#include <cassert>
namespace {
class FakePlatform final:public pasteit::PlatformServices{
public:void apply_settings(const pasteit::AppSettings&)override{}
std::optional<pasteit::ClipboardCapture> poll_clipboard()override{return std::nullopt;}void process_events()override{}
bool publish_text(std::string_view)override{return true;}bool publish_image(const std::vector<std::byte>&,std::string_view)override{return true;}
pasteit::PlatformFocusContext focused_context()override{return {};}std::vector<pasteit::PlatformRecentPath> recent_paths()override{return {};}
bool register_global_shortcut()override{return true;}bool global_shortcut_activated()override{return false;}bool restore_focus_and_paste(const pasteit::PlatformFocusContext&)override{return true;}
bool open_path(const std::filesystem::path& p)override{opened=p;return true;}bool open_uri(std::string_view)override{return true;}bool copy_text(std::string_view t)override{copied=t;return true;}
bool move_popup_by(int,int)override{return true;}bool set_popup_opacity(float)override{return true;}std::vector<std::filesystem::path> preferred_ui_fonts()override{return {};}
std::optional<std::filesystem::path> choose_directory(const std::filesystem::path&)override{return std::nullopt;}
std::string copied;std::filesystem::path opened;
};}
int main(){
 pasteit::ExecutionFeedback feedback;feedback.start("r1","a1");pasteit::ExecutionResult result{.request_id="r1",.action_id="a1",.status=pasteit::ExecutionStatus::Completed,.message="saved",.output_path="/tmp/result.txt",.output_paths={"/tmp/result.txt"}};assert(feedback.finish(result));assert(feedback.find("r1","a1")->result.output_paths.size()==1);assert(!feedback.finish(pasteit::ExecutionResult{.request_id="old",.action_id="a1"}));
 FakePlatform platform;assert(pasteit::run_feedback_command(pasteit::FeedbackCommand::CopyOutputPath,result,0,platform));assert(platform.copied=="/tmp/result.txt");assert(pasteit::run_feedback_command(pasteit::FeedbackCommand::OpenContainingDirectory,result,0,platform));assert(platform.opened=="/tmp");
}
