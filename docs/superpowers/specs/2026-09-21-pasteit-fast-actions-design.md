# PasteIt fast actions and content tools design

Date: 2026-09-21

Status: Implemented in the working tree on 2026-09-23. Linux desktop and headless builds pass; Windows runtime verification remains pending.

## Context

PasteIt already builds a content-dependent action catalog, sends the catalog to the local Djev decision API for ranking, executes fixed action kinds locally, and renders the result in Dear ImGui. The next feature set adds local content extraction, network diagnostics, GitHub helpers, date/time conversion, Mermaid generation, QR generation, resumable downloads, file hashes, and image annotation.

The implementation must preserve the current interaction model:

- the current clipboard item is the source of truth for the active catalog;
- Djev ranks declared action choices but does not provide executable commands;
- an `ActionKind` and typed parameters map each selected choice to a local executor;
- long-running work never blocks the ImGui frame loop;
- auxiliary modals are independent platform windows/viewports, not children constrained to the root popup;
- platform-specific behavior is behind platform services so Windows and macOS adapters can be added later.

## Goals

1. Add the requested fast actions for contact/business-card text, paths, IP addresses, GitHub URLs, date/time values, text/code explanation, Mermaid, QR, downloads, hashing, and image annotation.
2. Keep action ranking deterministic when local detection says an action is not applicable, while allowing Djev to rank applicable actions and user preference feedback to adjust future ranking.
3. Provide usable previews and copy/save controls for generated results.
4. Keep optional external renderers (`mmdc`, `qrencode`) optional and provide a useful source/payload fallback when they are unavailable.
5. Keep all new behavior testable without requiring a live desktop UI or live network service, while retaining live Djev/General LLM verification tests.

## Non-goals

- Do not turn Djev into a command executor.
- Do not add a full embedded browser or JavaScript runtime to ImGui.
- Do not add password/token filtering to the local demo.
- Do not make the root window always-on-top.
- Do not make simple local actions depend on an LLM request.

## Content model and detection

Extend `ContentKind` and semantic tags with `DateTime` where needed. Keep image and path entries represented by existing clipboard/path stores.

The detector produces an `ObservedContent`/classification result containing:

- content kind and confidence;
- optional extracted values;
- applicable capability tags;
- a bounded preview used by the UI and decision request;
- source reference and original content reference.

Detection is local and cheap. It runs before catalog construction, so unrelated actions are not sent to Djev.

### Business-card/contact extraction

For text containing contact-like signals, extract a structured table with these optional fields:

- name;
- organization and title;
- street/city/region/postal code/country;
- phone numbers;
- email addresses;
- websites and social handles.

Use existing resume-style field extraction patterns as the first pass, with email/phone/address heuristics and line-label matching. The UI can optionally invoke the General LLM to improve ambiguous fields, but the local extraction result must be immediately viewable.

Actions:

- `ExtractContactInfo`: open the structured table modal;
- `CopyContactAsJson`: copy the table as JSON;
- `SaveContactAsJson`: save the table to a user-selected recent directory;
- `CopyContactField`: copy an individual selected field.

The table modal has selectable values and a copy button per row. No separate action is created when no contact-like field is found.

### Path detection

For a path item, add `OpenTerminalAtPath`. The platform service receives the path and opens the configured terminal with its working directory set to the path’s directory (or the path itself when it is a directory). If the path is a file, the containing directory is used.

Existing copy/move actions remain available and continue to use recent directory targets plus an explicit target picker.

### IP detection and diagnostics

Recognize strict IPv4 and IPv6 values, including values embedded in a short text item when there is one unambiguous address. Add:

- `PingIp`;
- `TraceRouteIp` (Linux command label `traceroute`, Windows adapter can map to `tracert`);
- `ReverseDnsIp`;
- `DigIp`;
- `NetworkDiagnosticReport`.

Each command is an argv-style invocation through the platform network executor, never a shell command assembled from the clipboard string. The report action runs the applicable checks asynchronously and combines stdout, stderr, exit status, and elapsed time into a selectable report modal.

### GitHub URL detection

Recognize `github.com/<owner>/<repo>` URLs and normalize trailing `.git`, query, and fragment components. Add:

- `CloneGithubHttps`: ask for a destination directory and clone using the HTTPS URL;
- `CloneGithubSsh`: ask for a destination directory and clone using `git@github.com:owner/repo.git`;
- `CopyGithubHttpsUrl`;
- `CopyGithubSshUrl`.

