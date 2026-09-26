#include "ai/openai_compatible_client.hpp"
#include "ai/mermaid_prompt.hpp"
#include "config/settings_store.hpp"
#include <cstdlib>
#include <iostream>
int main(){
 pasteit::ProviderSettings provider;
 const char* endpoint=std::getenv("GENERAL_LLM_URL");if(!endpoint)endpoint=std::getenv("OPENAI_BASE_URL");
 const char* model=std::getenv("GENERAL_LLM_MODEL");if(!model)model=std::getenv("OPENAI_MODEL");
 const char* key=std::getenv("GENERAL_LLM_API_KEY");if(!key)key=std::getenv("OPENAI_API_KEY");
 const char* source="environment";
 if(endpoint)provider.endpoint=endpoint;if(model)provider.model_id=model;if(key)provider.api_key=key;
 if(provider.endpoint.empty()||provider.model_id.empty()||provider.api_key.empty()){
  const auto loaded=pasteit::SettingsStore{}.load();
  provider=loaded.settings.general_llm;
  source="saved_settings";
 }
 if(provider.endpoint.empty()||provider.model_id.empty()||provider.api_key.empty()){
  std::cout<<"SKIP: general LLM endpoint, model, or API key is not configured\n";return 77;
 }
 std::cout<<"provider_source="<<source<<"\nendpoint="<<pasteit::normalize_chat_completions_endpoint(provider.endpoint)<<"\nmodel="<<provider.model_id<<"\n";
 const auto request=pasteit::build_mermaid_generation_request("live-openai-mermaid",provider,
  "Service A calls Service B. Service B writes to the analytics database.");
 pasteit::OpenAiCompatibleClient client;const auto result=client.generate(request);
 if(!result.ok||result.content.empty()){std::cerr<<"general LLM live test failed: "<<result.error<<"\n";return 1;}
 const auto normalized=pasteit::normalize_mermaid_response(result.content);
 if(!normalized.ok){std::cerr<<"general LLM live test returned non-Mermaid content: "<<normalized.error<<"\n";return 1;}
 std::cout<<"general_llm mermaid ok: raw_bytes="<<normalized.raw_source.size()
          <<" normalized_bytes="<<normalized.normalized_source.size()<<"\n";
 return 0;
}
