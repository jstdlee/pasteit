# PasteIt

PasteIt is a local Linux/X11 and Windows clipboard action popup. It observes clipboard content, builds deterministic local action instances, asks a local Djev endpoint to rank a bounded request-local catalog, and executes only fixed native actions. V2 also supports editable text transformations through a separately configured OpenAI-compatible provider.

## Build Prerequisites

- CMake 3.20+
- A C++20 compiler
- X11/OpenGL development headers on Linux, or a Windows SDK and OpenGL toolchain on Windows, for the desktop popup
- HTTP(S) downloads use libcurl when its development headers are installed, the `curl` executable on Linux when they are not, and WinHTTP on Windows.
- `mmdc` is optional for an in-window Mermaid image. The offline Mermaid browser bundle and native C++ QR encoder are included in the app build.

GLFW 3.4, Dear ImGui 1.92.9b-docking, and `stb_image.h` are fetched automatically by CMake at pinned revisions. The docking tag supplies Dear ImGui's multi-viewport platform-window support; they do not need to be installed system-wide. On Debian/Ubuntu systems without development headers, prepare a project-private sysroot without `sudo`:

```bash
./scripts/bootstrap-local-deps.sh
```

This downloads and extracts headers only under `.deps/`. The resulting `pastit` binary uses the desktop's existing versioned X11/OpenGL runtime libraries and has no `.deps` RPATH.

## Djev Configuration

- `DJEV_URL` defaults to `http://127.0.0.1:8011`
- `DJEV_MODEL` defaults to `typed-decisions`, a model supported by the local Laya API on port 8011
- `DJEV_API_KEY` is optional for local deployments that require bearer authentication; `API_KEY` is accepted as the local demo fallback
- The desktop app reads these values from `.env` (or `PASTIT_ENV_FILE`) without printing the token

The app posts to `{DJEV_URL}/v1/systemone`. AutoJev settings also accept `http://127.0.0.1:8000/v1/autojev` as a shortcut; PasteIt maps it to AutoJev's `/v1/systemone` route and sends the TypeSafe `model/state/questions` request shape. Set its model field to `autojev`. Other Djev endpoints keep the request ID and protocol metadata. Djev receives only the current clipboard item, the relevant target context, represented destination paths, and at most 26 compact action alternatives. Clipboard history and the duplicate full action catalog are never sent. IP and date/time alternatives are offered only for short text with a locally validated match, reducing irrelevant choices. The serialized request is capped at 10 KiB; a deterministic local fallback remains available if Djev fails. Djev returns action IDs and probabilities only, and PasteIt executes the matching fixed native action rather than model-generated shell commands.

The local decision API on port 8011 is the Laya API, which accepts `typed-decisions`, `english`, and `multilingual` rather than the Jev alias `jev-latest`. PasteIt allows 30 seconds for a local decision because inference can take several seconds even after startup.

Wait for port 8011 to report healthy, then run `./build/djev_live_integration` to verify the local API. Set `DJEV_MODEL` explicitly if you want to test another model supported by that API.

## Settings and General LLM

Settings are loaded from `settings.json` beside the executable on startup and saved atomically on normal exit, including valid edits still open in Settings; data/history lives under its `data/` subdirectory. The Settings tab groups language, opacity, default image/text directories, and history controls under General. It also configures the Jev-compatible endpoint/model/key and a separate OpenAI-compatible endpoint/model/key. The editable General LLM model field is paired with a dropdown loaded from the provider's `/v1/models` endpoint; a failed refresh leaves the manually entered model unchanged. A base general-LLM URL is normalized to `/v1/chat/completions`; a complete endpoint is used unchanged. OpenCode Go endpoints automatically receive a per-request `x-opencode-session` header.

The Fast actions settings section configures the optional `mmdc` path and arguments, QR error correction/margin/scale, download resume directory, `.part` retention on cancel, terminal command argv, hash actions shown by default, source/target time zones, and annotation save directory. When `mmdc` is available, Mermaid renders an in-window PNG; otherwise it exports self-contained offline HTML and keeps the source available. QR renders an in-window native PNG. Annotation export is currently SVG-only; PNG/JPG preferences are normalized to SVG rather than writing SVG bytes with a raster suffix. Terminal profile and 24-hour date display are shown as stored preferences until a platform adapter consumes them.

