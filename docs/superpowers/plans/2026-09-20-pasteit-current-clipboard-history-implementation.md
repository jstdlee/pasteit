# PasteIt Current Clipboard and History Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Smart Actions strictly current-clipboard-only, add a persistent 50-item clipboard manager, add confirmable file operations and richer table/modal UI, and run the local Djev deployment at an 8192-token context limit.

**Architecture:** Split the existing mixed runtime into platform-neutral decision, history, destination, confirmation, and UI-state units. Djev receives one bounded current clipboard record and a non-duplicated Choice schema; historical records remain local and persistent. Dear ImGui panels return typed commands while `desktop_runtime.cpp` owns platform calls, textures, persistence orchestration, and execution.

**Tech Stack:** C++20, CMake/CTest, Dear ImGui 1.91.9b, GLFW/OpenGL3, Linux X11, JSON utilities, local Djev `/v1/systemone`, Docker Compose/vLLM, and the existing OpenAI-compatible general LLM client.

**Spec:** `docs/superpowers/specs/2026-09-20-pasteit-current-clipboard-history-design.md`

## Global Constraints

- Smart Actions and Djev requests contain zero or one clipboard item; every candidate action must use that item's `source_ref`.
- Clipboard history is local-only, persists the newest 50 records including images, and never enters the Smart Actions request.
- Djev receives at most 26 Choice alternatives and a serialized request no larger than 10 KiB.
- Current preview is at most 256 UTF-8 code points, title 160, path summary 240, and criterion description 120.
- File-producing actions require explicit confirmation and use `yyyyMMddHH-NN.ext` names for generated saves.
- Model output never becomes a shell command, filesystem command, destination path, or filename.
- Platform-independent code cannot include X11, Win32, Cocoa, or GLFW-native headers.
- Linux directory choice tries Zenity, then KDialog, then leaves manual entry available.
- All new UI text has English and Simplified Chinese translations.
- API keys cannot appear in logs, tests, command lines, or review artifacts.
- The workspace has no usable Git repository. Do not invent commits; record RED/GREEN evidence in `.sdd/pasteit-current-clipboard-history/progress.md` and preserve a green build after each integration wave.

## Parallel Execution Map

The user selected subagent-driven execution. Use disjoint write scopes to gain parallelism despite the lack of Git worktrees:

- **Setup, serial:** Task 0 adds optional includes for disjoint CMake fragments.
- **Wave A, parallel:** Tasks 1, 2, 3, and 4. Workers must not edit `CMakeLists.txt`, `desktop_runtime.cpp`, localization files, or README; each owns only its numbered CMake fragment.
- **Wave B, parallel after Wave A contracts compile:** Tasks 5 and 6. Workers must not edit `desktop_runtime.cpp` or `CMakeLists.txt`.
- **Wave C, serial integration:** Task 7 owns `CMakeLists.txt`, `desktop_runtime.cpp`, localization, and cross-module wiring.
- **Wave D, serial external/live verification:** Tasks 8 and 9.

The controller reviews every worker's files before Wave C, resolves interface mismatches against the spec, and never lets two active workers edit the same file.

## Review Focus

- A current text record after historical image/email/path records must produce no historical-source candidate, fallback, or popup row; Task 1 pins this end to end.
- A malformed clipboard manifest must not trigger blob deletion or prevent new clipboard captures; Task 2 pins this recovery path.
- A path copied from a root directory, URI list, or missing source must not create an invalid source-parent destination; Task 4 pins these cases.
- A save confirmation left open while clipboard history is pruned must fail cleanly rather than execute against another ref; Task 3 and Task 7 pin frozen-source revalidation.
- An 8192-token server restart that fails readiness must roll back to the prior 4096 configuration without leaving Djev down; Task 8 exercises rollback.

---

### Task 0: Parallel-safe build and ledger scaffolding

**Files:**
- Modify: `CMakeLists.txt`
- Create: `.sdd/pasteit-current-clipboard-history/progress.md`

**Interfaces:**
- Produces: six optional, disjoint CMake fragment entry points at `cmake/pasteit-v3-task1.cmake` through `cmake/pasteit-v3-task6.cmake`.
- Produces: the plan-owned progress ledger consumed by every worker.

- [ ] **Step 1: Add optional fragment includes after the existing test declarations**

Add exactly:

```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/pasteit-v3-task1.cmake OPTIONAL)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/pasteit-v3-task2.cmake OPTIONAL)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/pasteit-v3-task3.cmake OPTIONAL)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/pasteit-v3-task4.cmake OPTIONAL)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/pasteit-v3-task5.cmake OPTIONAL)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/pasteit-v3-task6.cmake OPTIONAL)
```

Every fragment must guard test targets with `if(PASTIT_BUILD_TESTS)` and may call `target_sources(pastit_core PRIVATE ...)` only for files owned by that task.

- [ ] **Step 2: Create the ledger**

The first line must identify this plan:

```markdown
# SDD ledger — plan: /home/user/dev/pastit/docs/superpowers/plans/2026-09-20-pasteit-current-clipboard-history-implementation.md
```

Add a table for Tasks 0-9 with Status, RED evidence, GREEN evidence, and Rulings columns. Record that the workspace has no usable Git repository and that numbered CMake fragments are the parallel write boundary.

