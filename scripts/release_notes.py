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
    notes += ["", "### Downloads", "",
              f"- **Linux** (x86_64, glibc 2.40 or newer): `Sun-Loops-Studio-{version}-x86_64.AppImage` - make it executable "
              "(`chmod +x`) and run it.",
              f"- **macOS** (10.15 or newer, Apple silicon and Intel): `Sun-Loops-Studio-{version}-macOS.dmg` - drag the app to "
              "Applications; the first time, right-click it > Open.",
              f"- **Windows** (10 and 11, 64-bit): `Sun-Loops-Studio-{version}-Windows-x64-Setup.exe` (installer) or "
              f"`Sun-Loops-Studio-{version}-Windows-x64.zip` (no installation); SmartScreen: More info > Run anyway.",
              "",
              "Each contains the program, its sounds and its four instruments (SunBurst, HexForge, SunSymphony, SunRiff). The same "
              "files without the version in their name are the README's permanent links; `SHA256SUMS*.txt` hold the checksums."]
    print("\n".join(notes))


if __name__ == "__main__":
    main()
