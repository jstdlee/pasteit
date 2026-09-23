# PasteIt Decision Actions Design

Date: 2026-09-20
Status: Proposed for review

## 1. Purpose

PasteIt is a local Linux desktop utility, rendered with Dear ImGui, that opens with `Ctrl+Alt+F`, observes the current clipboard and clipboard history, collects lightweight context about the focused application, and asks a locally deployed djev endpoint to rank executable actions.

The first implementation targets six content families:

- text;
- URL;
- email;
- image;
- local path/file URI;
- JSON.

The model chooses among a dynamically generated set of action instances. PasteIt owns action parameters and execution. Djev never returns shell commands and never performs filesystem or desktop actions itself.

## 2. Goals

1. Show a small popup with the five highest-probability actions for the current clipboard/context snapshot.
2. Save text and image clipboard data to files.
3. Use recent clipboard paths and the current accessed/open directory as save/copy/move targets.
4. Support URL paste, open, download, and save-to-file actions.
5. Support email paste, email-file saving, contact-like structured presentation, and a configurable send/compose adapter.
6. Support path move and copy actions.
7. Support JSON raw save and pretty-formatted output.
8. Detect likely resume content and present extracted fields as one-click copy items.
9. Keep execution deterministic after the user selects an action.
10. Record enough request/action/result metadata to reproduce a local demo decision.

## 3. Non-goals for the first implementation

- Wayland parity beyond a best-effort popup and clipboard fallback.
- Git clone/push integration; this remains a later action family.
- OCR or screenshot-based context extraction.
- A general-purpose natural-language command executor.
- Password/token/secret classification or filtering. This is a local personal demo as requested.
- A server-side agent or remote action service.

## 4. System architecture

```text
X11 clipboard watcher + path history
          |
          v
ClipboardStore + ContentDetectors + ContextCollector
          |
          v
ActionCatalogBuilder
          |
          v
DecisionProtocolClient ---- POST {DJEV_URL}/v1/systemone
          |
          v
Choice probabilities -> top-five ranking -> ImGui popup
          |
          v
ActionExecutorRegistry -> ExecutionResult -> clipboard/history update
```

Dear ImGui is the presentation layer. The action catalog and executor registry are independent from the UI and Djev client so they can be tested without a desktop session.

## 5. Decision subject and action model

The unit sent to Djev is an `ActionInstance`:

```cpp
struct ActionInstance {
    std::string id;              // valid only for this request snapshot
    ActionKind kind;
    std::string source_ref;      // clip_42 or path_03
    std::string target_ref;      // path_01, temp, focused_target
    std::string filename;        // optional local filename
    std::string representation;  // raw, pretty, normalized, vcard, etc.
    std::string label;           // UI label
    std::string description;     // Choice criterion shown to Djev
    bool enabled;
};
```

The first `ActionKind` set is:

```text
paste_text
save_text_file
paste_image
save_image_file
paste_url
open_url
download_url
save_url_file
paste_email
save_email_file
compose_email
send_email
copy_path
move_path
copy_path_to_directory
pretty_json
save_json_file
save_json_pretty_file
copy_resume_field
copy_resume_as_json
save_resume_file
```

The content detector is allowed to generate several actions for the same content. There is no fixed one-to-one mapping such as `url -> open_url`.

## 6. Clipboard and history state

Every captured clipboard item has a stable local reference and a content blob held by `ClipboardStore`.

```cpp
struct ClipboardItem {
    std::string ref;
    std::vector<std::string> mime_types;
    ContentKind kind;
    std::filesystem::path blob_path;
    std::string preview;
    uint64_t size_bytes;
    int64_t captured_at_ms;
    std::string source_app;
    std::vector<SemanticTag> tags;
};
```

The UI receives shortened previews. Full text and original image bytes are loaded only by the selected executor or an explicit preview expansion.

## 7. Recent path collection

`PathHistory` is separate from general clipboard history. It records locations inferred from:

1. directory clipboard entries;
2. file URI clipboard entries, using the parent directory;
3. paths copied from a file manager or terminal;
4. the currently focused/open directory when it can be read;
5. directories created by PasteIt itself.

```cpp
struct PathLocation {
    std::string ref;
    std::filesystem::path path;
    PathKind kind; // file or directory
    int64_t last_seen_ms;
    std::string source;
    bool exists;
};
```

For each popup, the catalog builder selects the most recent relevant locations and creates concrete actions such as:

```text
save_text_file(clip_42, path_01)
save_image_file(clip_42, path_01)
copy_path_to_directory(path_03, path_01)
move_path(path_03, path_01)
```

The first version should cap path candidates per action family so the Djev Choice remains compact. The cap is a local implementation constant, not a semantic content/action rule.

## 8. Content detectors

Detectors are deterministic and run before the Djev request.

### Text

Generate paste, save, and optional structured-text actions. The original text remains available through `source_ref`.

### URL

Parse absolute HTTP(S), `mailto:`, and common file/download URLs. Generate paste, open, download, and save actions when a target path exists.

Download writes the response bytes to a locally generated filename. Save URL file stores the URL text itself as `.url` or `.txt`, based on the selected representation.

### Email