- [ ] **Step 3: Verify the unchanged baseline through the new build directory**

Run:

```bash
cmake -S . -B build-v3 -DPASTIT_BUILD_TESTS=ON -DPASTIT_BUILD_DESKTOP=ON
cmake --build build-v3 -j2
ctest --test-dir build-v3 --output-on-failure
```

Expected: the existing 28 tests pass, with only the existing hotkey skip allowed when another PasteIt owns Ctrl+Alt+F.

- [ ] **Step 4: Record Task 0 evidence**

Write the configure/build/test output summary into the ledger before dispatching Wave A.

### Task 1: Current-only Smart Actions and compact Djev payload

**Files:**
- Modify: `src/ui/desktop_flow.hpp`
- Modify: `src/ui/desktop_flow.cpp`
- Modify: `src/decision/candidate_selector.hpp`
- Modify: `src/decision/candidate_selector.cpp`
- Modify: `src/djev/djev_client.cpp`
- Modify: `src/ui/popup.cpp`
- Modify: `tests/desktop_flow_test.cpp`
- Modify: `tests/candidate_selector_test.cpp`
- Modify: `tests/djev_response_test.cpp`
- Create: `tests/djev_payload_budget_test.cpp`
- Create: `cmake/pasteit-v3-task1.cmake`

**Interfaces:**
- Consumes: existing `DesktopDecisionInput`, `DecisionSnapshot`, `ActionCatalog`, and `DjevClient::build_payload`.
- Produces: `DesktopDecisionBatch build_desktop_decision(const DesktopDecisionInput&)` whose snapshot contains only the newest clipboard item.
- Produces: `bool is_current_source_action(const ActionInstance&, const DecisionSnapshot&)` in `candidate_selector.hpp` for selector, fallback, and popup guards.
- Produces: compact Djev JSON that retains executable actions only in local `DesktopDecisionBatch::catalog`.

- [ ] **Step 1: Write the historical-action regression tests**

Add current text plus older image/email/path records and assert the request, selected candidates, fallback input, and popup rows all reference only `clip_current`:

```cpp
DesktopDecisionInput input;
input.clipboard_items = {
    {.ref="clip_image", .kind=ContentKind::Image, .captured_at_ms=100},
    {.ref="clip_email", .kind=ContentKind::Email, .captured_at_ms=200},
    {.ref="clip_current", .kind=ContentKind::Text,
     .preview="current text", .captured_at_ms=300},
};
const auto batch = build_desktop_decision(input);
assert(batch.request.snapshot.clipboard_items.size() == 1);
assert(batch.request.snapshot.clipboard_items.front().ref == "clip_current");
for (const auto& action : batch.catalog.actions) {
    assert(action.source_ref == "clip_current");
}
for (const auto& action : batch.request.snapshot.available_actions) {
    assert(action.source_ref == "clip_current");
}
```

Create the task fragment with only the new test target at this RED stage:

```cmake
if(PASTIT_BUILD_TESTS)
  add_executable(djev_payload_budget_test tests/djev_payload_budget_test.cpp)
  target_link_libraries(djev_payload_budget_test PRIVATE pastit_core)
  add_test(NAME djev_payload_budget_test COMMAND djev_payload_budget_test)
endif()
```

- [ ] **Step 2: Run focused tests and verify RED**

Run:

```bash
cmake -S . -B build-v3 -DPASTIT_BUILD_TESTS=ON -DPASTIT_BUILD_DESKTOP=ON
cmake --build build-v3 -j2 --target desktop_flow_test candidate_selector_test djev_response_test
./build-v3/desktop_flow_test
```

Expected: the new assertion reports multiple clipboard items or a historical `source_ref`.

- [ ] **Step 3: Make desktop decision construction current-only**

Sort `input.clipboard_items`, copy only the newest item into the decision snapshot, and hash only that item:

```cpp
auto items = input.clipboard_items;
std::stable_sort(items.begin(), items.end(), newest_first);
if (!items.empty()) snapshot.clipboard_items.push_back(items.front());
snapshot.clipboard_hash = snapshot_hash(snapshot.clipboard_items);
```

Remove the obsolete `history_limit` parameter from the declaration, definition, and all call sites.

- [ ] **Step 4: Add selector and popup defense in depth**

Define and apply:

```cpp
bool is_current_source_action(const ActionInstance& action,
                              const DecisionSnapshot& snapshot) {
    return !snapshot.clipboard_items.empty() &&
           action.source_ref == snapshot.clipboard_items.front().ref;
}
```

Filter before candidate sorting and ignore any mismatched ranked row in `build_popup_model`.

- [ ] **Step 5: Write the payload-budget RED test**

Build 26 actions with 400-character descriptions, six 500-character paths, and long CJK previews. Assert:

```cpp
const auto payload = DjevClient::build_payload(request, "jev-latest");
assert(payload.size() <= 10 * 1024);
assert(payload.find("\"history\"") == std::string::npos);
assert(payload.find("\"available_actions\"") == std::string::npos);
assert(count_occurrences(payload, action.id) == 1);
```

- [ ] **Step 6: Run the payload test and verify RED**

Run:

```bash
cmake -S . -B build-v3 -DPASTIT_BUILD_TESTS=ON -DPASTIT_BUILD_DESKTOP=ON
cmake --build build-v3 -j2 --target djev_payload_budget_test
./build-v3/djev_payload_budget_test
```

