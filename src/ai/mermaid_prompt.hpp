#pragma once

#include "ai/openai_compatible_client.hpp"
#include "config/app_settings.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace pasteit {

inline constexpr std::size_t kMermaidInputBudgetBytes = 4096;

struct MermaidNormalizationResult {
    std::string raw_source;
    std::string normalized_source;
    bool ok = false;
    std::string error;
};

TextGenerationRequest build_mermaid_generation_request(std::string request_id,
                                                       const ProviderSettings& provider,
                                                       std::string_view source_text);

MermaidNormalizationResult normalize_mermaid_response(std::string raw_source);

bool has_supported_mermaid_header(std::string_view source);

}  // namespace pasteit
