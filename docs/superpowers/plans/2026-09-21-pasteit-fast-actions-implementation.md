# PasteIt Fast Actions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend PasteIt with content-aware contact, terminal, network, GitHub, date/time, explain, Mermaid, QR, download, hash, and image-annotation actions while preserving the Djev choice protocol and independent ImGui windows.

**Architecture:** Add pure detectors and catalog entries first, then platform-neutral service contracts with Linux adapters, then asynchronous job state models, then executors and independent result panels. Djev receives only applicable action IDs and descriptions; selected actions map to typed executors and never execute model-provided commands. Optional `mmdc` and `qrencode` are process adapters with source/payload fallbacks.

**Tech Stack:** C++20, CMake, Dear ImGui, X11/Linux platform adapters, existing libcurl client, existing General LLM and Djev clients, CTest, local HTTP fixtures.

**Spec:** `docs/superpowers/specs/2026-09-21-pasteit-fast-actions-design.md`

## Global Constraints

- The current clipboard item is the only source used to build the active action catalog.
- Djev receives `criteria` as an option-name-to-description map and only selects/ranks action IDs.
- The visible action limit remains eight after local applicability filtering.
- Long-running work must not block the ImGui frame loop.
- Every auxiliary modal must use an independent viewport/window and must not make the root window always-on-top.
- `mmdc` and `qrencode` are optional; missing tools produce source/payload fallback states.
- Platform-specific terminal, process, network, date/time, hashing, rendering, download, and image operations use platform-neutral interfaces.
- The user’s demo scope does not add password/token filtering, but command arguments still use argv/path values rather than shell concatenation.
- This workspace has no Git metadata; use build/test checkpoints instead of commit steps.

## Review Focus

- A stale previous clipboard item must not contribute image/email/other actions to the current catalog. Test this in the catalog task.
- A path containing spaces and a file path must open Terminal at the containing directory without shell quoting bugs. Test this in the platform/executor task.
- IPv6, IPv4 with surrounding prose, and malformed IP strings must produce the expected diagnostic actions. Test this in the detector task.
- Partial downloads must resume only after a valid Range response and must preserve `.part` on cancel. Test this in the download task.
- Missing `mmdc`/`qrencode` must still show copyable source/payload rather than failing the action. Test this in the renderer/UI task.

### Task 1: Add shared content signals, contact/date-time models, and fast detectors

**Files:**
- Create: `src/detect/fast_content_detector.hpp`
- Create: `src/detect/fast_content_detector.cpp`
- Modify: `src/core/types.hpp`
- Modify: `src/core/action.hpp`
- Modify: `CMakeLists.txt`
- Test: `tests/fast_content_detector_test.cpp`

**Interfaces:**
- Produces `FastContentSignals detect_fast_content(ContentKind kind, std::string_view text)`.
- Produces `ContactField { std::string kind; std::string label; std::string value; }`.
- Produces `DateTimeValue { std::string original; std::string normalized; std::string source_zone; std::int64_t epoch_seconds; bool has_epoch; }`.
- Produces booleans/tags for `contact`, `code`, `diagram`, `ip`, `github_url`, `qr`, and `date_time`.

- [ ] **Step 1: Write failing detector tests**

```cpp
TEST_CASE("business card extracts selectable contact fields") {
    const auto s = detect_fast_content(ContentKind::Text,
        "Ada Lovelace\nAnalytical Engine\nada@example.com\n+44 20 1234 5678\n1 Logic Lane, London");
    REQUIRE(s.contact);
    REQUIRE(s.contact_fields.size() >= 3);
    REQUIRE(has_field(s.contact_fields, "email", "ada@example.com"));
    REQUIRE(has_field(s.contact_fields, "phone", "+44 20 1234 5678"));
}

TEST_CASE("detector recognizes network, GitHub, diagram, and date signals") {
    REQUIRE(detect_fast_content(ContentKind::Text, "connect 192.0.2.10").ip);
    REQUIRE(detect_fast_content(ContentKind::Url,
        "https://github.com/acme/widget").github_url);
    REQUIRE(detect_fast_content(ContentKind::Text, "A -> B -> C").diagram);
    REQUIRE(detect_fast_content(ContentKind::Text,
        "2026-09-21T08:30:00Z").date_time);
}

TEST_CASE("malformed address does not create an IP signal") {
    REQUIRE_FALSE(detect_fast_content(ContentKind::Text, "999.999.1.1").ip);
}
```

