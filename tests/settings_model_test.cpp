#include "ui/settings_model.hpp"

#include <cassert>
#include <vector>

int main(){
    auto saved=pastit::default_settings();
    pastit::SettingsModel model(saved);
    model.working().window_opacity=0.7F;
    model.cancel();
    assert(model.working().window_opacity==saved.window_opacity);
    model.working().window_opacity=0.7F;
    model.working().renderers.mermaid_cli_path="/tools/mmdc";
    model.working().renderers.mermaid_arguments={"--theme","neutral"};
    model.working().renderers.qrencode_path="/tools/qrencode";
    model.working().renderers.qr_error_correction="H";
    model.working().downloads.resume_directory="/tmp/pastit-downloads";
    model.working().downloads.keep_part_files=false;
    model.working().hash.default_algorithms={"sha512"};
    model.working().terminal.command={"kgx","--working-directory"};
    model.working().date_time.target_zone="Asia/Singapore";
    model.working().annotation.export_format="jpg";
    model.working().annotation.save_directory="/tmp/pastit-annotations";
    std::string error;
    assert(model.save(saved,error));
    assert(saved.window_opacity==0.7F);
    assert(saved.renderers.mermaid_cli_path=="/tools/mmdc");
    assert((saved.renderers.mermaid_arguments==std::vector<std::string>{"--theme","neutral"}));
    assert(saved.renderers.qrencode_path=="/tools/qrencode");
    assert(saved.renderers.qr_error_correction=="H");
    assert(saved.downloads.resume_directory=="/tmp/pastit-downloads");
    assert(!saved.downloads.keep_part_files);
    assert((saved.hash.default_algorithms==std::vector<std::string>{"sha512"}));
    assert((saved.terminal.command==std::vector<std::string>{"kgx","--working-directory"}));
    assert(saved.date_time.target_zone=="Asia/Singapore");
    assert(saved.annotation.export_format=="svg");
    assert(saved.annotation.save_directory=="/tmp/pastit-annotations");
    model.working().djev.api_key="changed";
    model.reset(pastit::SettingsSection::Paths);
    assert(model.working().djev.api_key=="changed");
    model.reset(pastit::SettingsSection::FastActions);
    assert(model.working().annotation.export_format=="svg");
}