Expected: target is unavailable until integration or the payload exceeds 10 KiB and contains duplicate sections.

- [ ] **Step 7: Implement bounded UTF-8 fields and non-duplicated payload**

Add a local UTF-8-safe truncation helper and serialize only compact state plus Choice criteria:

```cpp
out << "\"state\":{";
write_current_clipboard(out, current, 256);
write_target(out, snapshot, 160);
write_required_paths(out, snapshot, 6, 240);
out << "},\"questions\":{\"best_action\":{"
       "\"type\":\"choice\","
       "\"instructions\":\"Choose the most useful current clipboard action.\","
       "\"criteria\":{";
write_action_criteria(out, request.snapshot.available_actions, 120);
out << "}}}";
```

If the result exceeds 10 KiB, remove lowest-priority destination variants and rebuild; never remove the final action or exceed 26 choices.

- [ ] **Step 8: Verify Task 1 GREEN**

Run the four focused targets and the existing `http_error_test` and `decision_session_test`. Expected: all pass and the budget assertion is at most 10 KiB.

- [ ] **Step 9: Record Task 1 evidence**

Append the exact RED and GREEN commands/results to `.sdd/pasteit-current-clipboard-history/progress.md` without editing any Wave A peer's files.

### Task 2: Persistent 50-item clipboard history

**Files:**
- Modify: `src/core/types.hpp`
- Modify: `src/storage/clipboard_store.hpp`
- Modify: `src/storage/clipboard_store.cpp`
- Create: `src/history/clipboard_history_store.hpp`
- Create: `src/history/clipboard_history_store.cpp`
- Create: `tests/clipboard_history_store_test.cpp`
- Create: `tests/clipboard_orphan_cleanup_test.cpp`
- Create: `cmake/pasteit-v3-task2.cmake`

**Interfaces:**
- Produces: `ClipboardItem::content_hash`.
- Produces: `ClipboardStore::restore(std::vector<ClipboardItem>, std::uint64_t next_ref)`, `items_newest_first(std::size_t)`, and `next_ref()`.
- Produces: `ClipboardHistoryLoadResult ClipboardHistoryStore::load() const`, `ClipboardHistorySaveResult ClipboardHistoryStore::save(const std::vector<ClipboardItem>&, std::uint64_t) const`, and `bool ClipboardHistoryStore::cleanup_orphan_blobs(const ClipboardHistorySaveResult&, std::string&) const`, with a fixed default limit of 50.
- Consumers: Task 5 models and Task 7 runtime integration.

- [ ] **Step 1: Write persistence and retention tests**

Use a temporary data directory, insert 52 records including PNG-like byte arrays, persist, reconstruct the stores, and assert:

```cpp
assert(reloaded.items.size() == 50);
assert(reloaded.items.front().captured_at_ms == 1051);
assert(reloaded.items.back().captured_at_ms == 1002);
store.restore(reloaded.items, reloaded.next_ref);
assert(store.read(reloaded.items.front().ref) == expected_latest_bytes);
assert(store.put(next_data).ref != reloaded.items.front().ref);
```

Register only the two tests in `cmake/pasteit-v3-task2.cmake` so the first compile fails on the missing production APIs rather than an unknown build target.

- [ ] **Step 2: Write malformed-manifest and orphan-cleanup tests**

Create one shared blob, one unique pruned blob, and a malformed manifest. Assert malformed load returns a warning and cleanup is not authorized; after a valid atomic save, the unique unreferenced hash blob is removed while the shared blob remains.

- [ ] **Step 3: Run tests and verify RED**

Run:

```bash
cmake -S . -B build-v3 -DPASTIT_BUILD_TESTS=ON -DPASTIT_BUILD_DESKTOP=ON
cmake --build build-v3 -j2 --target clipboard_history_store_test clipboard_orphan_cleanup_test
```

Expected: compilation fails because `ClipboardHistoryStore`, `content_hash`, and restore APIs do not exist.

- [ ] **Step 4: Add stable refs and restore support**

Generate process-independent refs from timestamp plus a monotonically restored serial:

```cpp
std::string make_ref(std::int64_t captured_at_ms, std::uint64_t serial) {
    return "clip_" + std::to_string(captured_at_ms) + "_" + std::to_string(serial);
}
```

Store `content_hash` on every item. `restore` validates unique refs and advances the serial above the loaded value.

- [ ] **Step 5: Implement atomic manifest load/save**

The manifest is a JSON array with schema version and complete item metadata. Save to `clipboard-history.json.tmp`, flush/close, then rename. Convert blob paths from hash values rather than trusting arbitrary paths from JSON.

Add `src/history/clipboard_history_store.cpp` to `pastit_core` in the task-owned CMake fragment after creating the implementation.

- [ ] **Step 6: Implement retention, adjacent coalescing, and cleanup guard**

Sort newest first, coalesce only adjacent equal hashes, resize to 50, and expose cleanup only from a successful save result:

```cpp
struct ClipboardHistorySaveResult {
    bool ok = false;
    std::vector<std::filesystem::path> referenced_blobs;
    std::string error;
};
```

Only hash-named `.bin` files under the configured blob directory are eligible for deletion.