Prompt Templates supports create, edit, delete, duplicate, enable/disable, and restore defaults. Translate, Rewrite, and Summarize are included. `{text}` is filled from the current clipboard; every other named `{variable}` placeholder opens a parameter dialog before the request is sent. Parameter values are prefilled from the current session's per-template memory and are frozen into the generated request after confirmation. Results open in an independent editable window with Copy Result, Replace Clipboard, and Retry.

Custom prompt opens a small dialog for one-off instructions and sends the copied text to the configured general LLM. Numeric sequences separated by commas or spaces, plus two-column data, offer Graph data. Its preview switches between line, bar, and pie charts, can treat the first row as a header, and can space ISO dates along the x axis. Copy image and Save image export the preview as PNG.

Selecting an action records it in `data/usage.json`, a decayed frequency model (half-life about 14 days). Choices are counted per context: content kind, detected signals (code, IP, date/time, contact, numbers, …), focused app, and time of day. Action keys are semantic (action kind, destination path, prompt template), never clipboard text. The model does two things. It adds a bounded ranking bonus of at most 0.20, smoothed so a single click cannot dominate, both to the Djev probabilities and to the local fallback. It also sends up to five `state.habits` entries (`action_id`, `share`, `count`) with each Djev request, so the model can weigh your frequent operations against what the content suggests. Settings → Usage insights shows the most frequent choices per content kind and can reset the history.

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

- Text: paste, save-to-file, contact/business-card extraction with vCard copy/save, Mermaid generation for diagram-like text, QR for short payloads (up to 512 bytes and 6 lines), Explain text/code, Translate, Rewrite, Summarize, and custom-template actions, all ranked together in the main action list. Local utilities: UPPER/lower/Title case, tidy whitespace, sort lines, remove duplicate lines, word count, Base64 encode/decode, URL encode/decode, and Markdown table from comma/tab/semicolon rows. Numbers get Graph data plus sum/mean/median/min/max. Code gets "Save as .py/.cpp/…" with a detected extension.
- Colors (`#hex`, `rgb()`, `hsl()`): copy as HEX, RGB, or HSL
- JWT: decode header and claims with a readable expiry (the signature is not verified); UUID: generate a fresh v4
- URL: paste, open, copy clean URL without `utm_*`/`fbclid`/… tracking parameters, copy as Markdown link, URL-decode, generate QR, copy GitHub HTTPS/SSH remotes, clone GitHub repositories, download, and save the URL response bytes to a target file; Save URL File does not save the URL string as a `.url` text file
- Email: paste, save `.eml`, compose actions, and QR payload generation
- Image: paste and save original bytes; clicking the thumbnail opens an aspect-preserving, zoomable preview, with actions to copy a temporary preview-file path and open SVG annotation tools
- Local path and `file://`: copy into a recent directory, move into a recent directory, open Terminal at the containing directory, open with the default application, show in file manager, copy path as text, copy name, copy parent folder, and hash regular files with the enabled SHA-256/SHA-512 defaults
- JSON: raw save, pretty copy, pretty save, minified copy, YAML, CSV (arrays of objects), jq-style leaf paths, and a custom LLM prompt. YAML, CSV and jq paths keep the original key order and number formatting.
- Resume-like text (an email plus section headings such as Experience/Education, or a phone plus a Skills block): copy extracted fields, copy extracted JSON, and save extracted JSON
- IP addresses: ping, traceroute, reverse DNS, dig, and a combined report. Linux maps these to argv calls for `ping -c 4`, `traceroute`, `getent hosts`, and `dig`; Windows uses `ping`, `tracert`, and `nslookup`. Clipboard text is never shell-concatenated.
- Date/time values: timezone conversion, Unix timestamp conversion, and normalized copy. The Settings tab supplies the default source and target zones used by the desktop catalog.

URL downloads run through a `DownloadManager` and write `<target>.part` until completion. Resume reuses an existing `.part` file, sends a Range request from its current size, appends only after a valid `206 Partial Content`, and safely restarts when the server answers `200 OK` without range support. Pause preserves `.part`; Cancel preserves or discards it according to the Settings toggle. Linux uses libcurl when development headers are present and a streamed, argv-only `curl` process when they are absent. Windows streams HTTP(S) downloads through WinHTTP.

Mermaid PNGs from optional `mmdc` appear in the independent result window. The fallback HTML includes the pinned JavaScript bundle and opens in a normal browser without a network connection or Chromium download. QR PNGs are generated in-process using Project Nayuki's C++ library and appear in the independent QR window. If a renderer fails, its result window still exposes the copyable source or payload.

