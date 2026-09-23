# PasteIt MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first runnable PasteIt Linux/X11 MVP with Dear ImGui, a dynamic Djev action protocol, and deterministic executors for text, URL, email, image, path, JSON, and resume fields.

**Architecture:** Keep domain state, content detection, action catalog, Djev protocol, and executors independent from the ImGui shell. At popup time, build concrete `ActionInstance` records, send their IDs/descriptions as a typed Djev `Choice`, rank the returned probabilities, and execute the selected instance locally.

**Tech Stack:** C++20, CMake, Dear ImGui, GLFW, OpenGL3, X11/XFixes/XTest, SQLite or a small local file store, libcurl, and a minimal C++ JSON implementation. Headless unit tests must not require an X11 display or a running Djev server.

**Spec:** `docs/superpowers/specs/2026-09-20-pasteit-decision-actions-design.md`

## Global Constraints

- The first implementation targets text, URL, email, image, local path/file URI, and JSON.
- Djev chooses dynamic action IDs; Djev never returns shell commands.
- PasteIt owns action parameters and execution.
- The local Djev endpoint is `POST {DJEV_URL}/v1/systemone`, defaulting to `http://127.0.0.1:8011/v1/systemone`; model defaults to `DJEV_MODEL=jev-latest`.
- The UI displays no more than five actions sorted by returned probability.
- Recent clipboard paths and the current accessible directory are action targets.
- Valid JSON gets raw and pretty actions; invalid JSON gets raw text actions only.
- Resume fields are direct clipboard actions, not generated prose.
- The current demo does not add password/token filtering or a security policy layer.
- The current workspace is not a Git repository; do not invent commit hashes or block implementation on Git commits.

## Review Focus

- A Djev response naming an ID absent from the request catalog must become an invalid decision without executing anything; test in Task 3.
- A path copied as a file URI must produce its parent directory as a save target; test in Task 2.
- A stale response after clipboard/context change must be discarded; test in Task 3 and Task 6.
- Invalid JSON must not expose pretty-save actions; test in Task 3.
- An image must save original bytes in Task 4 while the UI uses only a thumbnail preview in Task 5.

### Task 1: Project scaffold and domain contracts

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/main.cpp`
- Create: `src/core/types.hpp`
- Create: `src/core/action.hpp`
- Create: `src/core/protocol.hpp`
- Create: `tests/core_contracts_test.cpp`
- Create: `README.md`

**Interfaces:**
- Produces `ContentKind`, `ClipboardItem`, `PathLocation`, `ActionKind`, `ActionInstance`, `DecisionSnapshot`, `DecisionRequest`, `DecisionResponse`, and `ExecutionResult` types for all later tasks.

- [ ] **Step 1: Write failing contract tests**

Create tests that construct a text clipboard item, an image action, a recent path, and a request-local action catalog. Assert that IDs, refs, content kinds, and protocol version are preserved.

- [ ] **Step 2: Run the contract test and verify the expected missing-type failure**

Run:

```bash
cmake -S . -B build -DPASTIT_BUILD_TESTS=ON
cmake --build build --target core_contracts_test
ctest --test-dir build -R core_contracts_test --output-on-failure
```

Expected: configuration or compilation fails because the domain headers and implementation do not yet exist.

- [ ] **Step 3: Add the minimal CMake project and domain headers**

Use C++20, warnings, a `pastit_core` library, a `pastit` executable, and CTest. Keep domain types standard-library-only. Use opaque string refs for blobs, paths, actions, and request IDs.

- [ ] **Step 4: Run the contract test and the full current test suite**

Expected: `core_contracts_test` passes and CTest reports no failed tests.

- [ ] **Step 5: Document local build prerequisites**

Record the GLFW/OpenGL3/ImGui/X11 and optional libcurl development packages in `README.md`, along with `DJEV_URL` and `DJEV_MODEL` configuration.

### Task 2: Clipboard store, path history, and deterministic content detection

**Files:**
- Create: `src/storage/clipboard_store.hpp`
- Create: `src/storage/clipboard_store.cpp`
- Create: `src/storage/path_history.hpp`
- Create: `src/storage/path_history.cpp`
- Create: `src/detect/content_detector.hpp`
- Create: `src/detect/content_detector.cpp`
- Create: `src/detect/resume_detector.hpp`
- Create: `src/detect/resume_detector.cpp`
- Create: `tests/content_detection_test.cpp`

**Interfaces:**
- Consumes the types from Task 1.
- Produces `ClipboardStore::put/read`, `PathHistory::observe/recent`, `detect_content`, and `detect_resume_fields`.

- [ ] **Step 1: Write failing detector tests**

Cover plain text, HTTP URL, email, image MIME, absolute path, `file://` URI, valid JSON, invalid JSON, and a resume-like text containing name/email/phone/skills. Assert that a file URI observes the parent directory.