- [ ] **Step 7: Verify Task 2 GREEN**

Run:

```bash
cmake --build build-v3 -j2 --target clipboard_history_store_test clipboard_orphan_cleanup_test core_contracts_test content_detection_test
./build-v3/clipboard_history_store_test
./build-v3/clipboard_history_store_test
./build-v3/clipboard_orphan_cleanup_test
./build-v3/core_contracts_test
./build-v3/content_detection_test
```

Expected: every command exits zero; the repeated persistence test proves restart stability and no ref collision.

- [ ] **Step 8: Record Task 2 evidence**

Append RED/GREEN evidence and the manifest schema to the plan ledger.

### Task 3: Generated filenames and file-operation confirmation state

**Files:**
- Create: `src/app/generated_filename.hpp`
- Create: `src/app/generated_filename.cpp`
- Create: `src/app/file_operation_confirmation.hpp`
- Create: `src/app/file_operation_confirmation.cpp`
- Create: `tests/generated_filename_test.cpp`
- Create: `tests/file_operation_confirmation_test.cpp`
- Create: `cmake/pasteit-v3-task3.cmake`

**Interfaces:**
- Produces: `std::string generated_filename(ActionKind, const ClipboardItem&, std::chrono::system_clock::time_point, const std::filesystem::path&)`.
- Produces: `bool needs_file_confirmation(ActionKind)`.
- Produces: `FileOperationDraft make_file_operation_draft(const ActionInstance&, const ClipboardItem&, std::vector<PathLocation>, std::chrono::system_clock::time_point)`, `bool validate_file_operation_draft(FileOperationDraft&, const ClipboardStore&)`, and `std::optional<ActionInstance> confirmed_action(const FileOperationDraft&, const ClipboardStore&)`.
- Consumers: Task 6 confirmation panel and Task 7 runtime/executors.

- [ ] **Step 1: Write deterministic filename tests**

Use a fixed local time representing 2026-09-20 20:00 and existing files `2026092020-01.png` and `-02.png`:

```cpp
assert(generated_filename(ActionKind::SaveImageFile, image, fixed_time, dir)
       == "2026092020-03.png");
assert(generated_filename(ActionKind::SaveTextFile, text, fixed_time, empty_dir)
       == "2026092020-01.txt");
```

Cover JPEG, JSON, EML, URL, URL-download suffix, and binary fallback.

Register both Task 3 tests, but no new production sources, in `cmake/pasteit-v3-task3.cmake` for the RED build.

- [ ] **Step 2: Write confirmation state tests**

Assert save/download/copy/move need confirmation, paste/open/compose/transform do not, Cancel produces no action, edited filenames are retained, separators are rejected, nonexistent destinations disable confirmation, and a missing frozen source fails revalidation.

- [ ] **Step 3: Run focused tests and verify RED**

Run:

```bash
cmake -S . -B build-v3 -DPASTIT_BUILD_TESTS=ON -DPASTIT_BUILD_DESKTOP=ON
cmake --build build-v3 -j2 --target generated_filename_test file_operation_confirmation_test
```

Expected: compilation fails because the generated-filename and confirmation headers/functions do not exist.

- [ ] **Step 4: Implement filename generation and validation**

Use local time via `std::chrono`, a two-digit serial starting at 01, and action/content-derived extension. Reject empty names, `.`/`..`, `/`, NUL, and directory traversal. Keep user-entered UTF-8 names intact.

- [ ] **Step 5: Implement immutable draft state**

Define:

```cpp
struct FileOperationDraft {
    ActionInstance frozen_action;
    std::string frozen_source_ref;
    std::filesystem::path destination;
    std::string filename;
    std::vector<PathLocation> destinations;
    std::string validation_error;
};
```

`confirmed_action` returns a cloned action with the confirmed filename and a reserved `parameters["confirmed_destination"]`; it never mutates the source action.

Add both new Task 3 `.cpp` files to `pastit_core` through the task-owned CMake fragment.

- [ ] **Step 6: Verify Task 3 GREEN**

Run:

```bash
cmake --build build-v3 -j2 --target generated_filename_test file_operation_confirmation_test executor_test
./build-v3/generated_filename_test
./build-v3/file_operation_confirmation_test
./build-v3/executor_test
```

Expected: all three executables exit zero; Task 7 later adds executor cases for confirmed overrides.

- [ ] **Step 7: Record Task 3 evidence**

Append RED/GREEN evidence and the exact filename format to the ledger.

### Task 4: Destination resolver and platform directory chooser

**Files:**
- Create: `src/history/path_target_resolver.hpp`
- Create: `src/history/path_target_resolver.cpp`
- Modify: `src/platform/platform_services.hpp`
- Modify: `src/platform/linux/linux_desktop_services.hpp`
- Modify: `src/platform/linux/linux_desktop_services.cpp`
- Modify: `tests/platform_contracts_test.cpp`
- Create: `tests/path_target_resolver_test.cpp`
- Create: `cmake/pasteit-v3-task4.cmake`

**Interfaces:**
- Produces: `std::vector<DestinationCandidate> resolve_file_targets(const ClipboardItem&, std::string_view source_text, std::optional<std::filesystem::path> manual, std::optional<std::filesystem::path> focused, std::optional<std::filesystem::path> configured_default, const std::vector<PathLocation>& recent)` plus `DestinationRole` and `DestinationCandidate`.
- Extends: `PlatformServices::choose_directory(initial_directory)`.
- Consumers: Task 5 recent-path detail model, Task 6 confirmation panel, and Task 7 runtime.