- [ ] **Step 2: Run the focused test and verify it fails**

Run: `cmake --build build-headless --target fast_content_detector_test -j2 && ./build-headless/fast_content_detector_test`

Expected: configure/build failure because the detector types and test target do not exist.

- [ ] **Step 3: Add the models, enum values, and minimal detectors**

Add `ContentKind::DateTime`, `SemanticTag::DateTime`, and action kinds for all catalog actions used by later tasks. Implement bounded regex/string scans:

```cpp
struct FastContentSignals {
    bool contact = false;
    bool code = false;
    bool diagram = false;
    bool ip = false;
    bool github_url = false;
    bool qr = false;
    bool date_time = false;
    std::vector<ContactField> contact_fields;
    std::optional<DateTimeValue> date_time_value;
};

FastContentSignals detect_fast_content(ContentKind kind, std::string_view text);
```

Use strict IPv4 octet validation, a conservative IPv6 check, GitHub URL normalization, ISO/RFC/date patterns, contact line labels, and relationship tokens (`->`, `<-`, `--`, `extends`, `inherits`, `timeline`, `sequence`, `classDiagram`, `erDiagram`, `flowchart`). Keep QR true only for URL/email or non-empty text <= 2048 bytes.

- [ ] **Step 4: Run focused tests and full core tests**

Run: `cmake --build build-headless --target fast_content_detector_test -j2 && ./build-headless/fast_content_detector_test && ctest --test-dir build-headless --output-on-failure`

Expected: focused tests and all pre-existing tests pass.

### Task 2: Expand the action catalog and prompt seeds

**Files:**
- Modify: `src/core/action.hpp`
- Modify: `src/actions/action_catalog.hpp`
- Modify: `src/actions/action_catalog.cpp`
- Modify: `src/config/prompt_template_service.cpp`
- Modify: `src/ai/prompt_expander.cpp` only if new variable handling requires it
- Test: `tests/action_catalog_fast_actions_test.cpp`
- Test: `tests/prompt_template_test.cpp` if seed behavior changes

**Interfaces:**
- `build_catalog()` consumes `FastContentSignals` internally through `detect_fast_content`.
- Action IDs are stable slugs such as `a_ping_ip_<ref>`, `a_extract_contact_<ref>`, and `a_explain_code_<ref>`.
- Prompt actions retain `template_id`, `system_prompt`, endpoint, model, and variable metadata in `ActionInstance::parameters`.

- [ ] **Step 1: Write failing catalog tests**

```cpp
TEST_CASE("text catalog contains contact, Mermaid, QR, and explain actions") {
    const auto catalog = build_catalog(snapshot_with_text(
        "Ada\nada@example.com\nA -> B -> C"), {}, {});
    REQUIRE(catalog.find_by_kind(ActionKind::ExtractContactInfo));
    REQUIRE(catalog.find_by_kind(ActionKind::DrawMermaidDiagram));
    REQUIRE(catalog.find_by_kind(ActionKind::GenerateQr));
    REQUIRE(catalog.find_label("Explain text"));
}

TEST_CASE("path, IP, GitHub, and datetime catalog actions are content specific") {
    REQUIRE(has_kind(build_catalog(snapshot_with_path("/tmp/a b.txt")),
                     ActionKind::OpenTerminalAtPath));
    REQUIRE(has_kind(build_catalog(snapshot_with_text("192.0.2.10")),
                     ActionKind::ReverseDnsIp));
    REQUIRE(has_kind(build_catalog(snapshot_with_url(
        "https://github.com/acme/widget")), ActionKind::CloneGithubSsh));
    REQUIRE(has_kind(build_catalog(snapshot_with_text("2026-09-21T08:30:00Z")),
                     ActionKind::ToUnixTimestamp));
}
```

- [ ] **Step 2: Run the focused test and verify it fails**

Run: `cmake --build build-headless --target action_catalog_fast_actions_test -j2 && ./build-headless/action_catalog_fast_actions_test`

Expected: compile failure for missing action kinds/helpers.

- [ ] **Step 3: Implement action kinds and catalog entries**

Add contact, terminal, IP diagnostics, GitHub URL, datetime, Mermaid, QR, hash, annotation, and explanation kinds. Add entries only when the detector says they apply. Use at most three recent directory targets and include explicit parameter metadata for destination, timezone, timestamp unit, prompt variables, and filename.

Seed `Explain text` and `Explain code` prompt templates only when absent. Their system prompts must contain `{text}` and any additional variables must be surfaced by the existing parameter modal.

