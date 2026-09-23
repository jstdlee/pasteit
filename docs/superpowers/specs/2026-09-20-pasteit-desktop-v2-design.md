# PasteIt Desktop V2 Design

Date: 2026-09-20<br>
Status: Proposed for written review

## 1. Purpose

PasteIt V2 turns the existing Linux/X11 clipboard-action popup into a configurable desktop utility while preserving its core contract: Djev ranks request-local action IDs, and PasteIt executes only deterministic local actions. The revision fixes the observed Djev HTTP 422 failure, improves file-manager path awareness, adds editable OpenAI-compatible text transformations, and expands the Dear ImGui interface without introducing a daemon or new mandatory runtime framework.

The design keeps platform-independent application logic separate from Linux/X11 integration so later Windows and macOS backends can implement the same contracts without renaming or extracting the core again.

## 2. Confirmed requirements

1. Fix Djev failures that appear after several clipboard captures.
2. Keep Djev on its own `/v1/systemone` request and response schema.
3. Configure Djev endpoint, model ID, and API key in the application.
4. Use an OpenAI-compatible `/v1/chat/completions` API for general text transformations, with independent endpoint, API key, and model ID.
5. Provide CRUD operations for text prompt templates.
6. Add Translate, Rewrite, Summarize, and user-created prompt actions for text clipboard content.
7. Show generated text in an editable result window with copy and replace-clipboard operations.
8. Discover paths recently accessed by the Linux file manager, not only process working directories or copied paths.
9. Always generate path copy and move actions when valid source and destination paths exist.
10. Add a Recent Paths tab with a table of recent files and folders and per-row copy/open controls.
11. Clicking an image thumbnail opens an original-aspect image preview.
12. Add default image and text save directories.
13. Load a Chinese-capable font and provide Chinese and English UI text.
14. Make the popup movable and its opacity configurable.
15. Show file-operation success or failure inline and expose a copy-output-path control.
16. Prevent duplicate PasteIt processes from competing for the shortcut and issuing duplicate requests.

## 3. Root cause of HTTP 422

The deployed Djev server rejects a Choice with more than 26 alternatives. PasteIt currently builds actions for up to eight clipboard-history items and every recent destination. After several captures, `questions.best_action.criteria` exceeds 26 entries. The observed server response was:

```text
question 'best_action': at most 26 alternatives
```

The fix is not a retry. PasteIt will maintain a full local catalog for display and execution, then derive a bounded decision catalog of at most 26 enabled actions for Djev. The selection policy is deterministic:

1. actions for the current clipboard item before historical items;
2. one representative action per distinct action kind before additional destination variants;
3. configured default destinations before inferred recent destinations;
4. newer clipboard and path entries before older entries;
5. stable action ID as the final tie-breaker.

The UI shows only Djev-ranked actions from this bounded catalog. Full local actions remain accessible through content-specific controls and the Recent Paths tab. If Djev returns an HTTP or validation error, the response body’s safe error message is shown and the same bounded catalog is sorted locally using the deterministic priority order. The popup therefore remains usable during Djev failure.

## 4. Architecture and portability boundary

### 4.1 Platform-independent modules

Platform-independent modules are named after their responsibility rather than the current operating system:

```text
src/app/          application state, orchestration, async job state
src/config/       settings model, validation, JSON persistence
src/decision/     full catalog -> bounded Djev candidate selection, fallback ranking
src/ai/           OpenAI-compatible request/response protocol and prompt expansion
src/history/      persistent clipboard/path metadata and recency policy
src/ui/           ImGui view models and platform-neutral panel rendering
src/executor/     deterministic action execution and execution feedback
```

These modules cannot include X11, Win32, Cocoa, GLFW-native, or `/proc` headers.

### 4.2 Platform service contract

`src/platform/platform_services.hpp` defines the backend interface used by the app layer:

