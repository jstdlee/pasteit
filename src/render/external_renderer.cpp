#include "render/renderer_service.hpp"
#include "platform/app_paths.hpp"
#include "util/path_utf8.hpp"
#include "qrcodegen.hpp"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pasteit {
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

void create_parent_directory(const std::filesystem::path& output) {
    const auto parent = output.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
    }
}

std::string html_escape(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&#39;"; break;
        default: result.push_back(ch); break;
        }
    }
    return result;
}

std::string inline_script(std::string script) {
    // A literal closing tag inside the bundle must not terminate the HTML script element.
    for (std::size_t pos = 0; (pos = script.find("</script", pos)) != std::string::npos; pos += 9) {
        script.replace(pos, 8, "<\\/script");
    }
    return script;
}

// Offline Mermaid viewer: the bundled mermaid.js renders the diagram in the
// page, which adds pan (drag), zoom (wheel, +/-), fit, a minimap, a source
// panel and SVG/PNG export. Split into pieces to stay under MSVC's
// string-literal limit.
constexpr const char* kMermaidPageHead =
    "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";

constexpr const char* kMermaidPageStyle = R"PASTEIT(<style>
:root{--bg:#f6f7f9;--panel:#fff;--text:#1d2330;--muted:#667085;--line:#d9dde5;--accent:#3b6fe0;--dot:#d5d9e1;--danger:#b42318}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#15171c;--panel:#1e2128;--text:#e6e8ee;--muted:#9aa3b2;--line:#323743;--accent:#7aa2ff;--dot:#2a2f3a;--danger:#ff8a80}}
:root[data-theme=dark]{--bg:#15171c;--panel:#1e2128;--text:#e6e8ee;--muted:#9aa3b2;--line:#323743;--accent:#7aa2ff;--dot:#2a2f3a;--danger:#ff8a80}
*{box-sizing:border-box}html,body{height:100%;margin:0}
body{font:14px/1.4 system-ui,-apple-system,"Segoe UI",sans-serif;background:var(--bg);color:var(--text);display:flex;flex-direction:column;overflow:hidden}
header{display:flex;align-items:center;gap:6px;padding:8px 12px;background:var(--panel);border-bottom:1px solid var(--line);flex-wrap:wrap}
header h1{font-size:14px;font-weight:600;margin:0 10px 0 0}
.group{display:flex;align-items:center;gap:2px;padding-right:8px;margin-right:2px;border-right:1px solid var(--line)}
.group:last-of-type{border-right:0}
button{font:inherit;color:var(--text);background:transparent;border:1px solid transparent;border-radius:6px;padding:4px 9px;cursor:pointer;min-width:30px}
button:hover{background:var(--bg);border-color:var(--line)}
button[aria-pressed=true]{border-color:var(--accent);color:var(--accent)}
#zoom-label{min-width:52px;text-align:center;font-variant-numeric:tabular-nums;color:var(--muted)}
#status{margin-left:auto;color:var(--muted);font-size:12px}
main{flex:1;display:flex;min-height:0}
#stage{flex:1;position:relative;overflow:hidden;cursor:grab;touch-action:none;background-image:radial-gradient(var(--dot) 1px,transparent 1px);background-size:20px 20px}
#stage.dragging{cursor:grabbing}
#canvas{position:absolute;left:0;top:0;transform-origin:0 0}
#canvas pre.mermaid{margin:0;background:transparent;visibility:hidden}
#canvas pre.mermaid[data-processed]{visibility:visible}
#canvas svg{display:block;max-width:none!important}
#source-panel{width:min(40%,480px);display:none;flex-direction:column;border-left:1px solid var(--line);background:var(--panel)}
#source-panel.open{display:flex}
#source-text{flex:1;margin:0;padding:12px;overflow:auto;font:12px/1.5 ui-monospace,Menlo,Consolas,monospace;white-space:pre}
#minimap{position:absolute;right:12px;bottom:12px;width:200px;height:140px;background:var(--panel);border:1px solid var(--line);border-radius:8px;overflow:hidden;cursor:pointer;box-shadow:0 2px 8px rgba(0,0,0,.15)}
#minimap.hidden{display:none}
#minimap img{position:absolute;pointer-events:none}
#minimap-view{position:absolute;border:2px solid var(--accent);background:rgba(59,111,224,.12);pointer-events:none}
#error{display:none;position:absolute;left:16px;right:16px;top:16px;padding:12px;border:1px solid var(--danger);border-radius:8px;background:var(--panel);color:var(--danger);white-space:pre-wrap;font:12px/1.5 ui-monospace,Menlo,Consolas,monospace}
#hint{position:absolute;left:12px;bottom:12px;color:var(--muted);font-size:12px;background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:3px 8px}
</style></head>)PASTEIT";

constexpr const char* kMermaidPageBody = R"PASTEIT(<body>
<header>
<h1>Mermaid diagram</h1>
<div class="group"><button id="zoom-out" title="Zoom out (-)">&minus;</button><span id="zoom-label">100%</span><button id="zoom-in" title="Zoom in (+)">+</button></div>
<div class="group"><button id="fit" title="Fit to window (F, double-click)">Fit</button><button id="actual" title="Actual size (0)">1:1</button><button id="minimap-toggle" aria-pressed="true" title="Minimap (M)">Map</button></div>
<div class="group"><button id="source-toggle" aria-pressed="false" title="Show source (S)">Source</button><button id="copy-source" title="Copy Mermaid source">Copy</button></div>
<div class="group"><button id="save-svg" title="Download SVG">SVG</button><button id="save-png" title="Download PNG">PNG</button></div>
<div class="group"><button id="theme" title="Switch light / dark (T)">Theme</button></div>
<span id="status"></span>
</header>
<main><div id="stage"><div id="canvas"><pre class="mermaid">)PASTEIT";

