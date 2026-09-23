# PasteIt Desktop V2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver PasteIt Desktop V2 with bounded/fallback Djev decisions, persistent settings and prompt templates, OpenAI-compatible text transformations, accurate Linux recent paths, expanded ImGui views, and a platform contract suitable for future Windows/macOS backends.

**Architecture:** Preserve the existing domain/action/executor core, add focused config, network, AI, decision, history, and app-state modules, and move Linux/X11 details behind `PlatformServices`. Dear ImGui panels consume platform-neutral view models; `desktop_runtime.cpp` becomes composition and event-loop code rather than the owner of every feature.

**Tech Stack:** C++20, CMake/CTest, Dear ImGui 1.91.9b, GLFW 3.4, OpenGL3, Linux X11/XTest, JSON utilities, optional libcurl with fixed `curl`-CLI fallback, local Djev `/v1/systemone`, and OpenAI-compatible `/v1/chat/completions`.

**Spec:** `docs/superpowers/specs/2026-09-20-pasteit-desktop-v2-design.md`

## Global Constraints

- Djev uses its existing SystemOne schema; the general LLM uses an independent OpenAI-compatible Chat Completions schema.
- No Djev Choice request may contain more than 26 alternatives.
- Model output never becomes a shell command or filesystem command.
- API keys must not be printed in diagnostics or test output.
- Platform-independent modules cannot include X11, Win32, Cocoa, or GLFW-native headers.
- Linux implementation files live under `src/platform/linux/`; future Windows/macOS backends implement the same `PlatformServices` contract.
- Existing text, URL, email, image, path, JSON, resume, clipboard, and deterministic executor behavior must remain supported.
- The workspace is not a Git repository; do not invent commit hashes. Preserve a green build after every task instead of adding impossible commit steps.

## Review Focus

- A catalog containing resume fields, prompt templates, multiple clipboard items, and multiple destinations must still send no more than 26 unique enabled actions to Djev; Task 3 adds this test.
- A malformed or non-JSON HTTP 422 response must not suppress all actions or expose an API key; Task 3 adds both cases.
- A prompt template deleted while its request is running must not mutate that request’s frozen prompt/provider snapshot; Task 5 adds this test.
- `/proc/<pid>/fd` may contain sockets, deleted paths, reused descriptor numbers, and inaccessible links; Task 6 adds controlled-process tests.
- An image too large for the popup and a result string larger than the initial edit buffer must remain viewable/editable without truncation; Task 9 adds desktop-model tests and a visual smoke check.

---

### Task 1: Re-establish baseline and introduce platform-neutral contracts

**Files:**
- Create: `src/platform/platform_services.hpp`
- Create: `src/platform/linux/linux_desktop_services.hpp`
- Create: `src/platform/linux/linux_desktop_services.cpp`
- Move/modify: `src/platform/x11_clipboard.*` to `src/platform/linux/linux_x11_clipboard.*`
- Move/modify: `src/platform/x11_context.*` to `src/platform/linux/linux_x11_focus.*`
- Modify: `src/ui/desktop_runtime.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/platform_contracts_test.cpp`

**Interfaces:**
- Produces `PlatformFocusContext`, `PlatformRecentPath`, `ClipboardCapture`, and abstract `PlatformServices`.
- Produces `LinuxDesktopServices final : public PlatformServices` by composing the existing X11 clipboard/focus implementations.
- Keeps compatibility aliases only inside Linux implementation files; platform-neutral consumers use the new names immediately.

- [ ] **Step 1: Write the failing compile-time contract test**

Create `tests/platform_contracts_test.cpp` with a `FakePlatformServices` implementing every method and assertions that focus and recent-path values cross the interface without X11 types:

```cpp
static_assert(std::is_abstract_v<pastit::PlatformServices>);
FakePlatformServices fake;
fake.paths.push_back({.path = "/tmp/example", .kind = PathKind::Directory});
assert(fake.recent_paths().front().path == "/tmp/example");
assert(fake.set_popup_opacity(0.72F));
```

