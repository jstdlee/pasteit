#include "ai/mermaid_prompt.hpp"

#include "util/utf8.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>

namespace pasteit {
namespace {

constexpr std::array<std::string_view, 14> kSupportedHeaders{
    "graph ",
    "flowchart ",
    "sequenceDiagram",
    "classDiagram",
    "stateDiagram",
    "stateDiagram-v2",
    "erDiagram",
    "gantt",
    "journey",
    "pie",
    "gitGraph",
    "mindmap",
    "timeline",
    "requirementDiagram",
};

bool header_matches(std::string_view value, std::string_view header) {
    if (value.rfind(header, 0) != 0) {
        return false;
    }
    if (!header.empty() && std::isspace(static_cast<unsigned char>(header.back()))) {
        return true;
    }
    if (value.size() == header.size()) {
        return true;
    }
    const auto next = static_cast<unsigned char>(value[header.size()]);
    return std::isspace(next) || next == ':' || next == '\r' || next == '\n';
}

std::string lowercase_ascii(std::string_view value) {
    std::string out{value};
    for (auto& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

bool contains_mermaid_syntax(std::string_view line) {
    return line.find("--") != std::string_view::npos || line.find("->") != std::string_view::npos ||
           line.find(":") != std::string_view::npos || line.find("[") != std::string_view::npos ||
           line.find("]") != std::string_view::npos || line.find("{") != std::string_view::npos ||
           line.find("}") != std::string_view::npos || line.find("(") != std::string_view::npos ||
           line.find(")") != std::string_view::npos || line.find("|") != std::string_view::npos;
}

bool looks_like_trailing_prose(std::string_view line) {
    const auto lower = lowercase_ascii(line);
    if (lower == "thanks." || lower == "thanks" || lower.starts_with("hope this") ||
        lower.starts_with("explanation") || lower.starts_with("note:") ||
        lower.starts_with("the diagram") || lower.starts_with("this diagram")) {
        return true;
    }
    return lower.ends_with('.') && !contains_mermaid_syntax(lower);
}

std::string trim_copy(std::string_view input) {
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front()))) {
        input.remove_prefix(1);
    }
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back()))) {
        input.remove_suffix(1);
    }
    return std::string{input};
}

std::string first_supported_block(std::string_view text) {
    std::istringstream lines{std::string{text}};
    std::string line;
    std::string out;
    bool copying = false;
    bool saw_body = false;
    while (std::getline(lines, line)) {
        const auto trimmed_line = trim_copy(line);
        if (!copying && has_supported_mermaid_header(trimmed_line)) {
            copying = true;
        }
        if (copying) {
            if (saw_body && looks_like_trailing_prose(trimmed_line)) {
                break;
            }
            out += line;
            out.push_back('\n');
            if (!trimmed_line.empty() && !has_supported_mermaid_header(trimmed_line)) {
                saw_body = true;
            }
        }
    }
    return trim_copy(out);
}

std::string fenced_content(std::string_view text) {
    const auto open = text.find("```");
    if (open == std::string_view::npos) {
        return {};
    }
    auto content_start = text.find('\n', open + 3);
    if (content_start == std::string_view::npos) {
        return {};
    }
    ++content_start;
    const auto close = text.find("```", content_start);
    if (close == std::string_view::npos) {
        return {};
    }
    return std::string{text.substr(content_start, close - content_start)};
}

}  // namespace

bool has_supported_mermaid_header(std::string_view source) {
    const auto trimmed = trim_copy(source);
    for (const auto header : kSupportedHeaders) {
        if (header_matches(trimmed, header)) {
            return true;
        }
    }
    return false;
}

TextGenerationRequest build_mermaid_generation_request(std::string request_id,
                                                       const ProviderSettings& provider,
                                                       std::string_view source_text) {
    const auto bounded = utf8_prefix_bytes(sanitize_utf8(source_text), kMermaidInputBudgetBytes);
    TextGenerationRequest request;
    request.request_id = std::move(request_id);
    request.endpoint = provider.endpoint;
    request.api_key = provider.api_key;
    request.model_id = provider.model_id;
    request.temperature = 0.0;
    request.system_message =
        "Generate Mermaid source only. Return Mermaid source only, without Markdown fences, "
        "without code blocks, and with no explanatory prose. Start with a supported Mermaid "
        "diagram header such as flowchart TD, graph TD, sequenceDiagram, classDiagram, "
        "stateDiagram-v2, erDiagram, timeline, mindmap, gantt, journey, pie, or gitGraph.";
    request.user_message =
        "Convert this clipboard content into concise Mermaid source. Use valid node and edge syntax.\n"
        "Clipboard content:\n" +
        bounded;
    return request;
}

MermaidNormalizationResult normalize_mermaid_response(std::string raw_source) {
    MermaidNormalizationResult result;
    result.raw_source = std::move(raw_source);

    auto candidate = fenced_content(result.raw_source);
    if (candidate.empty()) {
        candidate = std::string{result.raw_source};
    }
    candidate = first_supported_block(candidate);
    if (candidate.empty()) {
        result.error = "Mermaid response did not contain a supported Mermaid diagram header";
        return result;
    }
    if (!has_supported_mermaid_header(candidate)) {
        result.error = "Mermaid response did not start with a supported Mermaid diagram header";
        return result;
    }
    if (candidate.find("```") != std::string::npos) {
        result.error = "Mermaid response still contains Markdown fences";
        return result;
    }
    result.normalized_source = std::move(candidate);
    if (!result.normalized_source.ends_with('\n')) {
        result.normalized_source.push_back('\n');
    }
    result.ok = true;
    return result;
}

}  // namespace pasteit
