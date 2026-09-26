#include "actions/action_catalog.hpp"
#include "ai/prompt_expander.hpp"

#include <cassert>

int main() {
    using namespace pasteit;
    DecisionSnapshot snapshot;
    snapshot.clipboard_items = {
        {.ref="text", .kind=ContentKind::Text, .preview="Hello {world}"},
        {.ref="url", .kind=ContentKind::Url, .preview="https://example.com"},
    };
    std::vector<PromptTemplate> templates{
        {.id="translate",.name="Translate",.system_prompt="Translate {text} from {source_language} to {target_language}",.temperature=0.2,.enabled=true},
        {.id="rewrite",.name="Rewrite",.system_prompt="Rewrite this",.temperature=0.3,.enabled=true},
        {.id="off",.name="Off",.system_prompt="No",.temperature=0.1,.enabled=false},
    };
    ProviderSettings provider{.endpoint="http://local/v1/chat/completions",.model_id="model",.api_key="must-not-freeze"};
    const auto catalog=build_catalog(snapshot,templates,provider);
    int text_count=0,url_count=0;
    for(const auto& action:catalog.actions){
        if(action.kind!=ActionKind::TransformText)continue;
        if(action.source_ref=="text")++text_count;if(action.source_ref=="url")++url_count;
        assert(action.parameters.at("template_id")!="off");
        assert(action.parameters.at("llm_endpoint")==provider.endpoint);
        assert(action.parameters.at("llm_model_id")==provider.model_id);
        assert(!action.parameters.contains("api_key"));
    }
    assert(text_count==2);assert(url_count==0);
    assert(catalog.find("a_transform_text_text_translate").has_value());

    const auto expanded=expand_prompt(templates[0],"Hello {world}", {.source_language="English",.target_language="Chinese"});
    assert(expanded.system_message=="Translate Hello {world} from English to Chinese");
    assert(!expanded.user_message.empty());
    const PromptTemplate custom{.id="custom",.name="Custom",.system_prompt="Use {tone} for {text}; output {tone}.",.enabled=true};
    const auto names=prompt_variable_names(custom.system_prompt);
    assert((names==std::vector<std::string>{"tone"}));
    PromptVariables custom_values;
    custom_values.values["tone"]="formal";
    assert(expand_prompt(custom,"clipboard",custom_values).system_message=="Use formal for clipboard; output formal.");
    PromptTemplate literal{.id="x",.name="X",.system_prompt="Keep {unknown}",.enabled=true};
    const auto no_text=expand_prompt(literal,"source text",{});
    assert(no_text.system_message=="Keep {unknown}");
    assert(no_text.user_message=="source text");
}