The production change caught by this test is accidental platform leakage or an incomplete backend contract.

- [ ] **Step 2: Run the contract test and verify RED**

Run:

```bash
cmake -S . -B build-v2 -DPASTIT_BUILD_TESTS=ON -DPASTIT_BUILD_DESKTOP=ON
cmake --build build-v2 -j2 --target platform_contracts_test
```

Expected: compilation fails because `platform/platform_services.hpp` and `LinuxDesktopServices` do not exist.

- [ ] **Step 3: Add the contract and move Linux implementations**

Define the interface exactly as the approved spec, including clipboard poll/publish, focus capture/restore, recent paths, shortcut, open path/URI, popup movement/opacity, and preferred UI fonts. Move source files with `apply_patch`, update includes, and implement `LinuxDesktopServices` as a narrow adapter. Do not change existing X11 behavior in this step.

- [ ] **Step 4: Compose the backend in the runtime**

Replace direct `X11ClipboardWatcher` and `X11ContextService` construction in `desktop_runtime.cpp` with one `std::unique_ptr<PlatformServices>`. Keep OpenGL/GLFW rendering local to the desktop runtime.

- [ ] **Step 5: Verify GREEN and baseline regression**

Run:

```bash
cmake --build build-v2 -j2
ctest --test-dir build-v2 --output-on-failure
```

Expected: the new contract test and all existing tests pass; display-dependent tests may return their existing skip code only when the display is unavailable or grabbed.

### Task 2: Persistent settings and prompt-template CRUD

**Files:**
- Create: `src/config/app_settings.hpp`
- Create: `src/config/app_settings.cpp`
- Create: `src/config/settings_store.hpp`
- Create: `src/config/settings_store.cpp`
- Create: `src/config/prompt_template_service.hpp`
- Create: `src/config/prompt_template_service.cpp`
- Modify: `src/util/json.hpp`
- Modify: `src/util/json.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/settings_test.cpp`
- Create: `tests/prompt_template_test.cpp`

**Interfaces:**
- Produces `AppSettings default_settings()`, `SettingsLoadResult SettingsStore::load()`, and `bool SettingsStore::save(const AppSettings&, std::string&)`.
- Produces `PromptTemplateService::{create, update, erase, duplicate, restore_defaults}` operating on `AppSettings::prompt_templates`.
- Stable template IDs are generated by an injected `IdGenerator` in tests and a UUID-like random generator in production.

- [ ] **Step 1: Write failing settings behavior tests**

Use a temporary directory and literal expected values. Cover missing-file defaults, full round trip, malformed JSON recovery, opacity clamp, independent Djev/general-LLM keys, and atomic-save replacement:

```cpp
auto settings = default_settings();
settings.window_opacity = 0.72F;
settings.djev.api_key = "djev-key";
settings.general_llm.api_key = "llm-key";
assert(store.save(settings, error));
const auto loaded = store.load();
assert(loaded.settings.window_opacity == 0.72F);
assert(loaded.settings.djev.api_key == "djev-key");
assert(loaded.settings.general_llm.api_key == "llm-key");
```

The mutation caught is conflating providers or silently dropping persisted fields.

- [ ] **Step 2: Write failing CRUD tests**

Assert create/read/update/delete/duplicate/enable and restore-default behavior with injected IDs. Delete a built-in, restore defaults, and assert custom templates remain untouched.

- [ ] **Step 3: Run both tests and verify RED**

Run:

```bash
cmake --build build-v2 -j2 --target settings_test prompt_template_test
```

Expected: missing headers/types/functions.

- [ ] **Step 4: Implement settings parsing and atomic persistence**

Extend the JSON utility with object/array/string/number/bool accessors needed by both provider and template arrays. Save to `settings.json.tmp`, flush/close, then rename to `settings.json`. Return warnings rather than throwing for malformed user files. Never log settings values.