- [ ] **Step 1: Write resolver tests**

Create a current file path whose parent is older than six recent directories. Assert source parent remains second after a manual destination, duplicates collapse, focused/default directories precede ordinary recent paths, and temp is the final fallback.

Cover `/file-at-root`, a multi-file URI list, an absent source, a source directory, and a file whose parent no longer exists.

Register `path_target_resolver_test` in `cmake/pasteit-v3-task4.cmake` before creating the resolver implementation.

- [ ] **Step 2: Write the platform contract RED test**

Extend `FakePlatformServices`:

```cpp
std::optional<std::filesystem::path> choose_directory(
    const std::filesystem::path& initial) override {
    last_initial = initial;
    return chosen_directory;
}
```

Assert cancellation returns `std::nullopt` and no platform-native type crosses the interface.

- [ ] **Step 3: Run focused tests and verify RED**

Run:

```bash
cmake -S . -B build-v3 -DPASTIT_BUILD_TESTS=ON -DPASTIT_BUILD_DESKTOP=ON
cmake --build build-v3 -j2 --target path_target_resolver_test platform_contracts_test
```

Expected: compilation fails because `DestinationCandidate`, `resolve_file_targets`, and `choose_directory` are missing.

- [ ] **Step 4: Implement ordered, de-duplicated resolution**

Define:

```cpp
enum class DestinationRole {
    Manual, SourceParent, FocusedDirectory,
    ConfiguredDefault, Recent, Temporary
};
struct DestinationCandidate {
    std::filesystem::path path;
    DestinationRole role;
    bool exists = false;
};
```

Normalize absolute paths, canonicalize only with an error code, preserve the first/highest role, and include only existing directories except that an invalid manual destination remains visible with `exists=false` for validation feedback.

Add `src/history/path_target_resolver.cpp` to `pastit_core` through the task-owned CMake fragment.

- [ ] **Step 5: Implement Linux chooser without shell interpolation**

Use `posix_spawnp` plus a pipe to capture stdout. Try argument vectors exactly:

```cpp
{"zenity", "--file-selection", "--directory", "--filename", initial_with_slash}
{"kdialog", "--getexistingdirectory", initial.string()}
```

Do not invoke `sh -c`. Trim one trailing newline, require an existing directory, and return `std::nullopt` for cancellation or unavailable programs.

- [ ] **Step 6: Verify Task 4 GREEN**

Run:

```bash
cmake --build build-v3 -j2 --target path_target_resolver_test platform_contracts_test linux_recent_paths_test test_x11_integration
./build-v3/path_target_resolver_test
./build-v3/platform_contracts_test
./build-v3/linux_recent_paths_test
./build-v3/test_x11_integration
```

Expected: all non-display cases exit zero; X11 integration may use its established display skip code only when X11 is unavailable.

- [ ] **Step 7: Record Task 4 evidence**

Append RED/GREEN evidence and Linux chooser fallback behavior to the ledger.

### Task 5: Clipboard and recent-path table/detail models

**Files:**
- Create: `src/ui/clipboard_history_model.hpp`
- Create: `src/ui/clipboard_history_model.cpp`
- Create: `src/ui/clipboard_history_panel.hpp`
- Create: `src/ui/clipboard_history_panel.cpp`
- Modify: `src/ui/recent_paths_model.hpp`
- Modify: `src/ui/recent_paths_model.cpp`
- Modify: `src/ui/recent_paths_panel.hpp`
- Modify: `src/ui/recent_paths_panel.cpp`
- Create: `tests/clipboard_history_model_test.cpp`
- Modify: `tests/recent_paths_model_test.cpp`
- Create: `cmake/pasteit-v3-task5.cmake`

**Interfaces:**
- Consumes: Task 2 `ClipboardItem::content_hash` and Task 4 destination/action IDs supplied by integration.
- Produces: headless `ClipboardHistoryModel`, `ClipboardHistoryDetail`, `RecentPathDetail`, `ClipboardHistoryCommand draw_clipboard_history_panel(...)`, and `RecentPathCommand draw_recent_paths_panel(...)`.
- Consumers: Task 7 runtime owns bytes, textures, platform calls, and action execution.

- [ ] **Step 1: Write model RED tests**

Assert newest-first rows expose type, preview, source, bytes, timestamp, image flag, blob availability, and stable ref. Assert selected detail survives row reordering by ref and closes when the record disappears.

Extend recent-path tests to assert full path detail, parent path for file rows, exact copy/move action IDs, and disabled operation commands for missing destinations.

Register `clipboard_history_model_test` in `cmake/pasteit-v3-task5.cmake` before creating the model implementation.

- [ ] **Step 2: Run model tests and verify RED**

Run:

```bash
cmake -S . -B build-v3 -DPASTIT_BUILD_TESTS=ON -DPASTIT_BUILD_DESKTOP=ON
cmake --build build-v3 -j2 --target clipboard_history_model_test recent_paths_model_test
```

Expected: compilation fails because clipboard model/detail types are missing and recent rows lack detail/action IDs.