The clone modal uses recent directories as a selectable list, remembers the last choice, shows progress/output asynchronously, and reports the resulting repository path.

### Date/time detection

Recognize ISO-8601, RFC-style, common local date/time forms, Unix seconds, and Unix milliseconds when unambiguous. Preserve the original text and parsed timezone/precision metadata.

Add:

- `ConvertTimezone`: parameter modal with source zone and target zone;
- `ToUnixTimestamp`: seconds/milliseconds selector;
- `CopyNormalizedDateTime`.

The local date/time service must use the host timezone database where available. The action result modal displays original value, parsed value, target zone, and copy buttons.

### Text/code explanation

Add built-in prompt templates for `Explain text` and `Explain code`, enabled by default but managed through the existing CRUD prompt-template settings. Code detection is heuristic and should only affect the label/ranking; both prompts remain available for sufficiently long text.

The prompts use the existing `{input}` variable expansion and parameter modal. Results use the existing General LLM result state and independent AI result viewport. The prompt action is part of the normal ranked action list and is not rendered as a separate shortcut section.

## Action catalog and decision protocol

Add explicit `ActionKind` values and stable IDs for every new capability. Each catalog entry includes:

- `kind`;
- stable `id`;
- source reference;
- human-readable label and description;
- typed parameters or parameter schema;
- whether the action is local, asynchronous, or LLM-backed.

The Djev request continues to send `criteria` as `option_name -> description`. Descriptions must explain the observable result and required inputs, not expose a shell command. The response only selects/ranks an action ID. Unknown IDs or inapplicable actions are ignored and the local fallback ranker chooses from the valid catalog.

For actions requiring parameters, the action is still ranked normally; selection opens the parameter modal before execution. Inputs are remembered per action kind where practical: directory, filename, timezone, clone destination, annotation text, and prompt variables.

The display limit remains eight ranked actions. Local applicability filtering happens before this limit so the top eight are relevant to the current clipboard item.

## Mermaid generation and rendering

### Detection

Add a local diagram-signal detector for relationship arrows, flow keywords, timeline markers, UML/class notation, entity inheritance, database schema terms, and structured node/edge lists. It should avoid offering Mermaid for ordinary prose with a single incidental arrow.

### Generation

`DrawMermaidDiagram` starts a General LLM job with a system prompt requiring:

- Mermaid source only, without Markdown fences;
- a supported diagram type;
- valid node/edge syntax;
- no explanatory prose in the source result.

Normalize fenced responses, trim accidental prose, validate the diagram header and basic syntax, and retain both raw and normalized source. The UI shows an error/source fallback if normalization fails.

### Rendering

Use optional local `mmdc`/Mermaid CLI to render SVG or PNG. The renderer is invoked through a platform-independent process adapter, with a Linux implementation first. If `mmdc` is unavailable or rendering fails:

- show the Mermaid source in a selectable editor;
- show the renderer status/error;
- keep Source and Copy buttons enabled;
- do not fail the LLM action itself.

The preview viewport contains the graph at the top, followed by Source, Copy source, Save image, and Close controls.

## QR generation

Offer `GenerateQr` for email, URL, and short text. The payload is exactly the normalized source value. Use optional local `qrencode` through the process adapter. When available, render a PNG preview in an independent QR modal with Save and Copy payload controls. When unavailable, show the payload and renderer status so the action remains useful.

## Download manager

Replace one-shot URL download behavior with a `DownloadJob` state machine:

`Queued -> Running -> Paused -> Running -> Completed`, with `Cancelled` and `Failed` terminal states.

Each job stores URL, destination, `.part` path, total bytes if known, downloaded bytes, speed, error, and whether the server accepted Range requests. Use an async worker and libcurl/argv-free HTTP implementation already available in the project; do not block the UI.

Resume behavior:

- reuse an existing `.part` file;
- request a byte range from its current size;
- append only after a valid partial response;
- rename to the final file only after successful completion;
- if the server does not support Range, report the fallback and restart safely.

The independent download modal contains progress, byte counts, speed, Pause, Resume, Cancel, Open destination, and Copy path controls. Cancellation preserves `.part` for a later resume unless the user explicitly chooses to discard it.

## Hash actions

For path/file content add `HashSha256` and `HashSha512`. Run hashing asynchronously for large files, stream data in bounded chunks, and expose algorithm, file, byte count, elapsed time, and digest in a selectable result modal. Copy digest and Copy `algorithm  path` are available. The initial Linux implementation may use the existing platform process adapter or a small local streaming implementation; the interface must not depend on Linux command names.