- [ ] **Step 5: Implement template CRUD**

Ship Translate, Rewrite, and Summarize defaults from one function. Reject empty names/prompts and duplicate IDs. Duplicate creates a new ID and appends `Copy`/`副本` only in the UI layer, not in stable storage logic.

- [ ] **Step 6: Verify GREEN and full suite**

Run both focused tests, then full CTest. Expected: all pass with no API key text in output.

### Task 3: Bound Djev candidates, expose HTTP errors, and add fallback ranking

**Files:**
- Create: `src/decision/candidate_selector.hpp`
- Create: `src/decision/candidate_selector.cpp`
- Create: `src/decision/fallback_ranker.hpp`
- Create: `src/decision/fallback_ranker.cpp`
- Create: `src/net/http_client.hpp`
- Create: `src/net/http_client.cpp`
- Modify: `src/djev/djev_client.hpp`
- Modify: `src/djev/djev_client.cpp`
- Modify: `src/ui/desktop_flow.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/candidate_selector_test.cpp`
- Create: `tests/http_error_test.cpp`

**Interfaces:**
- Produces `ActionCatalog select_djev_candidates(const ActionCatalog&, const DecisionSnapshot&, std::size_t limit = 26)`.
- Produces `std::vector<RankedAction> rank_fallback(const ActionCatalog&, const DecisionSnapshot&, std::size_t limit = 5)`.
- Produces `HttpResponse { int status; std::string body; std::string transport_error; }` and injectable `HttpTransport`.
- `DjevClient` returns server error text in `DecisionResponse::error` without exposing request headers.

- [ ] **Step 1: Write a 30-action failing selector test**

Build a literal catalog spanning current/historical refs, action kinds, prompt actions, and destination variants. Assert exactly 26 unique enabled actions, every chosen action belongs to the full catalog, current-item actions precede history, and at least one action per distinct kind is preserved when capacity permits.

- [ ] **Step 2: Write failing 422/fallback tests**

Inject an `HttpTransport` returning:

```json
{"error":{"message":"question 'best_action': at most 26 alternatives","type":"validation_error"}}
```

Assert `DjevClient::decide` preserves HTTP 422 and the message, then assert `rank_fallback` returns five executable rows in deterministic order. Add a plain-text 422 body case whose message is `Djev request failed (HTTP 422)`.

- [ ] **Step 3: Run focused tests and verify RED**

Expected: the current flow sends the entire catalog and loses the response body.

- [ ] **Step 4: Implement reusable HTTP transport**

Use libcurl when compiled. Without libcurl, retain direct plain-HTTP sockets for local endpoints and add a fixed `curl` CLI adapter for HTTPS using a mode-0600 temporary config/header file so the API key is not present in argv. Return status/body separately. Delete temporary files after the child exits.

- [ ] **Step 5: Implement deterministic candidate selection and fallback**

Calculate stable priority tuples from current source, unseen action kind, configured/default target, clipboard recency, path recency, and action ID. Never mutate the full catalog. Pass only the bounded catalog into the Djev request and keep it as the catalog used to validate the response.

- [ ] **Step 6: Wire the desktop flow and verify GREEN**

On invalid Djev response, build the popup from fallback-ranked bounded actions and retain the error banner. Run focused tests and full CTest.

- [ ] **Step 7: Reproduce the original failure against live Djev**

Add a live utility case that builds 30 local actions, proves serialized criteria count is 26, calls the configured local Djev endpoint, and validates returned IDs. Do not print tokens or clipboard bodies.

### Task 4: Add text-transform actions and immutable prompt snapshots

**Files:**
- Modify: `src/core/action.hpp`
- Modify: `src/core/protocol.hpp`
- Modify: `src/actions/action_catalog.hpp`
- Modify: `src/actions/action_catalog.cpp`
- Modify: `src/ui/desktop_flow.hpp`
- Modify: `src/ui/desktop_flow.cpp`
- Create: `src/ai/prompt_expander.hpp`
- Create: `src/ai/prompt_expander.cpp`
- Create: `tests/prompt_action_test.cpp`