Annotation export keeps the overlay non-destructive while editing. The current exporter writes a self-contained SVG that embeds the original image and overlays; raster PNG/JPG export is intentionally not advertised until a real encoder path exists.

Compose opens the local `mailto:` handler. Direct email sending is enabled only when `PASTIT_SENDMAIL` names a local executable; PasteIt invokes it with the destination address as its first argument.

## Data Views

Before ranking, PasteIt profiles the clipboard from a bounded sample (the first rows, a middle probe and the tail, plus the total length) and judges its general shape with a confidence: JSON, NDJSON, CSV/TSV, Markdown, code, logs, key-value, YAML, HTML/XML, SQL, URL or path lists, number series, prose or a single token. Small items are confirmed by a full parse; large ones are marked as sampled. The profile decides which specialised actions are offered, feeds usage learning, and is sent to Jev; when the local guess is uncertain, Jev also answers which content type it is.

- **Table view** (CSV/TSV/pipe tables, JSON arrays of objects, NDJSON): sortable and filterable columns, a statistics row (type, filled, distinct, sum/mean/min/max), column visibility, and copy as Markdown, CSV, JSON or SQL `INSERT` statements.
- **Charts**: line, bar, pie, scatter and histogram, drawn live with hover values. From a table, pick the X column and one or more numeric series; number lists open directly. Charts copy or save as PNG.
- **Markdown preview**: headings, emphasis, links, code, quotes, nested and task lists, tables and fenced code, with copy as HTML or plain text.

## Pipelines

Run pipeline opens a builder with the clipboard on the left and live output on the right. Commands chain with `|`, e.g. `sort | uniq -c | sort -nr | head -n 10`. Built-in stages run inside PasteIt on every platform: `sort` (`-n -r -u -f -k N -t SEP`), `uniq` (`-c -d -u -i`), `wc` (`-l -w -c -m`), `head`/`tail` (`-n N`), `grep` (`-i -v -o -c -n -F`, ECMAScript regex), `cut` (`-d -f`, `-c`), `tr` (sets, ranges, `-d`, `-s`), `sed 's/re/rep/g'`, `nl`, `rev`, `tac`, `column`, `trim`, `upper`, `lower`, `join [SEP]` and `anonymize`. Custom commands (Settings → Pipelines) name a pipeline that can be used as a stage, e.g. `errors = grep -i 'error|fail'`; extra arguments go to its last stage. A literal `|` goes in quotes or as `\\|`. Other programs run only if listed under Allowed external tools (default `gawk`, `awk`, `jq`) or when Allow any program on PATH is on; they are started by argv with the text on stdin, never through a shell. `;`, `&`, redirects and `$(...)` are rejected, and each run is limited to 2 seconds and 1 MiB of output.

Saved recipes (name, command, and the content they apply to: `lines`, `any`, or shapes such as `csv`, `json`, `log`) appear as ranked actions and take part in usage learning. Defaults include Count occurrences, Unique lines, Line count, First column, Sum column 2, Errors only and JSON keys.

## Privacy

Anonymize appears when the clipboard contains personal data. It detects names (labels such as `Name:`/`Dear`/`联系人：`, titles such as `Mr`/`Dr`/`先生`/`女士`, and bundled lists of common given names, surnames and Chinese family names), emails, phone numbers (international, Singapore, Chinese mobile), IPv4/IPv6/MAC addresses, card numbers (Luhn), IBANs (mod 97), Singapore NRIC/FIN and Chinese resident IDs (check digits), street addresses, birth dates, user folders in paths, and secrets (private keys, AWS/GitHub/Slack/OpenAI-style keys, bearer tokens, `password=`/`token=` values, passwords in URLs). Dates, times, versions and plain order numbers are left alone.

The Anonymize window lists every finding; untick one or a whole category to keep it. Replacement styles are numbered placeholders (`[EMAIL_1]`, the same value always gets the same placeholder), partial masking, realistic fake values (`example.com`, `192.0.2.x`, `555` numbers) or blackout. Settings → Privacy sets the default style, categories, words to always or never hide, and **Hide personal data before sending to the LLM**: prompt templates and custom prompts then receive placeholders and the answer is restored to the real values. The placeholder mapping lives in memory for the session only; Restore anonymized values puts real values back into any pasted text that still has placeholders. `anonymize` is also a pipeline stage. The Anonymize window can also **Ask LLM** directly: pick a prompt template or write a custom prompt; only the placeholder version is sent and the answer opens with the real values restored.