- [ ] **Step 3: Implement headless models and commands**

Use command variants that contain identifiers, not services:

```cpp
struct ClipboardHistoryCommand {
    enum class Kind { None, View, Use, Save, CloseDetail } kind;
    std::string ref;
};
struct RecentPathCommand {
    enum class Kind { None, View, CopyPath, Open, OpenParent,
                      CopyHere, MoveHere, UseAsDestination, CloseDetail } kind;
    std::string path_ref;
    std::string action_id;
};
```

- [ ] **Step 4: Implement ImGui tables and detail modals**

Use `ImGuiTableFlags_Resizable | Reorderable | Hideable | ScrollY | RowBg | Borders | SizingStretchProp`. Give Preview/Path stretch weight 4, metadata weight 1, and Actions a fixed minimum width. A row double-click emits View. Panels do not read blobs or call platform services.

The clipboard panel accepts a texture lookup callback that returns an optional opaque texture handle and dimensions; runtime retains texture ownership.

Add the two model `.cpp` files to `pastit_core` and the two panel `.cpp` files to the desktop-capable core target through the Task 5 fragment.

- [ ] **Step 5: Verify Task 5 GREEN**

Run:

```bash
cmake --build build-v3 -j2 --target clipboard_history_model_test recent_paths_model_test ui_state_test
./build-v3/clipboard_history_model_test
./build-v3/recent_paths_model_test
./build-v3/ui_state_test
```

Expected: all three executables exit zero and panel sources compile through the Task 5 fragment.

- [ ] **Step 6: Record Task 5 evidence**

Append RED/GREEN evidence and command contracts to the ledger.

### Task 6: Prompt table, prompt actions, and confirmation panel

**Files:**
- Modify: `src/ui/prompt_templates_panel.hpp`
- Modify: `src/ui/prompt_templates_panel.cpp`
- Modify: `src/ui/main_popup_panel.hpp`
- Modify: `src/ui/main_popup_panel.cpp`
- Create: `src/ui/file_operation_confirmation_panel.hpp`
- Create: `src/ui/file_operation_confirmation_panel.cpp`
- Create: `src/ui/imgui_widgets.hpp`
- Create: `src/ui/imgui_widgets.cpp`
- Create: `tests/prompt_templates_panel_model_test.cpp`
- Create: `tests/prompt_action_visibility_test.cpp`
- Create: `tests/file_operation_confirmation_panel_test.cpp`
- Create: `cmake/pasteit-v3-task6.cmake`

**Interfaces:**
- Consumes: Task 3 `FileOperationDraft` and existing `PromptTemplateService`.
- Produces: `PromptTemplatesPanelState`, `PromptTemplatePanelCommand`, `std::vector<ActionInstance> current_prompt_actions(const ActionCatalog&, std::string_view current_ref)`, and `FileOperationPanelCommand`.
- Consumers: Task 7 runtime applies service/storage/platform side effects.

- [ ] **Step 1: Write prompt state RED tests**

Assert edit uses a detached draft until Save, Cancel discards it, Duplicate returns the new stable ID and opens its draft, Delete freezes the clicked ID despite selection changes, and disabled templates do not appear in current prompt actions.

Build a ranked Top 5 without transforms and assert every enabled current-text transform still appears in `current_prompt_actions`; assert URL/image/path current items return none.

Register all three Task 6 tests in `cmake/pasteit-v3-task6.cmake` before adding production sources.

- [ ] **Step 2: Write confirmation panel-state RED test**

Assert Browse emits the initial directory, choosing a recent/source-parent shortcut changes only the draft, Confirm is disabled on validation error, Cancel emits no executable action, and Confirm carries a frozen draft copy.

- [ ] **Step 3: Run focused tests and verify RED**

Run:

```bash
cmake -S . -B build-v3 -DPASTIT_BUILD_TESTS=ON -DPASTIT_BUILD_DESKTOP=ON
cmake --build build-v3 -j2 --target prompt_templates_panel_model_test prompt_action_visibility_test file_operation_confirmation_panel_test
```

Expected: compilation fails because the new state, command, and prompt-action types are missing.

- [ ] **Step 4: Implement headless modal states**

Define commands with frozen IDs/copies:

```cpp
struct PromptTemplatePanelCommand {
    enum class Kind { None, View, BeginEdit, SaveEdit,
                      Duplicate, BeginDelete, ConfirmDelete, CancelModal } kind;
    std::string template_id;
    std::optional<PromptTemplate> draft;
};
struct FileOperationPanelCommand {
    enum class Kind { None, Browse, Confirm, Cancel } kind;
    std::optional<FileOperationDraft> draft;
};
```

- [ ] **Step 5: Implement table/modal rendering and shared widgets**

Move resize-safe `std::string` input helpers from `desktop_runtime.cpp` into `imgui_widgets.*`. Render prompt columns Enabled, Name, Temperature, Built-in/Custom, Actions and compact View/Edit/Duplicate/Delete buttons with localized tooltips. Render file operation source, directory, Browse, destination shortcuts, filename, output preview, validation, Confirm, and Cancel.

Add Task 6 model/panel/widget `.cpp` files to the appropriate core or desktop-capable target through the task-owned CMake fragment.

- [ ] **Step 6: Verify Task 6 GREEN**

