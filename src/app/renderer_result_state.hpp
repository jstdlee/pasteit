#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace pasteit {

enum class RendererResultKind { Unknown, Mermaid, Qr, Annotation, ExplainText, ExplainCode };
enum class RendererResultStatus { Idle, Rendering, Ready, Unavailable, Failed };

struct GeneratedMermaidSource {
    std::string clipboard_source_ref;
    std::string original_clipboard_source;
    std::string raw_model_response;
    std::string normalized_mermaid_source;
};

struct RendererResult {
    std::string action_id;
    RendererResultKind kind = RendererResultKind::Unknown;
    std::string source;
    std::string payload;
    bool available = false;
    RendererResultStatus status = RendererResultStatus::Idle;
    std::optional<std::filesystem::path> output_path;
    std::string error;
    std::optional<GeneratedMermaidSource> generated_mermaid = std::nullopt;
};

class RendererResultState {
public:
    void prepare(RendererResult result);
    void fail(std::string action_id, RendererResultKind kind, std::string source, std::string error);
    std::string copy_text() const;
    const RendererResult& active() const { return active_; }

private:
    RendererResult active_;
};

bool rendered_output_decodes(RendererResultKind kind, const std::filesystem::path& path);

}  // namespace pasteit
