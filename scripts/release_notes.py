#!/usr/bin/env python3
"""Prints the CHANGELOG.md section of one version as release notes.

    python3 scripts/release_notes.py 0.51.0 > notes.md

Lines wrapped inside a paragraph or a list item are joined, so the notes read well on GitHub and in the program's
"is available" window (which shows the first lines of the release's text).
"""
import pathlib
import sys


def section(changelog: str, version: str) -> list[str]:
    lines = changelog.splitlines()
    start = next((i for i, l in enumerate(lines) if l.startswith(f"## {version} ") or l.strip() == f"## {version}"), None)
    if start is None:
        raise SystemExit(f"CHANGELOG.md has no section for {version}")
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("## ")), len(lines))
    return lines[start + 1:end]


def unwrap(lines: list[str]) -> list[str]:
    out: list[str] = []
    for line in lines:
        text = line.strip()
        continues = out and text and not text.startswith(("-", "#", "|", "```")) and out[-1].strip() and not out[-1].startswith("#")
        if continues:
            out[-1] = out[-1].rstrip() + " " + text
        else:
            out.append(line.rstrip())
    while out and not out[0].strip():
        out.pop(0)
    while out and not out[-1].strip():
        out.pop()
    return out


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: release_notes.py <version>")
    version = sys.argv[1].lstrip("vV")
    root = pathlib.Path(__file__).resolve().parent.parent
    notes = unwrap(section((root / "CHANGELOG.md").read_text(encoding="utf-8"), version))
    notes += ["", f"Download `Sun-Loops-Studio-{version}-x86_64.AppImage` below, make it executable (`chmod +x`) and run it. "
              "SunBurst and HexForge are inside it. `SHA256SUMS.txt` holds its checksum."]
    print("\n".join(notes))


if __name__ == "__main__":
    main()