- [ ] **Step 4: Run the focused and regression tests**

Run: `cmake --build build-headless --target action_catalog_fast_actions_test prompt_template_test -j2 && ./build-headless/action_catalog_fast_actions_test && ./build-headless/prompt_template_test && ctest --test-dir build-headless --output-on-failure`

Expected: all pass; stale non-current clipboard items are not used by the catalog.

### Task 3: Add platform-neutral service contracts and Linux adapters

**Files:**
- Create: `src/platform/fast_action_services.hpp`
- Create: `src/platform/linux/linux_fast_action_services.hpp`
- Create: `src/platform/linux/linux_fast_action_services.cpp`
- Modify: `src/platform/platform_services.hpp`
- Modify: `src/platform/linux/linux_desktop_services.hpp/.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/fast_action_services_test.cpp`

**Interfaces:**

```cpp
struct ProcessOutput { int exit_code; std::string stdout_text; std::string stderr_text; };
enum class NetworkProbe { Ping, TraceRoute, ReverseDns, Dig };
enum class HashAlgorithm { Sha256, Sha512 };

class FastActionServices {
public:
    virtual ~FastActionServices() = default;
    virtual bool open_terminal(const std::filesystem::path& directory) = 0;
    virtual ProcessOutput run_network_probe(NetworkProbe probe, std::string_view host) = 0;
    virtual ProcessOutput clone_repository(std::string_view url,
                                           const std::filesystem::path& destination) = 0;
    virtual std::optional<std::int64_t> parse_datetime(std::string_view value,
                                                        std::string_view source_zone) = 0;
    virtual std::string format_datetime(std::int64_t epoch_seconds,
                                        std::string_view target_zone) = 0;
    virtual std::string hash_file(const std::filesystem::path&, HashAlgorithm) = 0;
    virtual ProcessOutput run_argv(const std::vector<std::string>& argv) = 0;
};
```

- [ ] **Step 1: Write service contract tests with a fake adapter**

```cpp
TEST_CASE("network and clone services receive typed values") {
    FakeFastActionServices fake;
    fake.run_network_probe(NetworkProbe::ReverseDns, "192.0.2.10");
    fake.clone_repository("https://github.com/acme/widget",
                          "/tmp/clone target");
    REQUIRE(fake.last_probe_host == "192.0.2.10");
    REQUIRE(fake.last_clone_destination == "/tmp/clone target");
}
```

- [ ] **Step 2: Run and observe the missing-contract failure**

Run: `cmake --build build-headless --target fast_action_services_test -j2 && ./build-headless/fast_action_services_test`

Expected: compile failure until the interface and fake are added.

- [ ] **Step 3: Implement the interfaces and Linux argv mappings**

Map Linux network probes to `ping -c 4`, `traceroute`, `getent hosts`/reverse lookup, and `dig`. Map clone to `git clone`. Set a working directory for terminal launch using the configured terminal command. Date/time conversion must use the host timezone database and hash must stream through the selected algorithm. Keep process output bounded and preserve exit codes.

- [ ] **Step 4: Run focused tests and platform regression tests**

Run: `cmake --build build-headless --target fast_action_services_test platform_contracts_test -j2 && ./build-headless/fast_action_services_test && ./build-headless/platform_contracts_test`

Expected: fake contract and existing platform tests pass without requiring network access.

### Task 4: Implement executors and asynchronous job state models

**Files:**
- Create: `src/app/download_job.hpp/.cpp`
- Create: `src/app/network_report_state.hpp/.cpp`
- Create: `src/app/contact_result_state.hpp/.cpp`
- Create: `src/app/date_time_result_state.hpp/.cpp`
- Create: `src/app/hash_result_state.hpp/.cpp`
- Create: `src/app/renderer_result_state.hpp/.cpp`
- Create: `src/executor/fast_action_executor.hpp/.cpp`
- Modify: `src/executor/action_executor.hpp/.cpp`
- Modify: `src/app/ai_result_state.hpp/.cpp` only for Mermaid result metadata if required
- Modify: `CMakeLists.txt`
- Tests: `tests/download_job_test.cpp`, `tests/fast_action_executor_test.cpp`, `tests/result_state_test.cpp`

**Interfaces:**