- [ ] **Step 2: Run the detector tests and verify failure**

Run:

```bash
cmake --build build --target content_detection_test
ctest --test-dir build -R content_detection_test --output-on-failure
```

Expected: tests fail because detector functions do not yet exist.

- [ ] **Step 3: Implement local blob storage**

Store text and binary content under an app data directory selected from `XDG_DATA_HOME` or `~/.local/share/pastit`. Deduplicate by content hash and expose preview metadata without loading full blobs.

- [ ] **Step 4: Implement path history**

Normalize absolute paths and `file://` URIs, observe parent directories for file entries, preserve recency, and return the most recent existing directories first.

- [ ] **Step 5: Implement content and resume detectors**

Use MIME, URI, email, phone, JSON parse, and small text heuristics. Resume fields must retain value and source range. Do not call Djev from these functions.

- [ ] **Step 6: Run all tests and verify green**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all Task 1 and Task 2 tests pass.

### Task 3: Dynamic action catalog and Djev request/response client

**Files:**
- Create: `src/actions/action_catalog.hpp`
- Create: `src/actions/action_catalog.cpp`
- Create: `src/djev/djev_client.hpp`
- Create: `src/djev/djev_client.cpp`
- Create: `src/djev/decision_ranker.hpp`
- Create: `src/djev/decision_ranker.cpp`
- Create: `tests/action_catalog_test.cpp`
- Create: `tests/djev_response_test.cpp`

**Interfaces:**
- Consumes `ClipboardStore`, `PathHistory`, content detector output, and Task 1 protocol types.
- Produces `ActionCatalog build_catalog(const DecisionSnapshot&)`, `DjevClient::decide(const DecisionRequest&) -> DecisionResponse`, and `rank_top_actions(const DecisionResponse&, const ActionCatalog&) -> std::vector<RankedAction>`.

- [ ] **Step 1: Write failing action catalog tests**

Assert that text, image, URL, email, path, valid JSON, and resume fields create multiple concrete actions. Assert that save actions use recent path refs and that action IDs are unique within one snapshot.

- [ ] **Step 2: Write failing Djev response tests**

Test parsing of `answers.best_action.choice`, probability maps, confidence, malformed answers, and an unknown choice ID. Unknown IDs must return an invalid decision and no executable action.

- [ ] **Step 3: Implement dynamic catalog generation**

Generate action instances from capabilities and available refs. Do not hard-code one action per content kind. Keep action descriptions compact and include source/target/representation.

- [ ] **Step 4: Implement the Djev HTTP client**

POST JSON to `{DJEV_URL}/v1/systemone` with `model`, `state`, and a `best_action` Choice whose criteria keys are action IDs. Support optional `DJEV_API_KEY`, request timeout, response parsing, and an offline error result.

- [ ] **Step 5: Implement probability ranking**

Join probabilities to the request-local catalog, discard unknown IDs, sort descending, and return no more than five `RankedAction` values. Preserve the model-selected choice and confidence for diagnostics.

- [ ] **Step 6: Run all tests**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: catalog and response tests pass without requiring a live Djev endpoint.

### Task 4: Deterministic file, clipboard, URL, JSON, path, and email executors

**Files:**
- Create: `src/executor/action_executor.hpp`
- Create: `src/executor/action_executor.cpp`
- Create: `src/executor/file_executor.hpp`
- Create: `src/executor/file_executor.cpp`
- Create: `src/executor/url_executor.hpp`
- Create: `src/executor/url_executor.cpp`
- Create: `src/executor/email_executor.hpp`
- Create: `src/executor/email_executor.cpp`
- Create: `tests/executor_test.cpp`

**Interfaces:**
- Consumes `ActionInstance`, `ClipboardStore`, and `PathHistory`.
- Produces `ExecutionResult execute_action(const ActionInstance&, ExecutionContext&)`.

