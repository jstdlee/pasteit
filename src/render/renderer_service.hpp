#pragma once

#include "config/app_settings.hpp"
#include "platform/fast_action_services.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace pasteit {

struct RenderResult {
    bool available = false;
    bool success = false;
    std::filesystem::path output;
    std::string status;
};

class RendererService {
public:
    virtual ~RendererService() = default;
    virtual RenderResult render_mermaid(std::string_view source,
                                        const std::filesystem::path& output) = 0;
    virtual RenderResult render_qr(std::string_view payload,
                                   const std::filesystem::path& output) = 0;
};

class ExternalRendererService final : public RendererService {
public:
    explicit ExternalRendererService(FastActionServices& services, RendererSettings settings = {});

    RenderResult render_mermaid(std::string_view source, const std::filesystem::path& output) override;
    RenderResult render_qr(std::string_view payload, const std::filesystem::path& output) override;

    static std::string normalize_mermaid_source(std::string_view source);

private:
    FastActionServices& services_;
    RendererSettings settings_;
};

}  // namespace pasteit
