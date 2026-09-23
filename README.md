# PasteIt

PasteIt is a local Linux/X11 clipboard action popup. It observes clipboard content, builds deterministic local action instances, asks a local Djev endpoint to rank a bounded request-local catalog, and executes only fixed native actions. V2 also supports editable text transformations through a separately configured OpenAI-compatible provider.

## Build Prerequisites

- CMake 3.20+
- A C++20 compiler
- X11/OpenGL development headers for the desktop popup
- libcurl development package is required for HTTP(S) URL downloads. Other HTTPS requests can use the installed `curl` executable when libcurl is unavailable.
- Optional `mmdc` and `qrencode` executables for Mermaid and QR image previews. When they are missing, PasteIt still opens a result window with copyable source or payload.

GLFW 3.4, Dear ImGui 1.91.9b-docking, and `stb_image.h` are fetched automatically by CMake at pinned revisions, matching the earlier `imgui_hf_demo` build. The docking tag supplies Dear ImGui's multi-viewport platform-window support; they do not need to be installed system-wide. On Debian/Ubuntu systems without development headers, prepare a project-private sysroot without `sudo`:

```bash
./scripts/bootstrap-local-deps.sh
```

This downloads and extracts headers only under `.deps/`. The resulting `pastit` binary uses the desktop's existing versioned X11/OpenGL runtime libraries and has no `.deps` RPATH.

## Djev Configuration

- `DJEV_URL` defaults to `http://127.0.0.1:8011`
- `DJEV_MODEL` defaults to `jev-latest`
- `DJEV_API_KEY` is optional for local deployments that require bearer authentication; `API_KEY` is accepted as the local demo fallback
- The desktop app reads these values from `.env` (or `PASTIT_ENV_FILE`) without printing the token

The app posts to `{DJEV_URL}/v1/systemone`. A full endpoint is accepted unchanged. Djev receives only the current clipboard item, the relevant target context, represented destination paths, and at most 26 compact action alternatives. Clipboard history and the duplicate full action catalog are never sent. The serialized request is capped at 10 KiB; a deterministic local fallback remains available if Djev fails. Djev returns action IDs and probabilities only, and PasteIt executes the matching fixed native action rather than model-generated shell commands.