- [ ] **Step 1: Write failing executor tests**

Test saving text, saving original image bytes, pretty-saving valid JSON, rejecting pretty JSON for invalid input, copying/moving a local path into a recent directory, writing a `.eml` representation, and returning an explicit unsupported result for an unconfigured direct-send adapter.

- [ ] **Step 2: Run the executor tests and verify failure**

Expected: tests fail because executor implementations do not yet exist.

- [ ] **Step 3: Implement local file executors**

Use `std::filesystem` and streams, not shell command strings. Generate deterministic filenames from content kind and timestamp. Update `PathHistory` and clipboard history after successful writes.

- [ ] **Step 4: Implement URL and email adapters**

Use a libcurl-backed download adapter when available. Provide `open_url` through the desktop launcher abstraction. Save email as `.eml`; provide compose through a `mailto:`/desktop-mail adapter and keep direct send behind an explicit configured adapter interface.

- [ ] **Step 5: Implement path and JSON actions**

Use `copy`, `copy_file`, `rename`, and `create_directories` through `std::filesystem`. Pretty output uses two-space indentation and a terminal newline.

- [ ] **Step 6: Run the full headless suite**

Expected: every executor test and all earlier tests pass; no test requires X11, ImGui, network, or Djev.

### Task 5: X11 clipboard watcher and Dear ImGui popup

**Files:**
- Create: `src/platform/x11_clipboard.hpp`
- Create: `src/platform/x11_clipboard.cpp`
- Create: `src/platform/x11_context.hpp`
- Create: `src/platform/x11_context.cpp`
- Create: `src/ui/popup.hpp`
- Create: `src/ui/popup.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/ui_catalog_smoke_test.cpp`

**Interfaces:**
- Consumes action catalog, Djev client/ranker, and executor registry.
- Produces a runnable `pastit` executable with X11 clipboard observation, `Ctrl+Alt+F`, an ImGui popup, keyboard selection, and click execution.

- [ ] **Step 1: Write a headless UI/catalog smoke test**

Assert that a snapshot with one text item produces five-or-fewer render rows with stable action IDs and labels. The test must not create a window.

- [ ] **Step 2: Integrate Dear ImGui with GLFW/OpenGL3**

Create a hidden/normal event loop, configure an undecorated popup window, and keep rendering separate from action execution. The application must still compile when no live Djev endpoint exists.

- [ ] **Step 3: Add X11 clipboard and shortcut integration**

Observe CLIPBOARD ownership/changes and convert supported MIME data into `ClipboardStore` entries. Register `Ctrl+Alt+F`, capture the current target snapshot before showing the popup, and restore target context after execution where the platform permits.

- [ ] **Step 4: Render the ranked action popup**

Display abbreviated text, image thumbnails, target path, probability, and execution status. Enter/click selects the exact request-local `ActionInstance`; Escape closes without execution.

- [ ] **Step 5: Run compile and headless tests**

Expected: the executable builds, UI smoke tests pass, and all headless tests remain green. If system development packages are unavailable, report the exact missing package and keep the domain/executor test targets buildable.

### Task 6: End-to-end fixtures, README, and verification

**Files:**
- Create: `tests/fixtures/clipboard_text.txt`
- Create: `tests/fixtures/sample_resume.txt`
- Create: `tests/fixtures/sample.json`
- Create: `tests/fixtures/sample.png`
- Create: `tests/protocol_fixture_test.cpp`
- Modify: `README.md`

**Interfaces:**
- Consumes every public interface from Tasks 1–5.
- Produces repeatable offline fixtures and an operator guide for running with local Djev.

- [ ] **Step 1: Write failing protocol fixture tests**

Test that the same snapshot and fixture catalog produces stable action IDs, that the ranked top five are stable for a fixed response, and that an altered clipboard hash makes the response stale.

- [ ] **Step 2: Implement fixtures and tests**

Use a fake Djev response fixture; do not require the live model in ordinary CTest.

- [ ] **Step 3: Document run commands**

Document build, test, local Djev environment variables, popup shortcut, and the six supported content families. Clearly distinguish compose email from direct sending when no mail adapter is configured.

- [ ] **Step 4: Run final verification**

Run:

```bash
cmake -S . -B build -DPASTIT_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all available tests pass. If UI dependencies prevent the desktop target from building, the report must identify the missing package and show the passing headless targets.
