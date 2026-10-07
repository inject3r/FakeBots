#!/usr/bin/env python3
"""Static release audit for FakeBots.

This intentionally checks source, not a generated binary. It catches accidental
regressions back to NPC/native-position driving and stale release metadata.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src"
INCLUDE = ROOT / "include"

BANNED_EXECUTABLE_PATTERNS = [
    re.compile(r"\bConnectNPC\s*\("),
    re.compile(r"\bIsPlayerNPC\s*\("),
    re.compile(r"\bSpawnPlayer\s*\("),
    re.compile(r"\bSetPlayerPos\s*\("),
    re.compile(r"\bSetVehiclePos\s*\("),
]

errors: list[str] = []

# A line carrying this marker is a reviewed, deliberate use of an otherwise banned API.
ALLOW_MARKER = "fakebots-audit-allow:"


def strip_comments(text: str) -> str:
    """Remove // and /* */ comments (keeping line numbers) so that prose never trips the audit."""
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    out = []
    for line in text.splitlines():
        in_str = False
        cut = len(line)
        i = 0
        while i < len(line):
            c = line[i]
            if c == '"' and (i == 0 or line[i - 1] != "\\"):
                in_str = not in_str
            elif not in_str and line.startswith("//", i):
                cut = i
                break
            i += 1
        out.append(line[:cut])
    return "\n".join(out)


for path in sorted(list(SOURCE.rglob("*.cpp")) + list(SOURCE.rglob("*.h"))):
    raw = path.read_text(encoding="utf-8", errors="replace")
    raw_lines = raw.splitlines()
    code_lines = strip_comments(raw).splitlines()
    for number, code in enumerate(code_lines, start=1):
        for pattern in BANNED_EXECUTABLE_PATTERNS:
            if pattern.search(code):
                if ALLOW_MARKER in raw_lines[number - 1] and pattern.pattern.startswith(r"\bSpawnPlayer"):
                    continue
                errors.append(f"forbidden executable API reference: {path}:{number}: {pattern.pattern}")

NPC_WORDS = re.compile(r"ConnectNPC|AuthType_NPC|\"NPC\"", re.I)
for root in (SOURCE, ROOT / "third_party" / "raknet" / "Source", ROOT / "third_party" / "raknet" / "Include"):
    for path in sorted(list(root.rglob("*.cpp")) + list(root.rglob("*.h")) + list(root.rglob("*.hpp"))):
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        if NPC_WORDS.search(text):
            errors.append(f"NPC reference in executable code: {path}")

common = (SOURCE / "core" / "Common.h").read_text(encoding="utf-8")
if 'FAKEBOTS_VERSION_STRING    "3.2.0"' not in common:
    errors.append("version metadata is not 3.2.0")

include = (INCLUDE / "FakeBots.inc").read_text(encoding="utf-8")
if "FAKEBOTS_RESPAWN_KEEP" not in include:
    errors.append("respawn sentinel is missing from the Pawn include")

if errors:
    for error in errors:
        print(f"ERROR: {error}")
    raise SystemExit(1)

print("source audit: PASS")