## Image annotation

Add `AnnotateImage` to image actions. The annotation viewport includes:

- original image preview;
- expanded bottom fast-annotation area;
- red pen/line/rectangle/arrow tools;
- white-background text/comment input;
- undo/clear;
- Save annotated image;
- Copy temporary image path.

Annotations are maintained as a non-destructive overlay until export. Export uses the existing image decode path plus a small image-write adapter; the adapter can use a bundled encoder or a detected local encoder without making the whole app depend on it. If export is unavailable, the overlay remains viewable and the UI reports the reason.

## Platform-independent services

Add capability interfaces under a platform-neutral namespace, with Linux implementations under `src/platform/linux`:

- `TerminalService`;
- `NetworkDiagnosticService`;
- `ProcessService`;
- `MermaidRenderer`;
- `QrRenderer`;
- `DownloadService`;
- `HashService`;
- `DateTimeService`;
- `ImageAnnotationService`.

The UI and action executors consume these interfaces. Windows and macOS can later supply adapters for terminal launch, process execution, networking command names, time zones, rendering, and image export without changing catalog or modal state models.

## UI behavior

All generated-result and long-running-operation windows use the existing independent viewport facility. They must:

- open focused and above the root window without setting the root window permanently always-on-top;
- be movable by their own title bars;
- have a stable initial size with scrolling for long content;
- place operation controls in a top toolbar;
- use selectable/copyable labels and result values;
- show source/status/error text without abbreviation in detail editors;
- keep image, Mermaid, QR, contact, network, hash, download, and AI results in separate state objects.

## Persistence and settings

Add settings for:

- optional `mmdc` path and renderer arguments;
- optional `qrencode` path and error-correction defaults;
- download resume directory and `.part` retention;
- default hash algorithms shown;
- terminal command/profile;
- date/time display preferences;
- annotation export format and default save directory.

Prompt templates remain CRUD-managed one row per prompt. Built-in Explain text/code templates are seeded only when absent, so users can modify, duplicate, disable, or delete them.

## Testing strategy

### Pure/core tests

- contact/business-card detection and stable table extraction;
- IP, GitHub, date/time, Mermaid-signal, QR eligibility, and code detection;
- action catalog applicability and stable IDs;
- Djev criteria map name-to-description payload;
- parameter expansion and remembered values;
- GitHub URL normalization and SSH conversion;
- date/time conversion and timestamp units;
- Mermaid response normalization/validation;
- download state transitions and Range/resume decisions;
- hash streaming result formatting;
- QR payload normalization;
- annotation geometry/state history.

### Service/executor tests

- fake terminal/network/process services receive typed argv and paths;
- ping/traceroute/reverse-DNS/dig report aggregation;
- clone destination and result handling;
- download worker pause/cancel/resume against a local range-capable HTTP fixture;
- mmdc/qrencode unavailable fallback;
- hash service with known SHA-256/SHA-512 fixtures;
- image annotation export fixture when encoder is available.

### Integration/UI tests

- live Djev ranking with only current clipboard actions;
- live General LLM Mermaid/explain/contact enrichment when configured;
- independent viewport creation for each result modal;
- top-eight action rendering;
- no frame-loop stall while copying input, running network tools, rendering, hashing, or downloading;
- desktop and headless builds plus existing CTest suite.

## Acceptance criteria

1. Clipboard text that looks like a business card opens a selectable contact table and supports row-level copy and JSON output.
2. A path action offers opening Terminal at the correct containing directory.
3. An IP action list offers ping, traceroute/tracert, reverse DNS, dig, and a combined report.
4. A GitHub URL offers HTTPS/SSH copy and clone actions with a destination modal.
5. Date/time input offers timezone conversion and Unix timestamp conversion.
6. Text/code offers ranked Explain prompts using the existing General LLM parameter protocol.
7. Diagram-like text ranks Mermaid near the top and shows graph preview when `mmdc` exists, otherwise source fallback.
8. URL downloads show progress and can pause, cancel, and resume from `.part` when supported.
9. File paths offer SHA-256 and SHA-512 with copyable digests.
10. Images offer red/white fast annotation and temporary-file copy.
11. URL, email, and short text offer QR preview when `qrencode` exists and a clear payload fallback otherwise.
12. All new result windows are independent and the root window is not forced always-on-top.
