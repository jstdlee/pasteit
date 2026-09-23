# PasteIt Linux/Windows decision architecture review

Date: 2026-09-23

## Current flow

Both desktop builds use the same orchestration path: platform services capture
clipboard and focused-window context; `build_desktop_decision` builds a local
action catalog from the newest clipboard item and recent destinations; the
candidate selector sends a bounded, request-local set to Djev; the returned
IDs are validated and ranked; the executor dispatches the chosen local action
to file, URL, email, or fast-action services. Linux supplies X11 clipboard and
focus services plus file-manager/shell path discovery. Windows supplies Win32
clipboard, focus, hotkey, and paste services plus Recent shortcuts and known
folders. The model selects among locally defined actions; it does not supply
shell commands.

## Findings and intended corrections

1. **High: UI may block on a superseded Djev request.** Replacing a
   `std::future` from `std::async` can wait for the old request (default timeout
   30 seconds). Retire superseded requests without destroying unfinished
   futures on the UI thread, and bind each request to its own client settings.
2. **High: Windows UTF-8 path round-trips are inconsistent.** Clipboard paths,
   confirmation strings, action parameters, and output paths pass through
   narrow `std::filesystem::path` constructors or `.string()`. Use the existing
   UTF-8 path conversion helpers at every string/path boundary in this flow.
3. **Medium: copied-file fast actions misread URI lists.** Both platforms store
   file drops as `text/uri-list`, but terminal/hash catalog construction treats
   the list as one literal path. Resolve one valid source path before offering
   these actions; omit ambiguous multi-source fast actions.
4. **Medium: Windows named-zone conversion is actually local-time conversion.**
   The platform implementation must honor configured named source and target
   zones or reject unsupported names rather than silently using local time.
   The proposed Windows implementation uses the C++20 time-zone database;
   Microsoft documents its availability on Windows 10 1903+ and Windows Server
   2022+ (https://learn.microsoft.com/en-us/cpp/standard-library/time-zone-class).
5. **Medium: Djev's selected choice can be truncated from the visible rows.**
   Ranking must retain the validated choice when limiting the popup to eight.

## Design limitations, not defects in this repair

Windows does not currently collect the focused process's working directory;
its destination context is consequently thinner than Linux's. A non-local
Djev endpoint receives a bounded clipboard preview, focused-window metadata,
and destination paths. Keep that disclosure visible in configuration and
consider a future redaction/opt-in policy before defaulting to a remote
endpoint. The current default is local.

## Verification scope

Exercise shared behavior and the Linux build/tests on Linux. Edit Windows
sources for the platform repair, but do not compile or run Windows tests in
this environment; Windows runtime behavior remains unverified until a Windows
build is available.

## Repair and verification record

- Superseded Djev requests are retained until completion and each request owns
  a copy of its client settings, avoiding a UI-thread wait and a settings race.
- Single file URIs resolve before terminal/hash actions are offered; ambiguous
  multi-file selections keep copy/move actions but omit those fast actions.
- UTF-8 path helpers now cover the action catalog, confirmation, execution,
  output clipboard paths, decision payload, and affected UI models. Windows
  default home paths are read through the wide Win32 environment API.
- Windows named-zone conversion uses the C++20 time-zone database; unavailable
  zones fail rather than silently converting in local time. Non-MSVC Windows
  builds reject named zones until their standard library supplies that API.
  The shared
  executor also rejects an empty conversion result.
- The validated Djev choice is retained in the eight visible ranked rows.
- Linux desktop target built successfully. Linux CTest: 55 passed, one X11
  hotkey test skipped, zero failed. Windows build and tests were intentionally
  not run.