**Summarize page** (URLs, when a general LLM is configured) downloads the page (redirects followed, no cookies, 5 s and 2 MB limits), extracts the readable text (the `<article>`/`<main>` element without scripts, navigation or footers), hides personal data when that option is on, and asks the LLM for a gist plus up to seven bullet points. The first use asks for permission (Allow once / Always allow); Settings → Privacy can turn it off again.

## Desktop Popup

`pastit` is the desktop entry point. A per-user lock prevents duplicate V2 processes. Start it once, then press `Ctrl+Alt+F` to capture the focused target and open the popup. The movable, opacity-configurable popup shows the top eight Djev/fallback actions as cards: a category icon, the label, the shortened destination (`~/…`) or a one-line description, and a confidence bar when Jev ranked the list (the local fallback is an ordering, so it shows no bar). Click a card, press its number key 1–8, or use Up/Down and Enter. The header shows the content kind, detected signals (color, code, JSON, …), the size, and whether Jev or the local fallback ranked the list; colors show a swatch. The window height fits its content after each ranking. The root window has no title bar or window buttons: drag the tab row to move it, and press Escape (or the global shortcut) to hide it without executing.

The Smart Actions tab abbreviates large text and uses image thumbnails. Enabled text prompt templates are ranked directly in the same action list as paste/save actions. The persistent Clipboard History tab retains the newest 50 text or image records; rows use content-type icons and open a detail view with the complete selectable/copyable text, while image details use the original stored bytes. Text metadata is sanitized and truncated only at UTF-8 character boundaries, and legacy malformed previews are normalized when history loads. The Recent Paths tab uses a resizable wide path column; long row paths preserve their prefix and filename suffix while abbreviating the middle with `...`. Row details put path operations in a top toolbar and use a scrollable body so long paths and actions remain reachable. Runtime recent-path sources are named file managers (such as Nautilus) and existing bash/zsh history paths; `proc-fd` records are filtered from both loaded and displayed history.

Displayed labels and read-only values are click-to-copy; editable single-line and multiline fields keep normal selection/Ctrl+C behavior. Multiline editors size themselves from three through ten visible rows and scroll beyond ten. On Linux, clipboard writes made by ImGui are routed through the X11 clipboard watcher, avoiding the event-loop stall that occurred when the app tried to synchronously read a GLFW-owned selection. Windows uses the Win32 clipboard adapter.

Save, download, copy, and move actions open a confirmation window with an editable destination and a generated `yyyyMMddHH-NN.ext` filename. The Linux Browse button uses Zenity or KDialog when available; Windows uses its native folder picker. Successful file operations stay visible and expose a copyable output path.

File confirmation, image preview, AI result, prompt-parameter, prompt-template detail/edit/delete, clipboard-history detail, recent-path detail, contact tables, network reports, Mermaid/QR previews, download progress, hash results, and annotation tools are independent native platform windows. Newly opened auxiliary windows request focus, are centered over the root popup, and are owned by it (a transient window on X11, an owned window on Windows), so the window manager always keeps them in front of the popup. While a confirmation or consent dialog is open, the popup is dimmed and locked. The Settings page is a main tab, with General buttons to keep only the latest ten path/clipboard records or delete all history. The main popup remains in its own GLFW viewport and is moved by dragging its tab row, with no redundant target/footer labels.

The UI uses a custom dark or light theme (Settings → General → Theme) with regular and bold weights of the system CJK font, scaled to the monitor's content scale. English and Simplified Chinese UI strings are supported. On Linux the app prefers Noto Sans CJK (Regular plus Bold) and falls back visibly when no CJK font is available; Windows looks for installed CJK fonts. Platform-independent configuration, decision, AI, history, executor, renderer, download, hash, annotation, date/time, and view-model modules share the same implementation. Linux and Windows provide native clipboard, focus, terminal, network command, process, timezone, and file-picker adapters.

For a launch-time visual smoke test:

```bash
PASTIT_SHOW_ON_START=1 ./build/pastit
```

XTest is loaded from the ordinary desktop runtime only when PasteIt needs to restore the captured target and inject `Ctrl+V`. If desktop headers are unavailable, CMake keeps the headless core and tests buildable and reports the missing components.