constexpr const char* kMermaidPageAfterSource = R"PASTEIT(</pre></div>
<div id="error" role="alert"></div>
<div id="hint">Drag to pan &middot; wheel to zoom &middot; double-click to fit</div>
<div id="minimap" title="Click or drag to navigate"><img id="minimap-image" alt=""><div id="minimap-view"></div></div>
</div><aside id="source-panel"><pre id="source-text"></pre></aside></main>
<script>)PASTEIT";

constexpr const char* kMermaidPageViewer = R"PASTEIT(</script><script>
(() => {
  const $ = (id) => document.getElementById(id);
  const stage = $("stage"), canvas = $("canvas"), pre = canvas.querySelector("pre.mermaid");
  const source = pre.textContent;
  $("source-text").textContent = source;
  let scale = 1, x = 0, y = 0, width = 0, height = 0, svg = null, moved = false;
  const status = (text) => { $("status").textContent = text; };
  const isDark = () => {
    const theme = document.documentElement.dataset.theme;
    return theme ? theme === "dark" : matchMedia("(prefers-color-scheme: dark)").matches;
  };
  const clamp = (value, low, high) => Math.min(high, Math.max(low, value));

  function apply() {
    canvas.style.transform = `translate(${x}px, ${y}px) scale(${scale})`;
    $("zoom-label").textContent = Math.round(scale * 100) + "%";
    updateMinimap();
  }
  function zoomAt(factor, cx, cy) {
    const next = clamp(scale * factor, 0.05, 8), ratio = next / scale;
    x = cx - (cx - x) * ratio; y = cy - (cy - y) * ratio; scale = next; moved = true; apply();
  }
  const middle = () => [stage.clientWidth / 2, stage.clientHeight / 2];
  function fit() {
    if (!width) return;
    const pad = 32;
    scale = clamp(Math.min((stage.clientWidth - pad * 2) / width, (stage.clientHeight - pad * 2) / height), 0.05, 2);
    x = (stage.clientWidth - width * scale) / 2; y = (stage.clientHeight - height * scale) / 2;
    moved = false; apply();
  }

  // Minimap: the diagram as an image plus the visible region.
  let mini = { scale: 1, left: 0, top: 0 };
  function buildMinimap() {
    const box = $("minimap"), image = $("minimap-image");
    const factor = Math.min((box.clientWidth - 12) / width, (box.clientHeight - 12) / height);
    mini = { scale: factor, left: (box.clientWidth - width * factor) / 2, top: (box.clientHeight - height * factor) / 2 };
    image.src = "data:image/svg+xml;charset=utf-8," + encodeURIComponent(serialize());
    Object.assign(image.style, { left: mini.left + "px", top: mini.top + "px",
                                 width: width * factor + "px", height: height * factor + "px" });
    updateMinimap();
  }
  function updateMinimap() {
    if (!width) return;
    const view = $("minimap-view");
    Object.assign(view.style, {
      left: mini.left + (-x / scale) * mini.scale + "px", top: mini.top + (-y / scale) * mini.scale + "px",
      width: (stage.clientWidth / scale) * mini.scale + "px", height: (stage.clientHeight / scale) * mini.scale + "px" });
  }
  function centerOnMinimap(event) {
    const rect = $("minimap").getBoundingClientRect();
    const px = (event.clientX - rect.left - mini.left) / mini.scale, py = (event.clientY - rect.top - mini.top) / mini.scale;
    x = stage.clientWidth / 2 - px * scale; y = stage.clientHeight / 2 - py * scale; moved = true; apply();
  }

  function serialize() {
    const copy = svg.cloneNode(true);
    copy.setAttribute("xmlns", "http://www.w3.org/2000/svg");
    copy.setAttribute("width", width); copy.setAttribute("height", height);
    copy.style.backgroundColor = isDark() ? "#15171c" : "#ffffff";
    return new XMLSerializer().serializeToString(copy);
  }
  function download(blob, name) {
    const link = document.createElement("a");
    link.href = URL.createObjectURL(blob); link.download = name; link.click();
    setTimeout(() => URL.revokeObjectURL(link.href), 1000);
  }

  async function render() {
    pre.removeAttribute("data-processed");
    pre.textContent = source;
    $("error").style.display = "none";
    mermaid.initialize({ startOnLoad: false, securityLevel: "strict", theme: isDark() ? "dark" : "default",
                         htmlLabels: false, flowchart: { htmlLabels: false } });
    try {
      await mermaid.run({ nodes: [pre] });
    } catch (error) {
      $("error").textContent = String(error && error.message || error);
      $("error").style.display = "block";
      $("source-panel").classList.add("open"); $("source-toggle").setAttribute("aria-pressed", "true");
      status("Could not render");
      return;
    }
    svg = pre.querySelector("svg");
    const box = svg.viewBox.baseVal;
    width = box && box.width ? box.width : svg.getBBox().width;
    height = box && box.height ? box.height : svg.getBBox().height;
    svg.setAttribute("width", width); svg.setAttribute("height", height);
    svg.style.maxWidth = "none";
    status(Math.round(width) + " \u00d7 " + Math.round(height));
    fit(); buildMinimap();
  }

  // Pan with the pointer; the minimap navigates instead.
  let drag = null;
  stage.addEventListener("pointerdown", (event) => {
    if (event.button !== 0) return;
    if ($("minimap").contains(event.target)) { drag = { minimap: true }; centerOnMinimap(event); }
    else drag = { x: event.clientX - x, y: event.clientY - y };
    stage.setPointerCapture(event.pointerId); stage.classList.toggle("dragging", !drag.minimap);
  });
  stage.addEventListener("pointermove", (event) => {
    if (!drag) return;
    if (drag.minimap) { centerOnMinimap(event); return; }
    x = event.clientX - drag.x; y = event.clientY - drag.y; moved = true; apply();
  });
  const endDrag = () => { drag = null; stage.classList.remove("dragging"); };
  stage.addEventListener("pointerup", endDrag);
  stage.addEventListener("pointercancel", endDrag);
  stage.addEventListener("wheel", (event) => {
    event.preventDefault();
    const rect = stage.getBoundingClientRect();
    zoomAt(Math.exp(-event.deltaY * (event.ctrlKey ? 0.01 : 0.0015)), event.clientX - rect.left, event.clientY - rect.top);
  }, { passive: false });
  stage.addEventListener("dblclick", (event) => { if (!$("minimap").contains(event.target)) fit(); });

  const toggleSource = () => {
    const open = $("source-panel").classList.toggle("open");
    $("source-toggle").setAttribute("aria-pressed", String(open));
    requestAnimationFrame(() => (moved ? apply() : fit()));
  };
  const toggleMinimap = () => {
    const hidden = $("minimap").classList.toggle("hidden");
    $("minimap-toggle").setAttribute("aria-pressed", String(!hidden));
  };
  const switchTheme = () => { document.documentElement.dataset.theme = isDark() ? "light" : "dark"; render(); };
  async function copySource() {
    try { await navigator.clipboard.writeText(source); }
    catch (_) {
      const area = document.createElement("textarea");
      area.value = source; document.body.append(area); area.select(); document.execCommand("copy"); area.remove();
    }
    status("Source copied");
  }
  function savePng() {
    if (!svg) return;
    const image = new Image(), ratio = 2;
    image.onload = () => {
      const board = document.createElement("canvas");
      board.width = Math.ceil(width * ratio); board.height = Math.ceil(height * ratio);
      const context = board.getContext("2d");
      context.scale(ratio, ratio); context.drawImage(image, 0, 0, width, height);
      board.toBlob((blob) => blob && download(blob, "diagram.png"), "image/png");
    };
    image.src = "data:image/svg+xml;charset=utf-8," + encodeURIComponent(serialize());
  }

  $("zoom-in").onclick = () => zoomAt(1.25, ...middle());
  $("zoom-out").onclick = () => zoomAt(0.8, ...middle());
  $("fit").onclick = fit;
  $("actual").onclick = () => zoomAt(1 / scale, ...middle());
  $("minimap-toggle").onclick = toggleMinimap;
  $("source-toggle").onclick = toggleSource;
  $("copy-source").onclick = copySource;
  $("save-svg").onclick = () => svg && download(new Blob([serialize()], { type: "image/svg+xml" }), "diagram.svg");
  $("save-png").onclick = savePng;
  $("theme").onclick = switchTheme;
  document.addEventListener("keydown", (event) => {
    if (event.ctrlKey || event.metaKey || event.altKey) return;
    const pan = 80, [cx, cy] = middle();
    switch (event.key) {
      case "+": case "=": zoomAt(1.25, cx, cy); break;
      case "-": case "_": zoomAt(0.8, cx, cy); break;
      case "0": zoomAt(1 / scale, cx, cy); break;
      case "f": case "F": fit(); break;
      case "m": case "M": toggleMinimap(); break;
      case "s": case "S": toggleSource(); break;
      case "t": case "T": switchTheme(); break;
      case "ArrowLeft": x += pan; moved = true; apply(); break;
      case "ArrowRight": x -= pan; moved = true; apply(); break;
      case "ArrowUp": y += pan; moved = true; apply(); break;
      case "ArrowDown": y -= pan; moved = true; apply(); break;
      default: return;
    }
    event.preventDefault();
  });
  addEventListener("resize", () => (moved ? apply() : fit()));
  render();
})();
</script></body></html>)PASTEIT";