Run:

```bash
cmake --build build-v3 -j2 --target prompt_templates_panel_model_test prompt_action_visibility_test file_operation_confirmation_panel_test prompt_template_test prompt_action_test settings_model_test
./build-v3/prompt_templates_panel_model_test
./build-v3/prompt_action_visibility_test
./build-v3/file_operation_confirmation_panel_test
./build-v3/prompt_template_test
./build-v3/prompt_action_test
./build-v3/settings_model_test
```

Expected: every executable exits zero.

- [ ] **Step 7: Record Task 6 evidence**

Append RED/GREEN evidence and modal-state invariants to the ledger.

### Task 7: Integrate runtime, executors, localization, and build graph

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/actions/action_catalog.cpp`
- Modify: `src/executor/action_executor.hpp`
- Modify: `src/executor/file_executor.cpp`
- Modify: `src/executor/url_executor.cpp`
- Modify: `src/executor/email_executor.cpp`
- Modify: `src/ui/desktop_runtime.cpp`
- Modify: `src/ui/image_preview_panel.hpp`
- Modify: `src/ui/image_preview_panel.cpp`
- Modify: `src/ui/localization.hpp`
- Modify: `src/ui/localization.cpp`
- Modify: `tests/executor_test.cpp`
- Modify: `tests/desktop_flow_test.cpp`
- Modify: `tests/localization_test.cpp`
- Modify: `tests/ui_state_test.cpp`

**Interfaces:**
- Consumes: every contract from Tasks 1-6.
- Produces: complete three-tab Dear ImGui runtime, persistent capture lifecycle, confirmed file execution, prompt table, and localized detail modals.

- [ ] **Step 1: Audit and normalize all new source/test registrations**

Configure `build-v3`, list CTest targets, and inspect every numbered CMake fragment. Ensure each Task 1-6 `.cpp` is registered exactly once, each focused test is registered exactly once, and ImGui panel sources link the existing `pastit_imgui` dependency through the same desktop-capable target pattern already used by the project. Keep the numbered fragments as the permanent parallel ownership boundary; do not duplicate their entries in `CMakeLists.txt`.

- [ ] **Step 2: Write runtime-integration RED tests before wiring**

Extend `executor_test.cpp` with edited save filename, copy rename, move rename, collision suffix, same-path rejection, and missing frozen source. Extend `desktop_flow_test.cpp` to assert the runtime-facing batch exposes one current item while a separate 50-row history model remains available. Extend `ui_state_test.cpp` for command-driven opening/closing of clipboard, path, prompt, and confirmation modals. Extend `localization_test.cpp` with all keys listed in Step 8.

- [ ] **Step 3: Run integration tests and verify RED**

Run:

```bash
cmake -S . -B build-v3 -DPASTIT_BUILD_TESTS=ON -DPASTIT_BUILD_DESKTOP=ON
cmake --build build-v3 -j2
ctest --test-dir build-v3 --output-on-failure
```

Expected: contract mismatches or missing runtime/executor consumption fail; record each exact failure before integration edits.

- [ ] **Step 4: Integrate history startup, capture, and shutdown**

At startup load metadata, restore `ClipboardStore`, and surface warnings. After each capture atomically save retained history and perform guarded cleanup. Replace `clipboard_history(clipboard_store)` in Smart Actions with only `items_newest_first(1)`; pass all 50 records only to `ClipboardHistoryModel`.

- [ ] **Step 5: Integrate action confirmation and executor overrides**

Before executing an action for which `needs_file_confirmation` is true, create and open a draft instead. On Confirm:

```cpp
auto confirmed = confirmed_action(draft);
auto manual = path_history.observe_path(
    draft.destination, PathKind::Directory, "manual", current_time_ms());