**Interfaces:**
- Adds `ActionKind::TransformText`.
- Adds immutable action parameters `template_id`, `template_name`, `system_prompt`, `temperature`, `llm_endpoint`, and `llm_model_id`; API key is held in the request job snapshot, not serialized to Djev.
- Produces `ExpandedPrompt expand_prompt(const PromptTemplate&, std::string_view text, PromptVariables)`.

- [ ] **Step 1: Write failing catalog tests**

Given two enabled and one disabled template, assert exactly two `TransformText` actions for a text source, stable IDs derived from source ref plus template ID, and descriptions suitable for Djev. Assert URL/email/path/image items do not receive these actions.

- [ ] **Step 2: Write failing placeholder tests**

Cover `{text}`, source/target language placeholders, unknown placeholder preservation, braces in source text, and the no-`{text}` two-message behavior with literal expected strings.

- [ ] **Step 3: Verify RED, implement minimal action/expander behavior, verify GREEN**

Add only the new enum/string mapping, catalog inputs, frozen parameters, and pure expansion logic. Run `action_catalog_test`, `prompt_action_test`, and full CTest.

### Task 5: OpenAI-compatible client and editable result state

**Files:**
- Create: `src/ai/openai_compatible_client.hpp`
- Create: `src/ai/openai_compatible_client.cpp`
- Create: `src/app/ai_result_state.hpp`
- Create: `src/app/ai_result_state.cpp`
- Modify: `src/executor/action_executor.hpp`
- Modify: `src/executor/action_executor.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/openai_client_test.cpp`
- Create: `tests/ai_result_state_test.cpp`

**Interfaces:**
- Produces `TextGenerationRequest`, `TextGenerationResult`, and `OpenAiCompatibleClient::generate` using injected `HttpTransport`.
- Produces `AiResultRecord { request_id, action_id, status, source_text, editable_text, error, frozen_request }`.
- `TransformText` execution returns an async-job descriptor; it does not block inside the filesystem executor.

- [ ] **Step 1: Write failing request/response tests**

Assert endpoint normalization for base URL and full `/v1/chat/completions`, correct model/messages/temperature/`stream:false`, bearer header passed separately to transport, parsing of `choices[0].message.content`, and readable errors for HTTP, malformed JSON, and missing content.

- [ ] **Step 2: Write failing result-state tests**

Assert a pending record transitions to completed/error only for its own request ID; clipboard changes do not stale a completed general-LLM result; editing updates only `editable_text`; retry reuses the frozen provider/prompt; a template mutation after dispatch cannot alter it.

- [ ] **Step 3: Verify RED and implement the client/state machine**

Use the shared HTTP transport and JSON parser. Do not add streaming. Store arbitrary-length result text in `std::string`; UI buffers will resize from this source of truth.

- [ ] **Step 4: Wire async dispatch in app orchestration**

Move pending-future handling from ad-hoc runtime fields to request-ID keyed app state. Keep Djev and text generation futures separate. Run focused tests and full CTest.

### Task 6: Persistent path history and Linux file-manager discovery

**Files:**
- Move/modify: `src/storage/path_history.*` to `src/history/path_history.*`
- Create: `src/history/path_history_store.hpp`
- Create: `src/history/path_history_store.cpp`
- Create: `src/platform/linux/linux_recent_paths.hpp`
- Create: `src/platform/linux/linux_recent_paths.cpp`
- Modify: `src/platform/linux/linux_desktop_services.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/content_detection_test.cpp`
- Create: `tests/path_history_store_test.cpp`
- Create: `tests/linux_recent_paths_test.cpp`

**Interfaces:**
- `PathHistory` stores file and directory observations instead of immediately replacing file entries with only their parent.
- `PathHistory::destination_directories()` derives valid directory targets separately.
- Produces `LinuxRecentPathCollector::scan_process(std::uint32_t pid)` and `scan_nautilus_service()`.