```cpp
struct PlatformFocusContext;
struct PlatformRecentPath;

class PlatformServices {
public:
    virtual ~PlatformServices() = default;
    virtual ClipboardCapture poll_clipboard() = 0;
    virtual PlatformFocusContext focused_context() = 0;
    virtual std::vector<PlatformRecentPath> recent_paths() = 0;
    virtual bool register_global_shortcut() = 0;
    virtual bool restore_focus_and_paste(const PlatformFocusContext&) = 0;
    virtual bool open_path(const std::filesystem::path&) = 0;
    virtual bool open_uri(std::string_view) = 0;
    virtual bool copy_text(std::string_view) = 0;
    virtual bool move_popup_by(int delta_x, int delta_y) = 0;
    virtual bool set_popup_opacity(float opacity) = 0;
    virtual std::vector<std::filesystem::path> preferred_ui_fonts() = 0;
};
```

The first implementation lives under `src/platform/linux/`:

```text
linux_x11_clipboard.*
linux_x11_focus.*
linux_recent_paths.*
linux_desktop_services.*
```

Future backends can live under `src/platform/windows/` and `src/platform/macos/`, implementing the same interface through Win32/WinRT and Cocoa/Pasteboard APIs. Platform-neutral code will not use names such as `X11Context` or `LinuxPath` in public application contracts.

### 4.3 Runtime composition

The executable creates the platform backend, settings store, histories, Djev client, OpenAI-compatible client, action catalog, and UI controller. The platform backend is injected into orchestration and executors. No global platform singleton is added.

## 5. Settings

Settings are stored in `${XDG_CONFIG_HOME:-~/.config}/pastit/settings.json` on Linux. Writes use a temporary sibling file followed by atomic rename. Missing or malformed settings fall back field-by-field to defaults and surface a non-blocking warning in the Settings window.

```cpp
enum class UiLanguage { System, English, SimplifiedChinese };

struct ProviderSettings {
    std::string endpoint;
    std::string model_id;
    std::string api_key;
};

struct PromptTemplate {
    std::string id;             // stable UUID
    std::string name;
    std::string system_prompt;
    double temperature;
    bool enabled;
    bool built_in;
};

struct AppSettings {
    int schema_version;
    UiLanguage language;
    float window_opacity;       // clamped to 0.55-1.0
    std::filesystem::path default_image_directory;
    std::filesystem::path default_text_directory;
    ProviderSettings djev;
    ProviderSettings general_llm;
    std::vector<PromptTemplate> prompt_templates;
};
```

Environment values remain supported for unattended launches. An explicitly saved non-empty setting overrides the environment; otherwise the existing `DJEV_URL`, `DJEV_MODEL`, and key variables provide defaults. Djev and general-LLM credentials are separate.

The settings UI has General, Paths, Djev, General LLM, and Prompt Templates sections. Both provider sections include a connection test whose result is shown inline. Provider tests do not overwrite the current configuration until Save is selected.

## 6. Prompt template CRUD and text transformation

Three templates ship by default:

- Translate: translate Chinese to natural English and other languages to Simplified Chinese.
- Rewrite: improve clarity and fluency while preserving meaning.
- Summarize: produce a compact factual summary.

Prompt templates support create, read/list, update, delete, duplicate, enable/disable, and restore-default operations. Deleting any template requires confirmation. Built-in templates are ordinary persisted records and may be edited or deleted; Restore Defaults recreates missing built-ins with new stable IDs without deleting custom templates.

Supported placeholders are `{text}`, `{source_language}`, and `{target_language}`. `{source_language}` and `{target_language}` default to `auto` and the template’s configured intent. Unknown placeholders remain literal. If `{text}` is absent, the expanded system prompt is sent as the system message and source text is sent as a separate user message. If `{text}` is present, it is expanded in the system prompt and a short user instruction is still included so OpenAI-compatible servers receive a conventional two-message conversation.

Each enabled template creates one request-local `TransformText` action. The action freezes the template ID, name, prompt, temperature, source clipboard ref, and provider snapshot at catalog-build time. Editing or deleting the template while a request runs cannot mutate that request.

The general LLM request uses:

```text
POST {base_url}/v1/chat/completions
Authorization: Bearer {api_key}
model: configured model ID
messages: system + user
temperature: template value
stream: false
```

Endpoint normalization accepts either a server base URL or a complete endpoint. A URL already ending in `/v1/chat/completions` is used unchanged; otherwise that path is appended after removing a trailing slash. Djev applies the same rule independently for `/v1/systemone`.

The client accepts the standard `choices[0].message.content` response. Network, HTTP, malformed JSON, and missing-content errors are displayed in the AI Result window. No partial result overwrites the clipboard.

## 7. Path discovery and history

`PathHistory` persists recent path metadata independently of settings. It records both files and directories and deduplicates normalized paths.

Linux sources, ordered by confidence, are:

1. successful PasteIt save, download, copy, and move results;
2. `text/uri-list`, `file://`, and absolute path clipboard captures;
3. the focused process working directory when meaningful;
4. open regular-file and directory targets found in `/proc/<focused-pid>/fd`;
5. for Nautilus, open directory descriptors found in the Nautilus service process, ordered by descriptor-link timestamp and current focus evidence.

Only existing local filesystem targets are added. Pseudo filesystems, sockets, pipes, deleted descriptors, and paths outside the user-visible filesystem are ignored. The collector returns observations; the platform-neutral history module owns normalization, deduplication, recency, limits, and persistence.

For a Path clipboard item, the catalog always contains Copy Path. For each valid destination directory, it contains Copy to Directory and Move to Directory. The configured text/image directory participates only for compatible save actions; it is also recorded in Recent Paths when it exists.

## 8. UI and interaction

### 8.1 Main popup

The top-level window becomes 720×560 by default. It remains borderless and floating, but the custom title area acts as a drag handle using platform-neutral drag intent translated by the desktop backend. Position is preserved for the session.

The popup has two tabs:

- Smart Actions / 智能动作
- Recent Paths / 最近路径

The window opacity setting is applied both to the native window and ImGui background where supported. Unsupported platforms retain an opaque window and show that limitation in Settings.

### 8.2 Smart Actions tab

The tab shows the abbreviated clipboard preview, target application, Djev/fallback status, and at most five action rows. Each row has pending, running, succeeded, or failed state. Non-paste operations keep the popup open after completion.

Successful save, download, copy, or move actions display the resulting path and controls to:

- copy the resulting path;
- open the result for a file;
- open the containing directory.

Direct paste actions close the popup after successful focus restoration and paste injection.

### 8.3 Recent Paths tab

An ImGui table displays Type, Name, Path, Source, Last Used, and Actions. Files and directories share one recency-sorted table and can be filtered by type. Each row provides Copy Path and Open. A selected source Path item additionally exposes Copy Here and Move Here for destination-directory rows.

### 8.4 Image preview

The thumbnail is a button. Clicking it opens an Image Preview window that preserves original aspect ratio, starts fit-to-window, supports 25%-400% wheel zoom and scrollable panning, and offers Save, Copy Image, and Open Temporary File. Texture creation remains on the UI thread; decoding occurs from the frozen clipboard blob.

### 8.5 AI Result window

Text transformations open an independent result window with request status and a multiline editable buffer. Controls are Copy Result, Replace Clipboard, Retry, and Close. Closing the main popup does not cancel or destroy a running result request. Only one result window is active per selected transformation in V2; selecting another transformation creates another queued result record but brings the newest to front.

### 8.6 Settings and localization

The Settings window edits a working copy and provides Save, Cancel, and Reset Section. Simplified Chinese and English labels are selected explicitly or from the process locale. The Linux backend searches common Noto Sans CJK and Droid fallback locations, then falls back to ImGui’s default font with a visible warning if no CJK font is available. UI strings are keyed in a small platform-neutral localization table rather than embedded throughout rendering code.

## 9. Single-instance behavior

Linux uses an advisory lock in `${XDG_RUNTIME_DIR:-/tmp}/pastit-<uid>.lock`. A second process exits with a clear message instead of registering another global shortcut or starting another clipboard history. The initial version does not add IPC to raise the existing popup; that can be added behind the same platform service interface later.