qrcodegen::QrCode::Ecc qr_ecc(std::string_view value) {
    if (value == "L") return qrcodegen::QrCode::Ecc::LOW;
    if (value == "Q") return qrcodegen::QrCode::Ecc::QUARTILE;
    if (value == "H") return qrcodegen::QrCode::Ecc::HIGH;
    return qrcodegen::QrCode::Ecc::MEDIUM;
}

void png_write_callback(void* context, void* data, int size) {
    static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
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
    auto input_path = output;
    input_path.replace_extension(".mmd");
    auto png_path = output;
    png_path.replace_extension(".png");
    {
        std::ofstream input(input_path, std::ios::binary | std::ios::trunc);
        if (input) {
            input << normalize_mermaid_source(source);
            input.close();
            std::vector<std::string> argv{
                settings_.mermaid_cli_path.empty() ? "mmdc" : path_to_utf8_string(settings_.mermaid_cli_path),
                "-i", path_to_utf8_string(input_path), "-o", path_to_utf8_string(png_path),
            };
            argv.insert(argv.end(), settings_.mermaid_arguments.begin(), settings_.mermaid_arguments.end());
            const auto process = services_.run_argv(argv);
            std::error_code ignored;
            std::filesystem::remove(input_path, ignored);
            if (process.exit_code == 0 && std::filesystem::is_regular_file(png_path)) {
                return {.available = true, .success = true, .output = png_path,
                        .status = "Mermaid PNG ready"};
            }
            std::filesystem::remove(png_path, ignored);
        }
    }
    std::ifstream bundle(executable_directory() / "mermaid.min.js", std::ios::binary);
    if (!bundle) return {.available = false, .success = false, .output = output,
                         .status = "Bundled Mermaid JavaScript unavailable"};
    const std::string script((std::istreambuf_iterator<char>(bundle)), {});
    if (script.empty() || bundle.bad()) return {.available = false, .success = false, .output = output,
                                                .status = "Bundled Mermaid JavaScript could not be read"};
    std::ofstream page(output, std::ios::binary | std::ios::trunc);
    if (!page) return {.available = true, .success = false, .output = output,
                       .status = "Could not open Mermaid HTML output"};
    // The page title carries a per-render token so the platform layer can find
    // (and raise) the browser window showing it.
    char token[16]{};
    std::snprintf(token, sizeof(token), "%06zx",
                  std::hash<std::string>{}(path_to_utf8_string(output.filename())) & 0xFFFFFFU);
    page << kMermaidPageHead << "<title>Mermaid diagram - " << token << "</title>" << kMermaidPageStyle
         << kMermaidPageBody << html_escape(normalize_mermaid_source(source)) << kMermaidPageAfterSource
         << inline_script(script) << kMermaidPageViewer;
    page.close();
    return {.available = true, .success = static_cast<bool>(page), .output = output,
            .status = page ? "Offline Mermaid HTML ready" : "Could not write Mermaid HTML"};
}