Recognize standalone email addresses, `mailto:` URLs, and email-like fields in larger text. Generate paste, save, compose, and send actions. The default desktop adapter opens a compose target; direct sending is enabled only when a local mail adapter is configured.

### Image

Recognize image MIME types and retain the original bytes. Generate paste-image, save-image, and temp-file actions. The UI uses a thumbnail; the executor writes the original format when supported.

### Path

Recognize absolute paths, `file://` URIs, and URI lists. Generate copy, move, copy-to-directory, and copy-path actions using `PathHistory` targets.

### JSON

Parse JSON locally. Generate raw save, pretty save, raw paste, and pretty output actions. Invalid JSON remains eligible for raw text actions but does not generate pretty actions.

## 9. Resume detection and structured presentation

Resume extraction is a separate semantic layer over text. It produces a local structure:

```json
{
  "is_resume": true,
  "fields": [
    {"kind": "name", "value": "Jane Doe", "source_range": [0, 8]},
    {"kind": "email", "value": "jane@example.com", "source_range": [42, 58]},
    {"kind": "phone", "value": "+65 9123 4567", "source_range": [60, 73]},
    {"kind": "skills", "value": "C++, Linux, SQLite", "source_range": [210, 228]}
  ]
}
```

Local heuristics identify obvious fields such as email, phone, URLs, headings, and section boundaries. Djev receives a compact description and can choose whether the resume representation is relevant to the focused target.

The popup presents each field as a direct action:

```text
Copy name
Copy email
Copy phone
Copy skills
Copy all extracted fields as JSON
Save structured resume as JSON
```

Each field action copies the exact or normalized field value directly to the clipboard.

## 10. Decision protocol

The application-level protocol wraps the local Djev API.

### Request

```json
{
  "protocol_version": 1,
  "request_id": "req_184",
  "snapshot": {
    "clipboard_hash": "...",
    "focused_target_hash": "...",
    "captured_at_ms": 1726800000123
  },
  "state": {
    "target": {},
    "clipboard": {},
    "history": [],
    "recent_paths": [],
    "available_actions": []
  },
  "questions": {
    "best_action": {
      "type": "choice",
      "instructions": "Choose the most useful action from available_actions.",
      "criteria": {}
    }
  }
}
```

The Djev HTTP payload uses the local deployment shape used by the demo: configurable `DJEV_URL`, default model identifier, structured `state`, and typed `questions` sent to `/v1/systemone`.

### Response

```json
{
  "request_id": "req_184",
  "choice": "a_save_image_path_01",
  "confidence": 0.86,
  "probabilities": {
    "a_save_image_path_01": 0.86,
    "a_paste_image": 0.09,
    "a_save_image_temp": 0.05
  }
}
```

The client validates that the returned choice exists in the request-local catalog, ranks the probability map, and shows at most five actions. It never parses generated command text.

## 11. Execution protocol

```text
prepared -> sent -> ranked -> selected -> executing -> completed
                                      \-> failed
                         stale response -> discarded
```

An action becomes stale if the clipboard hash or focused-target hash differs from the request snapshot. The user can reopen the popup to create a new action catalog.

Executors return structured results:

```cpp
struct ExecutionResult {
    std::string request_id;
    std::string action_id;
    ExecutionStatus status;
    std::string message;
    std::optional<std::filesystem::path> output_path;
    std::optional<std::string> output_clipboard_ref;
};
```

Successful save actions add their output path to both the clipboard and `PathHistory` so the new file can be reused by the next decision.

## 12. ImGui popup

The popup displays:

- content preview or image thumbnail;
- target application and focused field summary;
- five ranked actions;
- action label, target path, probability, and execution state;
- keyboard navigation and direct click execution.

Long text is abbreviated. Images are thumbnail-only until selected. Structured resume fields are rendered as separate compact rows.

## 13. Implementation phases

### Phase 1: core protocol and text

- CMake project and Dear ImGui window;
- X11 clipboard capture;
- local history store;
- text save/paste actions;
- dynamic action catalog;
- Djev request/response normalization;
- top-five popup.

### Phase 2: paths, images, and JSON

- `PathHistory`;
- file URI parsing;
- copy/move/path actions;
- image blob storage and thumbnail;
- image save/paste;
- JSON validation and pretty output.

### Phase 3: URL, email, and resume

- URL/download/save actions;
- email parsing and saved email representation;
- compose/send adapter boundary;
- resume field extraction and direct-copy rows.

## 14. Acceptance criteria

1. Pressing `Ctrl+Alt+F` opens the popup over the current desktop session.
2. A text clipboard item can be pasted, saved as a file, or shown as a structured resume candidate.
3. An image clipboard item can be pasted or saved to temp/recent directories.
4. A URL can be pasted, opened, downloaded, or saved as a file when a target directory exists.
5. An email can be copied, saved, and routed to the configured compose/send adapter.
6. A path can be copied, moved, or copied into a recent directory.
7. Valid JSON can be saved raw, saved pretty, or copied pretty.
8. The action list comes from the current dynamic catalog rather than a fixed content-type switch.
9. Djev probabilities are sorted and the top five are displayed.
10. Clicking an action executes the matching local `ActionInstance` without another LLM call.
11. A stale Djev response is discarded and never applied to a changed clipboard/target snapshot.
