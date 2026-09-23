#include "render/renderer_service.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace pastit {
namespace {

std::string trim_copy(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }
    return std::string{value.substr(first, last - first)};
}

bool starts_with(std::string_view value, std::string_view prefix) {
    return value.substr(0, prefix.size()) == prefix;
}

bool is_mermaid_header(std::string_view line) {
    const auto trimmed = trim_copy(line);
    static constexpr std::string_view headers[] = {
        "flowchart", "graph", "sequenceDiagram", "classDiagram", "stateDiagram", "stateDiagram-v2",
        "erDiagram", "journey", "gantt", "pie", "mindmap", "timeline", "gitGraph",
        "quadrantChart", "requirementDiagram", "C4Context",
    };
    return std::any_of(std::begin(headers), std::end(headers), [&](std::string_view header) {
        return starts_with(trimmed, header);
    });
}

std::string ensure_trailing_newline(std::string value) {
    if (!value.empty() && value.back() != '\n') {
        value.push_back('\n');
    }
    return value;
}

std::string after_first_header(std::string_view source) {
    std::istringstream lines(std::string{source});
    std::string line;
    std::string output;
    bool found_header = false;
    while (std::getline(lines, line)) {
        if (!found_header) {
            if (!is_mermaid_header(line)) {
                continue;
            }
            found_header = true;
        }
        output += line;
        output.push_back('\n');
    }
    return found_header ? output : ensure_trailing_newline(trim_copy(source));
}

bool normalized_starts_with_header(std::string_view source) {
    std::istringstream lines(std::string{source});
    std::string first_line;
    return std::getline(lines, first_line) && is_mermaid_header(first_line);
}

std::string fenced_body(std::string_view source) {
    std::size_t search = 0;
    while (true) {
        const auto fence = source.find("```", search);
        if (fence == std::string_view::npos) {
            return {};
        }
        const auto info_start = fence + 3;
        const auto body_start = source.find('\n', info_start);
        if (body_start == std::string_view::npos) {
            return {};
        }
        const auto info = trim_copy(source.substr(info_start, body_start - info_start));
        const auto body_end = source.find("```", body_start + 1);
        const auto body = source.substr(body_start + 1, body_end == std::string_view::npos
                                                        ? std::string_view::npos
                                                        : body_end - body_start - 1);
        if ((info.empty() || info == "mermaid" || normalized_starts_with_header(body)) &&
            !trim_copy(body).empty()) {
            return ensure_trailing_newline(std::string{body});
        }
        if (body_end == std::string_view::npos) {
            return {};
        }
        search = body_end + 3;
    }
}

std::filesystem::path temporary_mermaid_path() {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("pastit-mermaid-" + std::to_string(std::random_device{}()) + "-" +
            std::to_string(ticks) + ".mmd");
}

std::string first_non_empty(std::initializer_list<std::string_view> values) {
    for (const auto value : values) {
        if (!value.empty()) {
            return std::string{value};
        }
    }
    return {};
}

bool process_missing(const ProcessOutput& output) {
    return output.exit_code == 127 || output.exit_code == 126 ||
           output.stderr_text.find("execvp failed") != std::string::npos ||
           output.stderr_text.find("not found") != std::string::npos;
}

std::string process_status(std::string_view tool, const ProcessOutput& output) {
    const auto detail = first_non_empty({output.stderr_text, output.stdout_text});
    if (output.exit_code == 0) {
        return std::string{tool} + " rendered successfully";
    }
    if (process_missing(output)) {
        return std::string{tool} + " unavailable" + (detail.empty() ? std::string{} : ": " + detail);
    }
    return std::string{tool} + " failed with exit code " + std::to_string(output.exit_code) +
           (detail.empty() ? std::string{} : ": " + detail);
}

void create_parent_directory(const std::filesystem::path& output) {
    const auto parent = output.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
    }
}

std::string tool_path(const std::filesystem::path& configured, std::string_view fallback) {
    return configured.empty() ? std::string{fallback} : configured.string();
}

}  // namespace

ExternalRendererService::ExternalRendererService(FastActionServices& services, RendererSettings settings)
    : services_(services), settings_(std::move(settings)) {}

std::string ExternalRendererService::normalize_mermaid_source(std::string_view source) {
    const auto fenced = fenced_body(source);
    if (!fenced.empty()) {
        return fenced;
    }
    return after_first_header(source);
}

RenderResult ExternalRendererService::render_mermaid(std::string_view source, const std::filesystem::path& output) {
    create_parent_directory(output);
    const auto input_path = temporary_mermaid_path();
    {
        std::ofstream input(input_path, std::ios::binary | std::ios::trunc);
        input << normalize_mermaid_source(source);
    }

    std::vector<std::string> argv;
    argv.push_back(tool_path(settings_.mermaid_cli_path, "mmdc"));
    argv.insert(argv.end(), settings_.mermaid_arguments.begin(), settings_.mermaid_arguments.end());
    argv.push_back("-i");
    argv.push_back(input_path.string());
    argv.push_back("-o");
    argv.push_back(output.string());
    const auto process = services_.run_argv(argv);
    std::error_code error;
    std::filesystem::remove(input_path, error);

    const bool missing = process_missing(process);
    return RenderResult{
        .available = !missing,
        .success = process.exit_code == 0,
        .output = output,
        .status = process_status("mmdc", process),
    };
}

RenderResult ExternalRendererService::render_qr(std::string_view payload, const std::filesystem::path& output) {
    create_parent_directory(output);
    const std::vector<std::string> argv = {
        tool_path(settings_.qrencode_path, "qrencode"),
        "-o", output.string(),
        "-t", "PNG",
        "-l", settings_.qr_error_correction,
        "-m", std::to_string(settings_.qr_margin),
        "-s", std::to_string(settings_.qr_scale),
        std::string{payload},
    };
    const auto process = services_.run_argv(argv);
    const bool missing = process_missing(process);
    return RenderResult{
        .available = !missing,
        .success = process.exit_code == 0,
        .output = output,
        .status = process_status("qrencode", process),
    };
}

}  // namespace pastit