RenderResult ExternalRendererService::render_qr(std::string_view payload, const std::filesystem::path& output) {
    create_parent_directory(output);
    try {
        if (payload.empty()) return {.available = true, .success = false, .output = output,
                                     .status = "QR payload is empty"};
        const std::vector<std::uint8_t> bytes(payload.begin(), payload.end());
        const auto code = qrcodegen::QrCode::encodeBinary(bytes, qr_ecc(settings_.qr_error_correction));
        const int margin = std::clamp(settings_.qr_margin, 0, 64);
        const int scale = std::clamp(settings_.qr_scale, 1, 64);
        const int pixels = (code.getSize() + margin * 2) * scale;
        if (pixels > 8192) return {.available = true, .success = false, .output = output,
                                   .status = "QR image would exceed 8192 pixels; lower the scale"};
        std::vector<std::uint8_t> raster(static_cast<std::size_t>(pixels) * pixels, 255);
        for (int y = 0; y < pixels; ++y) {
            for (int x = 0; x < pixels; ++x) {
                const int module_x = x / scale - margin;
                const int module_y = y / scale - margin;
                if (module_x >= 0 && module_x < code.getSize() &&
                    module_y >= 0 && module_y < code.getSize() && code.getModule(module_x, module_y)) {
                    raster[static_cast<std::size_t>(y) * pixels + x] = 0;
                }
            }
        }
        std::ofstream image(output, std::ios::binary | std::ios::trunc);
        if (!image) return {.available = true, .success = false, .output = output,
                            .status = "Could not open QR PNG output"};
        const bool encoded = stbi_write_png_to_func(png_write_callback, &image, pixels, pixels, 1,
                                                    raster.data(), pixels) != 0;
        image.close();
        return {.available = true, .success = encoded && static_cast<bool>(image), .output = output,
                .status = encoded && image ? "QR PNG ready" : "Could not write QR PNG"};
    } catch (const std::exception& error) {
        return {.available = true, .success = false, .output = output,
                .status = std::string{"QR encoding failed: "} + error.what()};
    }
}

}  // namespace pasteit
