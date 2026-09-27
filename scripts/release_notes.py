#!/usr/bin/env python3
"""Release notes for a build: commits since the previous build-* tag.

Usage: release_notes.py <commit-sha> [artifact-dir]
Each commit subject becomes a bullet with its body indented below it;
Co-Authored-By / Signed-off-by trailers are dropped.
"""
import os
import subprocess
import sys


def git(*args):
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout


def escape(text):
    """GitHub renders notes as HTML-capable Markdown: keep <n> and the like literal."""
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def main():
    sha = git("rev-parse", sys.argv[1] if len(sys.argv) > 1 else "HEAD").strip()
    artifacts = sys.argv[2] if len(sys.argv) > 2 else "artifacts"
    tags = [t for t in git("tag", "--list", "build-*", "--sort=-v:refname").split() if t]
    previous = tags[0] if tags else None  # newest build release
    if previous:
        revs, since = f"{previous}..{sha}", f"since {previous}"
    else:
        revs, since = f"-n20 {sha}", "(latest 20 commits)"
    log = git("log", "--no-merges", "--format=%x1e%h%x1f%s%x1f%b", *revs.split())
    lines = [f"Automated build of `{sha[:7]}` on `main`.", "", f"## Changes {since}", ""]
    for record in filter(None, log.split("\x1e")):
        short, subject, body = (record.split("\x1f") + ["", ""])[:3]
        lines.append(f"- **{escape(subject.strip())}** ({short.strip()})")
        for body_line in body.splitlines():
            if not body_line.strip() or body_line.startswith(("Co-Authored-By:", "Signed-off-by:")):
                continue
            lines.append(f"  {escape(body_line.rstrip())}")
    if len(lines) == 4:
        lines.append("- No new commits.")
    lines += ["", "## Downloads", "",
              "- `pasteit-linux-x86_64.zip`: unzip, then run `./pasteit` (X11 desktop; press Ctrl+Alt+F)."]
    if os.path.exists(os.path.join(artifacts, "pasteit-windows-x64.zip")):
        lines.append("- `pasteit-windows-x64.zip`: unzip, then run `pasteit.exe`.")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