## 10. Async state and stale-result handling

Djev and general-LLM calls run off the UI thread. Each request owns immutable source refs and configuration snapshots. Djev results are rejected when the clipboard hash or focused target hash changed. General-LLM results remain viewable even if the clipboard changes, but Replace Clipboard always requires an explicit click.

Application shutdown waits only for bounded client timeouts. UI state uses explicit request IDs rather than shared booleans so late responses cannot update a newer action or result window.

## 11. Error handling

- Djev 4xx/5xx: retain HTTP status and parse `error.message`; show it in the action tab; use local fallback ranking.
- More than 26 local actions: never sent directly; candidate selection enforces the bound before serialization.
- General LLM failures: preserve source and any edited result; show retryable error.
- Invalid save directory: disable affected save action and show the path validation message.
- File collision: use the existing deterministic overwrite behavior for explicit copy/move actions; generated save filenames receive a numeric suffix instead of silently overwriting.
- Move fallback: attempt rename first, then copy-and-remove across filesystems; report partial failure without claiming success.
- Missing CJK font or native opacity: degrade visibly without blocking clipboard actions.
- File-manager discovery unavailable: continue with clipboard, cwd, configured, and operation-result paths.

## 12. Testing and verification

Headless tests cover:

1. 27+ full catalog actions produce at most 26 Djev candidates with deterministic ordering and current-item coverage.
2. Djev HTTP 422 bodies expose `error.message` and trigger fallback ranking.
3. settings defaults, round-trip persistence, malformed-file recovery, validation, and API-key separation;
4. prompt template create/read/update/delete/duplicate/restore-default behavior and stable IDs;
5. placeholder expansion and OpenAI-compatible request/response parsing;
6. stale Djev results and non-stale independent general-LLM result behavior;
7. path-history persistence and normalization for files, directories, URI lists, and moved/copied outputs;
8. Linux `/proc/<pid>/fd` discovery against a controlled child process holding file and directory descriptors;
9. guaranteed Copy/Move path action generation for valid destinations;
10. Recent Paths table view-model rows and per-row commands;
11. execution feedback and output-path clipboard operations;
12. localization fallback and Chinese glyph font selection logic;
13. single-instance lock behavior in a subprocess test.

Desktop integration checks cover movable-window state, native opacity when available, clickable thumbnail to full-image preview, Settings editing, and editable AI Result behavior. Live verification uses the deployed local Djev API and a configured OpenAI-compatible endpoint when credentials are present; tests never print API keys.

## 13. Acceptance criteria

1. Repeated clipboard captures cannot create a Djev request with more than 26 alternatives.
2. A Djev 422 shows the server’s reason and still presents locally ranked executable actions.
3. Nautilus-opened directories appear in Recent Paths and as relevant destinations without copying their paths first.
4. Path clipboard content exposes copy and move destinations.
5. Recent files and folders can be copied or opened from a table row.
6. Image thumbnail click opens a zoomable original-aspect preview.
7. Settings persist default directories, both provider configurations, language, opacity, and templates.
8. Prompt templates support CRUD, duplicate, enable/disable, and restore-default operations.
9. Translate/Rewrite/custom prompt actions call the configured OpenAI-compatible API and display editable, copyable results.
10. Successful file operations show status and allow copying/opening the resulting path.
11. Chinese text renders when a system CJK font is available.
12. The popup can be dragged and opacity changes take effect when the platform supports them.
13. A second PasteIt process cannot register a competing shortcut.
14. Platform-independent modules compile without platform headers, and Linux functionality is reached only through `PlatformServices`.

## 14. Non-goals

- Native Windows or macOS implementations in this revision.
- Streaming LLM tokens.
- Multiple simultaneous general-LLM provider profiles.
- Cloud synchronization of settings, templates, or clipboard history.
- A general shell-command or model-generated filesystem executor.
- OCR or image generation.
- Background daemon installation or desktop autostart packaging.
