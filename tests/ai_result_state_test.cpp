#include "app/ai_result_state.hpp"
#include <cassert>
int main(){
    pastit::TextGenerationRequest frozen{.request_id="r1",.endpoint="e",.api_key="k",.model_id="m",.system_message="old",.user_message="source"};
    pastit::AiResultState state;
    state.start("action", "source", frozen);
    frozen.system_message="mutated";
    assert(state.active().frozen_request.system_message=="old");
    assert(!state.complete("other","ignored"));
    assert(state.complete("r1",std::string(5000,'x')));
    state.edit("changed");assert(state.active().editable_text=="changed");assert(state.active().source_text=="source");
    const auto retry=state.retry_request();assert(retry&&retry->system_message=="old");
    state.start("a2","s",pastit::TextGenerationRequest{.request_id="r2"});
    assert(state.fail("r2","failed"));assert(state.active().error=="failed");
}