- [ ] **Step 1: Write failing persistence/normalization tests**

Cover file, folder, `file://`, multi-entry URI list, percent encoding, deduplication, updated recency, bounded persistence, and reload. Assert both a file and its parent can be displayed while only directories become copy/move destinations.

- [ ] **Step 2: Write a controlled `/proc` failing test**

Fork a child that opens a temporary file and directory and keeps the descriptors alive through a pipe. Scan the child PID and assert both paths are returned in recency order. Also open a pipe/socket and unlink one file; assert they are ignored. Skip only if `/proc` access is unavailable.

- [ ] **Step 3: Verify RED and implement history persistence**

Persist under the app data directory using atomic JSON writes. Apply an explicit item limit and stable normalized-path identity. Do not persist clipboard content in the path file.

- [ ] **Step 4: Implement Linux discovery**

Enumerate numeric `/proc/<pid>/fd` entries, use `readlink` and `lstat`, accept existing regular files/directories, and reject `socket:`, `pipe:`, `anon_inode:`, and ` (deleted)`. Find Nautilus PID through the focused context first and process lookup second; descriptor link timestamps are evidence for recency, not target file mtimes.

- [ ] **Step 5: Integrate observations and verify GREEN**

Poll discovery when opening the popup and at a low background cadence. Merge results through `PathHistory`; never let the Linux collector own application recency policy. Run focused and full tests.

### Task 7: Default save paths, guaranteed path actions, and execution feedback

**Files:**
- Modify: `src/actions/action_catalog.cpp`
- Modify: `src/executor/file_executor.cpp`
- Modify: `src/executor/url_executor.cpp`
- Modify: `src/executor/email_executor.cpp`
- Create: `src/app/execution_feedback.hpp`
- Create: `src/app/execution_feedback.cpp`
- Modify: `src/core/protocol.hpp`
- Modify: `tests/action_catalog_test.cpp`
- Modify: `tests/executor_test.cpp`
- Create: `tests/execution_feedback_test.cpp`

**Interfaces:**
- Catalog input gains validated default image/text directories.
- `ExecutionResult` gains a vector of output paths and operation metadata while retaining the first `output_path` compatibility field.
- Produces feedback commands `CopyOutputPath`, `OpenOutput`, and `OpenContainingDirectory` executed through `PlatformServices`.

- [ ] **Step 1: Write failing catalog tests**

Assert default text/image directories produce save actions even when no inferred recent path exists. For a path source plus two destination directories, assert Copy Path, two Copy To, and two Move To actions exist with unique IDs and visible target paths.

- [ ] **Step 2: Write failing collision and feedback tests**

Assert generated saves choose `name (2).ext` instead of overwriting, explicit copy/move retains documented overwrite behavior, multi-source operations return every result path, and feedback copy/open commands receive the literal completed path.

- [ ] **Step 3: Verify RED and implement path/default behavior**

Keep target validation in catalog construction and revalidate immediately before execution. Preserve rename-then-copy/remove move semantics and report partial failures explicitly.

- [ ] **Step 4: Implement feedback state and verify GREEN**

Feedback is keyed by action ID and request ID so a late completion cannot annotate a new popup batch. Run focused tests and full CTest.

### Task 8: Localization, Recent Paths view model, settings view model, and single instance