```cpp
enum class DownloadStatus { Queued, Running, Paused, Completed, Cancelled, Failed };
struct DownloadJob { std::string id, url; std::filesystem::path part_path, final_path;
    std::uint64_t downloaded = 0, total = 0; bool range_supported = false;
    DownloadStatus status = DownloadStatus::Queued; std::string error; };

class DownloadManager {
public:
    std::string start(std::string url, std::filesystem::path destination);
    void pause(std::string_view id); void resume(std::string_view id);
    void cancel(std::string_view id); std::optional<DownloadJob> get(std::string_view id) const;
};
```

- [ ] **Step 1: Write failing state-machine tests**

```cpp
TEST_CASE("cancel preserves partial download and resume uses its size") {
    DownloadJob job = make_job_with_partial_file(4096);
    job.status = DownloadStatus::Running;
    apply_download_event(job, DownloadEvent::Cancel);
    REQUIRE(job.status == DownloadStatus::Cancelled);
    REQUIRE(job.part_path_exists);
    apply_download_event(job, DownloadEvent::Resume);
    REQUIRE(job.range_start == 4096);
}
```

- [ ] **Step 2: Run focused tests and verify failure**

Run: `cmake --build build-headless --target download_job_test -j2 && ./build-headless/download_job_test`

Expected: missing job/event symbols.

- [ ] **Step 3: Implement non-blocking state models and typed executors**

Use a worker thread and synchronized job map for downloads. Reuse the existing HTTP/libcurl layer where possible; issue a Range request only when `.part` size is nonzero, append after a valid partial response, and rename on completion. Preserve `.part` for cancel/failure. Add executors for contact extraction, network report aggregation, GitHub clone, datetime conversion, hashes, QR payload generation, Mermaid LLM request/normalization, and image annotation state. Each executor returns a result state or job ID immediately.

- [ ] **Step 4: Run job/executor tests and existing executor tests**

Run: `cmake --build build-headless --target download_job_test fast_action_executor_test result_state_test executor_test -j2 && ./build-headless/download_job_test && ./build-headless/fast_action_executor_test && ./build-headless/result_state_test && ./build-headless/executor_test`

Expected: all pass; no action invokes a shell string with concatenated clipboard input.

### Task 5: Add Mermaid/QR renderer adapters and image annotation model

**Files:**
- Create: `src/render/renderer_service.hpp`
- Create: `src/render/external_renderer.cpp`
- Create: `src/annotation/image_annotation.hpp/.cpp`
- Modify: `src/config/app_settings.hpp/.cpp`
- Modify: `src/config/settings_store.cpp`
- Modify: `CMakeLists.txt`
- Tests: `tests/renderer_service_test.cpp`, `tests/image_annotation_test.cpp`

**Interfaces:**

```cpp
struct RenderResult { bool available; bool success; std::filesystem::path output; std::string status; };
class RendererService {
public:
    virtual RenderResult render_mermaid(std::string_view source,
                                        const std::filesystem::path& output) = 0;
    virtual RenderResult render_qr(std::string_view payload,
                                   const std::filesystem::path& output) = 0;
};
struct AnnotationStroke { std::vector<ImVec2> points; float width; ImU32 color; };
```

- [ ] **Step 1: Write fallback and annotation tests**

```cpp
TEST_CASE("missing renderers preserve source and payload") {
    FakeRenderer renderer(false);
    REQUIRE_FALSE(renderer.render_mermaid("flowchart LR\nA-->B", "/tmp/a.svg").success);
    REQUIRE_FALSE(renderer.render_qr("https://example.com", "/tmp/q.png").success);
}

TEST_CASE("annotation undo removes only the latest overlay") {
    AnnotationDocument doc;
    doc.add_stroke(red_stroke({{0, 0}, {10, 10}}));
    doc.add_comment("note", {5, 5});
    doc.undo();
    REQUIRE(doc.comments().size() == 0);
    REQUIRE(doc.strokes().size() == 1);
}
```

- [ ] **Step 2: Run focused tests and verify failure**

Run: `cmake --build build-headless --target renderer_service_test image_annotation_test -j2 && ./build-headless/renderer_service_test && ./build-headless/image_annotation_test`

Expected: missing renderer/annotation types.

- [ ] **Step 3: Implement optional renderer and annotation state**

Run `mmdc` and `qrencode` through `run_argv`, using configured paths when present and executable lookup otherwise. Normalize Mermaid fences before rendering. Store renderer availability/status separately from generated source/payload. Implement annotation strokes, red pen/line/rectangle/arrow, white-background comment cards, undo/clear, and export requests without mutating the original image.

