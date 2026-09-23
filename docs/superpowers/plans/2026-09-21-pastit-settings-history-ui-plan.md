# PasteIt Settings, History, and Ranking UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make the popup non-topmost, expose eight ranked actions, clean recent-path sources, improve history/detail toolbars and type indicators, add bounded history pruning controls, and load General LLM model IDs from the provider's `/models` endpoint.

**Architecture:** Keep ranking and storage behavior in platform-independent model/store modules. Keep X11 process discovery limited to named file managers and shell-history parsing in the Linux backend. Add a small GET-capable HTTP path and an OpenAI-compatible model-list client; the desktop runtime owns asynchronous refresh and settings UI state.

**Tech Stack:** C++20, CMake, Dear ImGui multi-viewport, existing JSON parser, existing curl/socket HTTP transport, Linux/X11 backend.

**Spec:** This plan implements the eight requirements in the current user request.

## Global Constraints

- Do not add a new third-party dependency; reuse the bundled JSON parser and existing HTTP transport.
- Keep Djev's own decision schema unchanged; `/models` is only for the OpenAI-compatible General LLM provider.
- Preserve current-only clipboard decisions and fixed native action execution.
- Persist settings/history changes atomically and keep the app buildable in both desktop and headless configurations.

## Review Focus

- A normal window manager must be able to cover the popup after it is shown; verify the root GLFW window is not floating.
- Eight ranked rows must be retained through decision-session ranking and popup-model construction, including fallback ranking.
- A focused process's `/proc/<pid>/fd` entries must never become `proc-fd` recent-path rows; named file managers and bash/zsh history remain accepted.
- Long text in clipboard detail must be selectable/read-only and copyable without changing the stored value.
- Pruning ten/all records must update both in-memory stores and on-disk manifests/blob cleanup.
- An unavailable or malformed `/models` endpoint must leave the manually entered model usable and report a non-fatal status.

### Task 1: Ranking and root-window behavior

**Files:**
- Modify: `src/ui/desktop_runtime.cpp`
- Modify: `src/djev/decision_session.cpp`
- Modify: `src/ui/popup.cpp`
- Modify: `src/ui/main_popup_panel.cpp`
- Test: `tests/ui_catalog_smoke_test.cpp`

Use an application display limit of eight in `prepare_ranked_decision`, `build_popup_model`, and `MainPopupPanelState::clamp_rows`; leave lower-level `rank_top_actions` defaults unchanged for existing protocol tests. Remove `GLFW_FLOATING, GLFW_TRUE` from the root window hints; auxiliary windows remain independent through their existing ImGui window class.

### Task 2: Recent-path source policy

**Files:**
- Modify: `src/platform/linux/linux_recent_paths.hpp/.cpp`
- Modify: `src/platform/linux/linux_desktop_services.cpp`
- Modify: `src/storage/path_history.cpp`
- Modify: `src/history/path_history_store.cpp`
- Test: `tests/linux_recent_paths_test.cpp`
- Test: `tests/path_history_store_test.cpp`

Add named file-manager scanning (`nautilus`, `dolphin`, `thunar`, `nemo`, `pcmanfm`, `caja`) and bash/zsh history parsing for existing absolute paths. `LinuxDesktopServices::recent_paths()` must call named file-manager and shell-history collectors, never expose direct `scan_process()` results as `proc-fd`. Ignore legacy `proc-fd` records when loading/retrieving recent history so old persisted data disappears from the UI and Djev payload.

### Task 3: History model/panel presentation

**Files:**
- Modify: `src/ui/recent_paths_panel.cpp`
- Modify: `src/ui/clipboard_history_model.hpp/.cpp`
- Modify: `src/ui/clipboard_history_panel.cpp`
- Modify: `src/ui/localization.hpp/.cpp`
- Test: `tests/clipboard_history_model_test.cpp`
- Test: `tests/recent_paths_model_test.cpp`

Move the Recent Path Detail operation buttons to one top toolbar row and remove the bottom/right action placement. Add a clipboard history copy command and place Copy/Use/Save/Close at the top of the detail modal. Render non-image text details through a read-only `InputTextMultiline` so standard ImGui selection and Ctrl+C work; keep image preview behavior unchanged. Add a stable icon field for Text, URL, Email, Image, Path, JSON, and Unknown, show the icon in the table, and expose the text label as a tooltip.

### Task 4: History pruning API and settings controls

**Files:**
- Modify: `src/storage/path_history.hpp/.cpp`
- Modify: `src/storage/clipboard_store.hpp/.cpp`
- Modify: `src/ui/localization.hpp/.cpp`
- Modify: `src/ui/desktop_runtime.cpp`
- Test: `tests/path_history_store_test.cpp`
- Test: `tests/clipboard_history_store_test.cpp`

Add `PathHistory::retain_latest(size_t)` and `PathHistory::clear()`, plus `ClipboardStore::retain_latest(size_t)` and `ClipboardStore::clear()`. In the settings tab's first General section add top-level buttons for “Keep latest 10” and “Delete all” history. The runtime must rewrite path history, save the retained clipboard manifest, call orphan-blob cleanup, restore the in-memory clipboard store, clear open history-detail selections, and show a status message. Group language, opacity, default directories, and history controls under a General collapsing header.

### Task 5: OpenAI-compatible model discovery

**Files:**
- Modify: `src/net/http_client.hpp/.cpp`
- Create: `src/ai/model_catalog.hpp`
- Create: `src/ai/model_catalog.cpp`
- Modify: `CMakeLists.txt`
- Modify: `src/ui/desktop_runtime.cpp`
- Modify: `src/ui/localization.hpp/.cpp`
- Test: `tests/openai_client_test.cpp` or a new `tests/model_catalog_test.cpp`

Add an optional `HttpTransport::get_json()` operation and implement it for libcurl and the curl/socket fallback. Add `OpenAiCompatibleModelClient::list_models()` that normalizes a General LLM base/chat URL to `/v1/models`, parses standard `{ "data": [{"id":"..."}] }` (and a compatible `models` array), sorts/deduplicates IDs, and returns a non-fatal error otherwise. The settings tab keeps an editable model input and adds a combo/dropdown populated asynchronously from the endpoint; selecting an item writes the ID into the input. Refresh on the General LLM endpoint change or an explicit Refresh button, never overwrite a manually entered model on failure, and keep API-key headers private.

### Task 6: Verification and documentation

**Files:**
- Modify: `README.md`
- Modify: `.sdd/pasteit-current-clipboard-history/progress.md`
- Modify: `CMakeLists.txt` if test registration is needed

Run focused RED/GREEN tests for each new behavior, then desktop and headless builds with full CTest, X11 window inspection, local Djev live integration, and configured General LLM live integration. Document the eight-row display, source policy, pruning controls, and `/models` fallback behavior without exposing credentials.

## Execution Order

Implement Tasks 1–4 first because they are local and independent. Implement Task 5 after the HTTP interface is covered by a focused fake-transport test. Finish with Task 6 and keep the rebuilt `pastit` process running for inspection.