**Files:**
- Create: `src/ui/localization.hpp`
- Create: `src/ui/localization.cpp`
- Create: `src/ui/recent_paths_model.hpp`
- Create: `src/ui/recent_paths_model.cpp`
- Create: `src/ui/settings_model.hpp`
- Create: `src/ui/settings_model.cpp`
- Create: `src/platform/linux/linux_single_instance.hpp`
- Create: `src/platform/linux/linux_single_instance.cpp`
- Create: `tests/localization_test.cpp`
- Create: `tests/recent_paths_model_test.cpp`
- Create: `tests/settings_model_test.cpp`
- Create: `tests/single_instance_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces `tr(UiLanguage, UiTextKey)`, `RecentPathRow`, `RecentPathsModel`, and a settings working-copy model with Save/Cancel/Reset Section.
- Produces RAII `LinuxSingleInstance` with `acquired()` and a stable per-UID lock path.

- [ ] **Step 1: Write failing localization/model tests**

Assert English, Simplified Chinese, and system-locale resolution; unknown system locale falls back to English. Assert path rows include file/folder type, name, path, source, time, and enabled copy/open/copy-here/move-here commands based on source selection.

- [ ] **Step 2: Write failing settings working-copy tests**

Assert Cancel does not mutate saved settings, Save validates directories/provider fields before commit, Reset Section only resets the selected section, and provider test results do not save credentials.

- [ ] **Step 3: Write failing subprocess lock test**

Acquire the lock in the parent, run a child probe, and assert the child reports not acquired. Release the parent and assert a new child acquires it. Use a test-specific runtime directory.

- [ ] **Step 4: Verify RED and implement the pure models/lock**

Keep UI text keys centralized and do not translate stored prompt names automatically. Use `flock(LOCK_EX | LOCK_NB)` on Linux and write no sensitive process data to the lock file.

- [ ] **Step 5: Verify GREEN and full suite**

Run the four focused tests and CTest.

### Task 9: Split and expand Dear ImGui desktop UI

**Files:**
- Create: `src/ui/main_popup_panel.hpp`
- Create: `src/ui/main_popup_panel.cpp`
- Create: `src/ui/recent_paths_panel.hpp`
- Create: `src/ui/recent_paths_panel.cpp`
- Create: `src/ui/settings_panel.hpp`
- Create: `src/ui/settings_panel.cpp`
- Create: `src/ui/prompt_templates_panel.hpp`
- Create: `src/ui/prompt_templates_panel.cpp`
- Create: `src/ui/image_preview_panel.hpp`
- Create: `src/ui/image_preview_panel.cpp`
- Create: `src/ui/ai_result_panel.hpp`
- Create: `src/ui/ai_result_panel.cpp`
- Create: `src/ui/font_loader.hpp`
- Create: `src/ui/font_loader.cpp`
- Modify: `src/ui/desktop_runtime.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/ui_catalog_smoke_test.cpp`
- Create: `tests/ui_state_test.cpp`

**Interfaces:**
- Panels accept view models and emit command enums; panels never execute actions, write settings, or call HTTP directly.
- `ImagePreviewState` owns zoom (0.25-4.0), scroll intent, texture reference, source blob ref, and open/save/copy commands.
- `AiResultPanelState` owns a dynamically resized editable string, not a fixed C buffer.

- [ ] **Step 1: Write failing headless UI-state tests**

Assert two main tabs, five-or-fewer smart rows, fallback/error banner coexistence, image thumbnail click opens preview state, zoom clamp, arbitrary-length AI result editing, prompt CRUD commands, and successful file-action rows expose output-path commands.

- [ ] **Step 2: Verify RED and split the runtime**

Move rendering blocks into focused panel files without changing behavior first. Keep ImGui-dependent sources conditional on desktop dependencies. Make `desktop_runtime.cpp` own window/event/render lifecycle and command dispatch only.

- [ ] **Step 3: Implement movable/transparent main window**

Use the custom title region to emit drag deltas and call `PlatformServices::move_popup_by`. Apply the clamped setting through `set_popup_opacity` and ImGui background alpha. Store no absolute monitor assumptions in UI code.

- [ ] **Step 4: Implement CJK font and localization**

Ask the platform backend for preferred fonts, load Noto Sans CJK SC with Chinese glyph ranges when present, merge a symbol/icon-capable fallback if available, and surface a warning if loading fails. Route panel labels through `tr`.

- [ ] **Step 5: Implement Recent Paths and Settings panels**

Render the ImGui table and emit copy/open/copy-here/move-here commands. Render provider fields with password masking, connection-test buttons, default-directory fields, language and opacity controls, and Save/Cancel. Render prompt create/edit/delete/duplicate/enable/restore operations with delete confirmation.

- [ ] **Step 6: Implement image and AI result windows**

Make the thumbnail an image button. Render fit-to-window image preview with zoom/pan and original-aspect sizing. Render AI results in `InputTextMultiline` using a resize callback backed by `std::string`; add Copy, Replace Clipboard, Retry, and Close.

- [ ] **Step 7: Implement inline execution feedback**

Keep non-paste operations visible, show running/success/failure indicators, list result paths, and dispatch copy/open controls. Preserve auto-close only for successful direct-paste actions.

- [ ] **Step 8: Verify tests and perform visual smoke checks**

Run full CTest. Then run the app with `PASTIT_SHOW_ON_START=1 PASTIT_DIAGNOSTICS=1` and verify tabs, Chinese glyphs, drag, opacity, Settings, prompt CRUD, image preview, and result window. Diagnostics may report counts/IDs/status only.

### Task 10: End-to-end integration, live providers, documentation, and completion audit

**Files:**
- Modify: `tests/djev_live_integration.cpp`
- Create: `tests/openai_live_integration.cpp`
- Modify: `README.md`
- Modify: `scripts/bootstrap-local-deps.sh` only if the final build proves an additional header is strictly required
- Modify: `CMakeLists.txt`

**Interfaces:**
- `djev_live_integration` proves the bounded production payload against real local Djev.
- `openai_live_integration` is opt-in and runs only when endpoint/model/key configuration exists; it prints endpoint/model/status but never key or prompt body.

- [ ] **Step 1: Extend live Djev verification**

Build a request from more than 26 local actions, assert the production selector emits 26, call `/v1/systemone`, and validate every returned probability ID belongs to that bounded catalog. Repeat enough times to prove history growth no longer changes the bound.

- [ ] **Step 2: Add opt-in general-LLM live verification**

Send a short non-sensitive literal through one default prompt and assert non-empty returned content. Return CTest skip code when general-LLM settings are absent; never treat absence as success in the manual completion audit.

- [ ] **Step 3: Update README**

Document settings location, Djev versus general-LLM schemas, endpoint normalization, prompt CRUD, default paths, recent-path sources, tabs, image preview, Chinese font behavior, opacity/drag, singleton behavior, and fixed executor protocol. Preserve the dependency explanation matching the prior ImGui demo.

- [ ] **Step 4: Run a fresh build and all tests**

Run:

```bash
cmake -S . -B build-v2-final -DPASTIT_BUILD_TESTS=ON -DPASTIT_BUILD_DESKTOP=ON
cmake --build build-v2-final -j2
ctest --test-dir build-v2-final --output-on-failure
./build-v2-final/djev_live_integration
```

Run `openai_live_integration` when configured. Record explicit skip reason otherwise.

- [ ] **Step 5: Run desktop end-to-end scenarios**

Verify text, URL, email, image, path, JSON, resume, Translate, Rewrite, template CRUD, default saves, Nautilus path discovery, Recent Paths copy/open, path move/copy, image full preview, Chinese UI, movable/transparent window, result editing, and copy-new-path. Use actual X11 clipboard ownership and real configured providers; do not substitute mocks for this final pass.

- [ ] **Step 6: Verify packaging and portability**

Install to a temporary prefix, inspect `ldd` and `readelf -d`, and confirm no project-private runtime RPATH. Verify no platform header appears under `src/app`, `src/config`, `src/decision`, `src/ai`, `src/history`, `src/ui` view-model headers, or `src/executor` public headers.

- [ ] **Step 7: Request independent code review and close every finding**

Review against every acceptance criterion in the approved spec. Fix all Critical and Important findings with new failing tests, then repeat the fresh build, full CTest, live Djev check, configured OpenAI-compatible check, and desktop scenarios before marking the goal complete.