confirmed.target_ref = manual.ref;
confirmed.filename = draft.filename;
execute_confirmed(confirmed);
```

Make file, URL, and email executors respect nonempty confirmed filenames. For copy/move, use the confirmed filename as the destination basename; preserve collision-safe behavior and same-path rejection.

- [ ] **Step 6: Integrate three tabs and typed panel commands**

Replace inline recent-path/prompt rendering with panel calls. Add Clipboard History. Runtime dispatches View/Use/Save/Open/Browse/Confirm commands and owns a small LRU texture cache keyed by clipboard ref. Image detail uses original bytes and the existing zoom clamp.

- [ ] **Step 7: Integrate current prompt actions**

Render Djev Top 5 first and every result of `current_prompt_actions(active_batch.catalog, current_ref)` below it. If General LLM endpoint/model is incomplete, render disabled rows and a Settings shortcut instead of dispatching an invalid request.

- [ ] **Step 8: Add all localization keys**

Cover Clipboard History, Preview, Size, Captured, Details, View, Edit, Close, MIME Types, Missing Data, Prompt Actions, Configure General LLM, Built-in, Custom, Browse, Destination, Filename, Output Path, Confirm, invalid-path/name errors, source parent, focused directory, manual destination, and delete/duplicate modal text. Extend the localization test with an English/nonempty-Chinese assertion for every key.

- [ ] **Step 9: Verify Task 7 GREEN**

Run the full `build-v3` CTest suite. Expected: every old and new test passes; display-dependent hotkey tests may return only the existing skip code when another process owns the shortcut.

- [ ] **Step 10: Record Task 7 evidence**

Append the build command, test count, skips, and integration rulings to the ledger.

### Task 8: Raise local Djev to 8192 and run live compact-payload verification

**Files:**
- Modify: `/home/user/dev/djev-spark/.env`
- Modify: `/home/user/dev/pastit/.env`
- Modify: `tests/djev_live_integration.cpp`
- Modify: `README.md`

**Interfaces:**
- Consumes: Task 1 compact request and the existing local Djev endpoint/token loading.
- Produces: running vLLM command with `--max-model-len 8192` and live proof for current-only/budgeted requests.

- [ ] **Step 1: Extend the live test before changing the server**

Add cases for current text with 49 local-history records, current image, path with source parent, JSON, 26 choices, and a worst-case compact body. Assert the serialized body is at most 10 KiB and returned IDs belong to the sent candidates. Do not print clipboard bodies or credentials.

- [ ] **Step 2: Run against the existing 4096 server**

Run `djev_live_integration` and record whether every compact case passes at 4096. A failure must retain the exact HTTP/status message without exposing headers.

- [ ] **Step 3: Snapshot non-secret deployment state and update context limit**

Record the current container image, Compose project path, `MAX_MODEL_LEN`, `KV_CACHE_GB`, and process command. In both listed `.env` files change only `MAX_MODEL_LEN=8192`; retain the prior value in the ledger for rollback. Never print `API_KEY`.

- [ ] **Step 4: Restart with bounded readiness polling**

Run `docker compose up -d` from `/home/user/dev/djev-spark`. Poll port 8011 and a minimal authenticated SystemOne request at short intervals for up to the documented model startup window; do not use a single blocking sleep over 60 seconds.

- [ ] **Step 5: Verify process and live behavior**

Confirm `ps` shows `--max-model-len 8192`, then run all live integration cases. Capture only endpoint, model alias, candidate counts, payload bytes, selected action ID, and pass/fail.

- [ ] **Step 6: Roll back on readiness or live failure**

If the 8192 deployment fails, restore `MAX_MODEL_LEN=4096`, rerun Compose, prove port 8011 and the compact live test are healthy, and report the resource failure as unresolved rather than leaving Djev down.

- [ ] **Step 7: Document configuration and evidence**

Update README with the compact request behavior, 8192 local setting, rollback command, and the distinction from the hosted Jev context limit. Record Task 8 evidence in the ledger.

### Task 9: Desktop smoke, independent review, and final handoff

**Files:**
- Modify: `README.md`
- Modify: `.sdd/pasteit-current-clipboard-history/progress.md`

**Interfaces:**
- Consumes: the integrated `build-v3/pastit` binary and all prior evidence.
- Produces: verified running application and final review closure.

- [ ] **Step 1: Run fresh full verification**

Run:

```bash
cmake --build build-v3 -j2
ctest --test-dir build-v3 --output-on-failure
./build-v3/djev_live_integration
./build-v3/openai_live_integration
```

Expected: build succeeds, all non-display tests pass, Djev live cases pass, and OpenAI live either passes with configured credentials or exits with the established explicit skip code.

- [ ] **Step 2: Run persistence and file-operation smoke scripts**

With a temporary XDG data directory, seed 52 mixed text/image records, restart the app/store utility, and confirm 50 rows and readable newest image bytes. Execute save/copy/move confirmations into a temporary directory and assert the chosen names and output bytes.

- [ ] **Step 3: Stop only the PasteIt process started by this task**

Resolve the exact PID/session from the task ledger, terminate it normally, and leave unrelated older user processes untouched until the final launch step makes replacement necessary.

- [ ] **Step 4: Run desktop smoke with forced visibility**

Launch with `PASTIT_SHOW_ON_START=1 PASTIT_DIAGNOSTICS=1`. Verify:

- current text shows no historical image/email action;
- Clipboard History shows 50 rows and image detail;
- Prompt Actions shows every enabled template;
- prompt table View/Edit/Duplicate/Delete works;
- Recent Paths columns resize and row detail opens;
- source parent and Browse/manual destinations appear;
- Save/Download/Copy/Move open confirmation and honor edited filenames.

- [ ] **Step 5: Dispatch independent whole-change review**

Provide the reviewer the spec, this plan, ledger, all changed file paths, test output, live Djev output, and Review Focus verbatim. Ask for findings graded Critical/Important/Minor and require exact file/line evidence.

- [ ] **Step 6: Fix Critical and Important findings in one RED/GREEN pass**

For each accepted finding, add a focused failing test, observe RED, implement the minimal fix, observe GREEN, then rerun the full suite and live Djev test. Record deferred Minor findings explicitly.

- [ ] **Step 7: Packaging audit and final launch**

Install to a temporary prefix, run `ldd`, check `readelf -d` for absent RPATH/RUNPATH, and scan platform-neutral source for native-header leakage. Stop the old PasteIt instance that owns Ctrl+Alt+F, launch the verified `build-v3/pastit`, and confirm the shortcut opens the new UI.

- [ ] **Step 8: Complete the ledger and handoff**

Record final test totals, live provider state, server process limit, review closure, running binary PID/session, configuration paths, and any deferred Minor issues. Link the binary, README, spec, plan, and ledger in the final response.
