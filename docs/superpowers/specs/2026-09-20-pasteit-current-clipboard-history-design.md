# PasteIt Current Clipboard and History Design

**Date:** 2026-09-20<br>
**Status:** Approved design, pending implementation plan<br>
**Product:** PasteIt Desktop V2

## Purpose

PasteIt must predict actions for the clipboard value the user has just copied, while still offering a durable clipboard manager for older values. Historical clipboard content must not leak into the current Smart Actions decision. File-producing actions must let the user confirm the destination and filename before execution, and the path, clipboard, and prompt-management tables must provide inspectable detail views.

This revision also reduces the Djev request enough to operate safely below the local context limit and raises the active local Djev deployment from 4096 to 8192 tokens.

## Confirmed Root Causes

1. `desktop_runtime.cpp` currently passes every in-memory clipboard record to `build_desktop_decision`.
2. `desktop_flow.cpp` retains up to eight records, and `action_catalog.cpp` creates actions for every retained record.
3. `candidate_selector.cpp` prioritizes the newest source but deliberately preserves action-kind diversity, so image and email actions from older records remain eligible.
4. `djev_client.cpp` serializes retained clipboard items twice (`clipboard.items` and `history`) and serializes action descriptions twice (`available_actions` and Choice `criteria`).
5. Runtime evidence showed the request growing to eight clipboard records and 53 local actions before the upstream rejected a 4087-input-token prompt plus ten output tokens.
6. The active local vLLM process was launched with `--max-model-len 4096`; the deployment environment also sets `MAX_MODEL_LEN=4096`. The model configuration itself advertises a substantially larger positional limit, so 4096 is a deployment limit rather than a PasteIt client limit or model architecture limit.

## Design Decisions

### 1. Separate current decision state from clipboard history

Smart Actions has exactly one subject: the newest current clipboard item.

- `DecisionSnapshot::clipboard_items` contains zero or one item for a Smart Actions request.
- Every action in `DecisionSnapshot::available_actions` must have `source_ref` equal to that current item's ref.
- Catalog construction, Djev candidate selection, fallback ranking, stale-response checks, and popup rows all enforce the same invariant.
- Old clipboard records are exposed only through the Clipboard History tab.
- Selecting **Use** or **Copy** on a history record publishes that record to the system clipboard. It then becomes the current clipboard through the normal capture path and may receive a new Smart Actions decision.
- Merely viewing a history record never changes the current Smart Actions decision.

This replaces the rejected alternative of continuing to send history to Djev and filtering only the rendered rows. Rendering-only filtering would preserve semantic interference, unnecessary tokens, and fallback ambiguity.

### 2. Compact Djev protocol

The Djev request keeps the established `/v1/systemone` protocol but removes duplicated context.

The `state` contains only:

- current clipboard ref, kind, bounded preview, byte size, MIME summary, and capture time;
- focused application, bounded window title, and focused directory;
- a bounded list of destination paths required by current actions.

The Choice question contains:

- one concise instruction;
- at most 26 action IDs in `criteria`;
- one bounded description per action.

The request does not include historical clipboard records and does not repeat the complete action list inside `state`. Full executable actions remain local and are matched by ID after the response.

Limits:

- at most 26 choices;
- current clipboard preview at most 256 UTF-8 code points;
- window title at most 160 code points;
- at most six path summaries, each bounded to 240 code points;
- each criterion description at most 120 code points;
- serialized request target at most 10 KiB.

If the compact request cannot satisfy the size target, candidate selection removes the lowest-priority destination variants before removing distinct action kinds. Djev failure continues to produce a deterministic current-item-only fallback Top 5.

### 3. Local Djev context configuration

The active local deployment will be changed to `MAX_MODEL_LEN=8192` and restarted through its existing Compose workflow. Verification must confirm the vLLM process command contains `--max-model-len 8192` and that the structured endpoint on port 8011 returns a valid response.

PasteIt must still keep requests compact enough for the previous 4096-token deployment. Raising the server limit is headroom, not a substitute for fixing request construction.

The API key must never appear in logs, tests, process arguments, or review artifacts.

### 4. Persistent clipboard history

Add a platform-neutral `ClipboardHistoryStore` backed by:

- `${XDG_DATA_HOME:-~/.local/share}/pastit/clipboard-history.json` for metadata;
- the existing `blobs/<content-hash>.bin` directory for bytes.

Each record stores:

- stable clipboard ref;
- content hash;
- MIME types and detected content kind;
- blob filename;
- bounded preview and size;
- source application and capture time;
- semantic tags.

Retention rules:

- keep the newest 50 records across restarts, including images;
- sort by capture time descending;
- adjacent captures with identical content are coalesced by updating recency and source metadata;
- non-adjacent duplicate records may coexist but share one content-addressed blob;
- after a successful atomic manifest save, delete only hash-named blobs that no retained record references;
- never clean blobs after a malformed manifest load;
- ignore missing-blob records with a visible warning rather than failing startup;
- generate refs that cannot collide after restart.

Clipboard history persistence is independent of the current-only Djev snapshot.

### 5. Clipboard History tab

Add a third main tab, **Clipboard History**, with a resizable table:

| Column | Behavior |
|---|---|
| Type | Text, URL, Email, Image, Path, or JSON |
| Preview | Bounded text or image thumbnail |
| Source | Capturing application |
| Size | Human-readable byte size |
| Captured | Local date and time |
| Actions | View, Copy/Use, Save |

The table uses resizable, reorderable, hideable columns and vertical scrolling. Double-clicking a row or pressing View opens a detail modal.

The detail modal shows full metadata and:

- scrollable full text for textual records;
- a zoomable original image for image records;
- Copy/Use, Save, and Close commands;
- the existing direct-copy behavior for structured resume fields where applicable.

The runtime owns blob reads, clipboard publication, textures, and action execution. The panel emits typed commands by clipboard ref and has no direct dependency on platform services or storage.

### 6. Destination resolution

File-producing actions receive destination candidates from a platform-neutral resolver. Ordered destination roles are:

1. manually selected destination for the active operation;
2. source parent when the current clipboard contains a file or directory path;
3. focused file-manager/current directory;
4. configured default image or text directory, according to action kind;
5. recent existing directories;
6. temporary directory fallback.

Directories are canonicalized when possible and de-duplicated without losing the highest-priority role. A source file's parent remains available even if newer path-history entries would otherwise exceed the normal recent-path limit.

Extend `PlatformServices` with a platform-neutral directory chooser:

```cpp
virtual std::optional<std::filesystem::path>
choose_directory(const std::filesystem::path& initial_directory) = 0;
```

The Linux backend invokes the first available chooser in this order: Zenity, then KDialog. If neither executable exists, `choose_directory` returns no value. The operation modal always retains an editable directory field, so manual entry remains available without a chooser. Future Windows and macOS backends implement the same contract with native pickers.

### 7. File-operation confirmation

The following actions do not execute immediately:

- save text, image, URL, email, JSON, pretty JSON, or resume data;
- download URL;
- copy path to directory;
- move path.

Selecting one opens a modal with:

- frozen source preview and action label;
- editable destination directory;
- Browse button;
- recent/source-parent destination shortcuts;
- editable filename;
- resolved output-path preview;
- validation or collision warning;
- Confirm and Cancel.

The default generated name for save/download actions is:

```text
yyyyMMddHH-01.ext
yyyyMMddHH-02.ext
...
```

The serial is the first available value in the selected directory for the current local hour. Extension derives from content and representation: image MIME, `.txt`, `.json`, `.eml`, `.url`, source URL suffix when reliable, or `.bin` fallback.

Copy and move actions default to the source basename, preserving its extension, but the filename remains editable so the operation can rename. Confirmation freezes the source record and action while revalidating the destination and output collision immediately before execution. Cancel executes nothing.

Executors receive only a confirmed action plus validated destination/filename override. No model output becomes a filesystem command.

### 8. Prompt actions and template management

When the current clipboard kind is plain text, Smart Actions shows a dedicated **Prompt Actions** section below the predicted Top 5. It contains every enabled `TransformText` action for the current clipboard, regardless of Djev rank.

- Historical text records do not contribute prompt actions.
- Disabled templates are omitted.
- If the General LLM endpoint or model is incomplete, prompt actions remain visible but disabled with a configure-provider explanation.

Prompt Templates in Settings becomes a resizable table with columns:

- Enabled;
- Name;
- Temperature;
- Built-in/Custom;
- Actions.

Each row exposes compact View, Edit, Duplicate, and Delete controls with tooltips. View/Edit opens a detail modal backed by a draft copy. Save applies through `PromptTemplateService`; Cancel discards the draft. Duplicate creates a stable new ID and opens the copy for editing. Delete uses a confirmation modal that freezes the selected template ID.

### 9. Recent Paths table

The Recent Paths table uses resizable, reorderable, hideable columns. The Path column uses stretch sizing and receives the largest initial width; Type, Source, and Actions use compact widths.

Double-clicking a row or pressing View opens a detail modal with:

- full path;
- kind, source, existence, and last-seen time;
- Copy Path and Open;
- Open Parent for file rows;
- Copy Here and Move Here when valid for the current source;
- Use as Destination for the pending file-operation modal.

The view model resolves exact action IDs before rendering. UI code does not scan the catalog inline.