The local `djev-spark` deployment used by this project is configured independently of the hosted TypeSafe/Jev service described in the [TypeSafe documentation](https://docs.typesafe.ai/introduction). Its context window is controlled by `MAX_MODEL_LEN` in both `/home/user/dev/djev-spark/.env` and this project's `.env`; the verified setting is `8192`, which starts vLLM with `--max-model-len 8192`. PasteIt allows 30 seconds for a local Djev decision because a four-read inference can take several seconds even after startup.

After changing the local context setting, restart from `/home/user/dev/djev-spark` with `docker compose up -d`, wait for port 8011, and run `./build-v3/djev_live_integration`. If 8192 cannot start on another machine, restore `MAX_MODEL_LEN=4096` in both files, rerun the same Compose command, and repeat the live test. Do not copy API keys into command lines or logs.

## Settings and General LLM

Settings are saved atomically at `${XDG_CONFIG_HOME:-~/.config}/pastit/settings.json`; data/history lives under `${XDG_DATA_HOME:-~/.local/share}/pastit`. If an interim build left `settings.json` beside the executable, the default settings store can read it once and saving migrates the values back to the XDG path. The Settings tab groups language, opacity, default image/text directories, and history controls under General. It also configures Djev endpoint/model/key and a separate OpenAI-compatible endpoint/model/key. The editable General LLM model field is paired with a dropdown loaded from the provider's `/v1/models` endpoint; a failed refresh leaves the manually entered model unchanged. A base general-LLM URL is normalized to `/v1/chat/completions`; a complete endpoint is used unchanged. OpenCode Go endpoints automatically receive a per-request `x-opencode-session` header.

The Fast actions settings section configures optional `mmdc` path/arguments, `qrencode` path/error correction/margin/scale, download resume directory, `.part` retention on cancel, terminal command argv, hash actions shown by default, source/target time zones, and annotation save directory. Annotation export is currently SVG-only; PNG/JPG preferences are normalized to SVG rather than writing SVG bytes with a raster suffix. Terminal profile and 24-hour date display are shown as stored preferences until a platform adapter consumes them.

Prompt Templates supports create, edit, delete, duplicate, enable/disable, and restore defaults. Translate, Rewrite, and Summarize are included. `{text}` is filled from the current clipboard; every other named `{variable}` placeholder opens a parameter dialog before the request is sent. Parameter values are prefilled from the current session's per-template memory and are frozen into the generated request after confirmation. Results open in an independent editable window with Copy Result, Replace Clipboard, and Retry.

Selecting an action also records a small persistent preference bonus in `settings.json`. The key is semantic: action kind, plus a destination path for file/path actions and a prompt-template ID for text transformations; transient action IDs and clipboard contents are not used. The bonus uses diminishing returns and is capped at 0.20, so a repeated choice can move a matching candidate upward without overwhelming the Djev probability or local fallback ranking.

## Build And Test

```bash
cmake -S . -B build -DPASTIT_BUILD_TESTS=ON -DPASTIT_BUILD_DESKTOP=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The ordinary CTest suite is offline and does not require a Djev or general-LLM server. X11 integration probes skip only when the display or shortcut is unavailable.

To run the required live local Djev integration check:

```bash
cmake --build build --target djev_live_integration
./build/djev_live_integration
```

The live check reads `DJEV_URL`, `DJEV_MODEL`, `DJEV_API_KEY`, `API_KEY`, and `TYPESAFE_API_KEY` from the environment, or from `.env` when the environment value is not already set. It does not print tokens or clipboard bodies. It verifies that 49 historical text/image/email records do not enter a current-text request, and sends a bounded 26-item catalog whose body remains under 10 KiB, reproducing and preventing the former HTTP 422/context-overflow condition.

An optional `openai_live_integration` uses `GENERAL_LLM_URL`, `GENERAL_LLM_MODEL`, and `GENERAL_LLM_API_KEY` (or the corresponding `OPENAI_*` names). It exits with skip code 77 when they are absent.

## Supported Content Families

- Text: paste, save-to-file, contact/business-card extraction, Mermaid generation for diagram-like text, QR payload fallback for short text, Explain text/code, Translate, Rewrite, Summarize, and custom-template actions, all ranked together in the main action list
- URL: paste, open, generate QR, copy GitHub HTTPS/SSH remotes, clone GitHub repositories, download, and save the URL response bytes to a target file; Save URL File does not save the URL string as a `.url` text file
- Email: paste, save `.eml`, compose actions, and QR payload generation
- Image: paste and save original bytes; clicking the thumbnail opens an aspect-preserving, zoomable preview, with actions to copy a temporary preview-file path and open SVG annotation tools
- Local path and `file://`: copy into a recent directory, move into a recent directory, open Terminal at the containing directory, and hash regular files with the enabled SHA-256/SHA-512 defaults; the redundant Copy Path action is omitted because the path is already in the clipboard
- JSON: raw save, pretty copy, and pretty save
- Resume-like text: copy extracted fields, copy extracted JSON, and save extracted JSON
- IP addresses: ping, traceroute, reverse DNS, dig, and a combined report. Linux maps these to argv calls for `ping -c 4`, `traceroute`, `getent hosts`, and `dig`; clipboard text is never shell-concatenated.
- Date/time values: timezone conversion, Unix timestamp conversion, and normalized copy. The Settings tab supplies the default source and target zones used by the desktop catalog.

URL downloads run through a `DownloadManager` and write `<target>.part` until completion. Resume reuses an existing `.part` file, sends a Range request from its current size, appends only after a valid `206 Partial Content`, and safely restarts when the server answers `200 OK` without range support. Pause preserves `.part`; Cancel preserves or discards it according to the Settings toggle.

Mermaid and QR renderers are optional process adapters. Missing `mmdc` or `qrencode` opens the same independent preview window with full status text and copyable source/payload, so the action remains useful without those tools.

Annotation export keeps the overlay non-destructive while editing. The current exporter writes a self-contained SVG that embeds the original image and overlays; raster PNG/JPG export is intentionally not advertised until a real encoder path exists.

Compose opens the local `mailto:` handler. Direct email sending is enabled only when `PASTIT_SENDMAIL` names a local executable; PasteIt invokes it with the destination address as its first argument.

## Desktop Popup

`pastit` is the desktop entry point. A per-user advisory lock prevents duplicate V2 processes. Start it once, then press `Ctrl+Alt+F` to capture the focused X11 target and open the popup. The movable, opacity-configurable popup shows the top eight Djev/fallback actions; click one or use Up/Down and Enter. The root window is a normal decorated window, so other applications can cover it; Escape closes without executing.

The Smart Actions tab abbreviates large text and uses image thumbnails. Enabled text prompt templates are ranked directly in the same action list as paste/save actions. The persistent Clipboard History tab retains the newest 50 text or image records; rows use content-type icons and open a detail view with the complete selectable/copyable text, while image details use the original stored bytes. Text metadata is sanitized and truncated only at UTF-8 character boundaries, and legacy malformed previews are normalized when history loads. The Recent Paths tab uses a resizable wide path column; long row paths preserve their prefix and filename suffix while abbreviating the middle with `...`. Row details put path operations in a top toolbar and use a scrollable body so long paths and actions remain reachable. Runtime recent-path sources are named file managers (such as Nautilus) and existing bash/zsh history paths; `proc-fd` records are filtered from both loaded and displayed history.

Displayed labels and read-only values are click-to-copy; editable single-line and multiline fields keep normal selection/Ctrl+C behavior. Multiline editors size themselves from three through ten visible rows and scroll beyond ten. Clipboard writes made by ImGui are routed through the X11 clipboard watcher, avoiding the event-loop stall that occurred when the app tried to synchronously read a GLFW-owned selection.

Save, download, copy, and move actions open a confirmation window with an editable destination and a generated `yyyyMMddHH-NN.ext` filename. The Linux Browse button uses Zenity or KDialog when available. Successful file operations stay visible and expose a copyable output path.

File confirmation, image preview, AI result, prompt-parameter, prompt-template detail/edit/delete, clipboard-history detail, recent-path detail, contact tables, network reports, Mermaid/QR previews, download progress, hash results, and annotation tools are independent native platform windows. Newly opened auxiliary windows request focus and are positioned above the root popup; they have no transient-parent relationship. The Settings page is a main tab, with General buttons to keep only the latest ten path/clipboard records or delete all history. The main popup remains in its own GLFW viewport and uses the native decorated title bar for dragging, with no redundant target/footer labels.

English and Simplified Chinese UI strings are supported. On Linux the app prefers Noto Sans CJK and falls back visibly when no CJK font is available. Platform-independent configuration, decision, AI, history, executor, renderer, download, hash, annotation, date/time, and view-model modules are isolated from the Linux/X11 backend. Future Windows and macOS ports can provide their own terminal, network command, process, renderer, timezone, and file-picker adapters without changing catalog or executor contracts.

For a launch-time visual smoke test:

```bash
PASTIT_SHOW_ON_START=1 ./build/pastit
```

XTest is loaded from the ordinary desktop runtime only when PasteIt needs to restore the captured target and inject `Ctrl+V`. If desktop headers are unavailable, CMake keeps the headless core and tests buildable and reports the missing components.