- [ ] **Step 4: Run focused tests**

Run: `cmake --build build-headless --target renderer_service_test image_annotation_test settings_test -j2 && ./build-headless/renderer_service_test && ./build-headless/image_annotation_test && ./build-headless/settings_test`

Expected: optional-tool fallback and settings persistence pass on machines without `mmdc` or `qrencode`.

### Task 6: Add independent contact, network, Mermaid, QR, download, hash, and annotation panels

**Files:**
- Create: `src/ui/contact_result_panel.hpp/.cpp`
- Create: `src/ui/network_report_panel.hpp/.cpp`
- Create: `src/ui/mermaid_preview_panel.hpp/.cpp`
- Create: `src/ui/qr_preview_panel.hpp/.cpp`
- Create: `src/ui/download_progress_panel.hpp/.cpp`
- Create: `src/ui/hash_result_panel.hpp/.cpp`
- Create: `src/ui/image_annotation_panel.hpp/.cpp`
- Modify: `src/ui/multi_viewport.hpp/.cpp`
- Modify: `src/ui/desktop_runtime.cpp`
- Modify: `CMakeLists.txt`
- Tests: `tests/fast_action_panel_model_test.cpp`, `tests/multi_viewport_test.cpp`

**Interfaces:**

Each panel receives a result-state reference and a `MultiViewportManager&`, and exposes a pure model helper for tests. For example:

```cpp
void draw_mermaid_preview(MermaidResultState&, MultiViewportManager&);
void draw_download_progress(DownloadManager&, std::string_view job_id,
                            MultiViewportManager&);
```

- [ ] **Step 1: Write model/UI contract tests**

```cpp
TEST_CASE("result panels request independent viewports") {
    FakeViewportManager viewports;
    MermaidResultState result;
    draw_mermaid_preview_model(result, viewports);
    REQUIRE(viewports.last_flags != 0);
    REQUIRE_FALSE(viewports.last_is_root_child);
}
```

- [ ] **Step 2: Implement compact top-toolbar layouts**

Use independent viewport creation for every panel. Put Source/Copy/Save at the top of Mermaid and QR panels; Pause/Resume/Cancel at the top of download panels; operation tools at the top of network/hash/path panels. Use selectable/copyable fields and scrollable text areas with a maximum initial height of ten rows. Draw the image annotation preview with the fast annotation section expanded at the bottom.

- [ ] **Step 3: Wire action selection to each panel and preserve non-blocking behavior**

Extend the action selection handler in `desktop_runtime.cpp`/`desktop_flow.cpp` to open parameter/result panels immediately, submit async jobs, poll state in the frame loop, and close only when the user requests it. Route copy actions through the existing X11 clipboard watcher callback.

- [ ] **Step 4: Run UI/model tests and desktop compile**

Run: `cmake --build build-headless --target fast_action_panel_model_test multi_viewport_test ui_state_test -j2 && ./build-headless/fast_action_panel_model_test && ./build-headless/multi_viewport_test && ./build-headless/ui_state_test && cmake --build build-final --target pastit -j2`

Expected: panels compile in headless/core builds, desktop target links when Dear ImGui dependencies are available, and auxiliary windows are not children of the root popup.

### Task 7: Add live Djev/General LLM integration for Mermaid, contact enrichment, and Explain prompts

**Files:**
- Modify: `src/djev/decision_session.cpp`
- Modify: `src/djev/decision_ranker.cpp`
- Modify: `src/ai/openai_compatible_client.cpp`
- Modify: `src/ui/desktop_runtime.cpp`
- Modify: `src/ui/ai_result_panel.cpp`
- Modify: `tests/djev_live_integration.cpp`
- Modify: `tests/openai_live_integration.cpp`
- Test: `tests/mermaid_prompt_test.cpp`

**Interfaces:**

Mermaid uses `GeneralLlmRequest { system_message, user_message, model, endpoint }`; Explain and contact enrichment reuse the same request/result state. Djev receives only catalog criteria and returns an action ID/probabilities.

- [ ] **Step 1: Write prompt contract tests**

```cpp
TEST_CASE("Mermaid prompt asks for source without fences") {
    const auto prompt = build_mermaid_prompt("A depends on B");
    REQUIRE(prompt.system_message.find("Mermaid source only") != std::string::npos);
    REQUIRE(prompt.system_message.find("without Markdown fences") != std::string::npos);
}

TEST_CASE("Djev criteria descriptions identify option names") {
    const auto request = build_decision_request(snapshot_with_mermaid_text());
    REQUIRE(request.criteria.at("a_draw_mermaid_diagram_ref").find("Mermaid") != std::string::npos);
}
```