### 10. UI module boundaries

`desktop_runtime.cpp` remains responsible for frame lifecycle, platform calls, asynchronous provider work, texture ownership, persistence orchestration, and command dispatch. Rendering and headless state move into focused modules:

- `history/clipboard_history_store.*`
- `history/path_target_resolver.*`
- `app/generated_filename.*`
- `app/file_operation_confirmation.*`
- `ui/clipboard_history_model.*`
- `ui/clipboard_history_panel.*`
- `ui/file_operation_confirmation_panel.*`
- expanded `ui/recent_paths_model.*` and `ui/recent_paths_panel.*`
- expanded `ui/prompt_templates_panel.*`
- expanded `ui/main_popup_panel.*` and `ui/image_preview_panel.*`
- shared `ui/imgui_widgets.*`

Headless models do not include ImGui, X11, GLFW-native, Win32, or Cocoa headers. Panel code may depend on ImGui but returns typed commands and does not call storage, executors, settings persistence, or `PlatformServices` directly.

Dear ImGui uses the pinned docking branch with multi-viewport enabled. The main Smart Actions popup stays on the main GLFW viewport. Settings, file confirmation, image preview, AI results, prompt-template view/edit/delete, clipboard-history detail, and recent-path detail use no-auto-merge window classes so each becomes an independent native platform window that can move beyond the popup bounds. The platform-neutral view models remain unaware of this desktop presentation choice, preserving the Windows and macOS port boundary.

All new visible text receives English and Simplified Chinese entries.

## Error Handling

- A Djev context/HTTP error displays the server message and current-only local fallback actions.
- A clipboard manifest warning does not prevent capture or Smart Actions.
- A missing blob disables actions for that history row and identifies the missing data.
- Invalid or nonexistent destination paths keep Confirm disabled.
- Invalid filename characters and path separators are rejected or sanitized before execution; the modal shows the resolved name.
- Copy/move to the identical source path is rejected before execution.
- Cross-device move retains the existing copy-then-remove fallback and reports partial failures.
- A stale confirmation draft is rejected if its source record no longer exists.
- Directory chooser cancellation leaves the modal and current directory unchanged.
- If 8192-token Djev restart fails, restore the running 4096 configuration and keep the compact client request; do not leave the local service unavailable.

## Verification Strategy

Implementation follows test-first RED/GREEN cycles.

Required automated coverage:

1. Current text plus historical image/email produces only current-text Djev actions and fallback rows.
2. Djev payload excludes clipboard history and duplicate action metadata, contains at most 26 choices, and stays within the 10 KiB target for a worst-case catalog.
3. Clipboard history persists and reloads exactly the newest 50 records, including image bytes.
4. Adjacent duplicate capture coalescing, shared-blob retention, orphan cleanup, malformed manifest, and missing blob behavior.
5. Path target resolution always includes a path source's parent and a manually selected destination.
6. Generated names produce deterministic `yyyyMMddHH-NN.ext` values and advance around collisions.
7. Confirmation Cancel executes nothing; Confirm applies edited directory and filename for save/download/copy/move.
8. Text current clipboard exposes all enabled prompt actions outside Top 5; non-text current clipboard does not.
9. Prompt edit/duplicate/delete modal state uses frozen IDs and draft copies.
10. Clipboard and recent-path detail view models expose the complete row state and valid commands.
11. Platform contract includes `choose_directory` without native-header leakage.
12. English and Simplified Chinese translations cover all new labels, statuses, tooltips, and errors.

Final integration verification:

- full CMake build and CTest suite;
- real local Djev calls for text, image, path, JSON, 26-choice, and worst-case compact payload scenarios;
- process inspection confirming `--max-model-len 8192`;
- restart persistence test with 50 mixed clipboard records;
- desktop smoke test for all three tabs, image detail, recent-path detail, prompt CRUD, and file confirmation;
- independent whole-change code review with all Critical and Important findings resolved;
- stop the old running PasteIt instance and launch the newly built binary only after verification succeeds.

## Acceptance Criteria

- Copying text never shows actions sourced from older image, email, URL, path, or JSON records.
- Clipboard History retains the latest 50 records, including usable image data, across application restarts.
- Every file-producing action offers a confirmable destination and editable filename before modifying the filesystem.
- Source parent and manually chosen directories are always available where applicable.
- Prompt actions for current plain text are directly visible and not dependent on Top-5 Djev ranking.
- Prompt Templates and Recent Paths use resizable tables with detail modals and per-row actions.
- The local Djev service runs with an 8192-token maximum context and PasteIt's compact request remains comfortably below the previous 4096-token failure boundary.
- Full automated, live-provider, persistence, and desktop smoke verification passes.