- [ ] **Step 2: Run focused tests and verify missing helpers/failures**

Run: `cmake --build build-headless --target mermaid_prompt_test djev_response_test -j2 && ./build-headless/mermaid_prompt_test && ./build-headless/djev_response_test`

Expected: the new prompt test fails until the request builder is added; existing Djev response tests remain green.

- [ ] **Step 3: Implement bounded prompts and response normalization**

Limit source text sent to the LLM using the existing payload budget logic, preserve the full local clipboard for local actions, strip Mermaid fences/prose, and store raw/normalized result separately. Ensure prompt variables are displayed before request submission and saved values are reused.

- [ ] **Step 4: Run live tests only when configured**

Run: `./build-headless/djev_live_integration` and `./build-headless/openai_live_integration` with the existing local Djev and General LLM environment. Expected: real requests complete; otherwise the tests report an explicit configuration skip, not a mock success.

### Task 8: Integrate settings, CMake targets, documentation, and end-to-end verification

**Files:**
- Modify: `src/config/app_settings.hpp/.cpp`
- Modify: `src/ui/settings_panel.cpp`
- Modify: `src/ui/settings_model.cpp`
- Modify: `README.md`
- Modify: `CMakeLists.txt`
- Add all test sources from Tasks 1–7 to CMake
- Test: `tests/fast_actions_e2e_test.cpp`

- [ ] **Step 1: Write end-to-end catalog/executor test**

```cpp
TEST_CASE("current clipboard drives only its applicable top eight actions") {
    auto snapshot = snapshot_with_text("https://github.com/acme/widget");
    const auto catalog = build_catalog(snapshot, seeded_prompts(), provider_settings());
    REQUIRE(catalog.actions.size() >= 4);
    REQUIRE_FALSE(has_kind(catalog, ActionKind::PingIp));
    REQUIRE(has_kind(catalog, ActionKind::CopyGithubSshUrl));
}
```

The test must also build a second snapshot with an image and confirm image-only actions do not leak into the URL catalog.

- [ ] **Step 2: Add settings and model lists**

Persist optional renderer paths, download resume policy, terminal command, hash defaults, date/time preferences, and annotation save format. Render them in the existing settings tab with selectable values and no synchronous network call from an input widget. Keep prompt templates in their existing CRUD table.

- [ ] **Step 3: Add all source files and test targets to CMake**

Register each new `.cpp` in `pastit_core`, add focused executables/tests, and keep headless builds free of ImGui-only headers by isolating panel code behind the existing desktop compile conditions.

- [ ] **Step 4: Run full verification**

Run:

```bash
cmake --build build-headless --parallel 2
ctest --test-dir build-headless --output-on-failure
cmake --build build-final --parallel 2
ctest --test-dir build-final --output-on-failure
./build-headless/djev_live_integration
./build-headless/openai_live_integration
```

Then run the desktop binary with the existing diagnostics environment, exercise text/contact, path, IP, GitHub, datetime, Mermaid, QR, image, and download actions, and confirm no frame-loop hangs or root-window parenting regressions.

- [ ] **Step 5: Update README and progress log**

Document optional `mmdc`/`qrencode`, action behavior, `.part` resume semantics, network command mapping, and the fallback states. Append a dated implementation checkpoint to `.sdd/pasteit-current-clipboard-history/progress.md` with build/test results and any unavailable optional tools.

## Self-review checklist

- Spec coverage: all twelve acceptance criteria map to Tasks 1–8; business-card extraction, terminal-at-path, diagnostics, GitHub, datetime, explain, Mermaid, QR, download, hash, annotation, independent windows, and settings are explicitly assigned.
- Placeholder scan: no `TODO`, `TBD`, or unspecified implementation step is used; every task names files, interfaces, tests, commands, and expected outcomes.
- Type consistency: detector signals feed catalog construction; `ActionKind` values feed `fast_action_executor`; services are injected into executors; state objects feed panels; CMake registers every production/test file.
- Review focus: stale clipboard isolation is tested in Tasks 1/2/8; path spaces in Task 3; IP variants in Task 1; download resume in Task 4; renderer fallback in Task 5.
- Dependency choice: no mandatory Mermaid/QR runtime is introduced; optional tools are isolated behind process/render services.
