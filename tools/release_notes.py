#!/usr/bin/env python3
"""
Prints the Markdown body of a GitHub release.

    python3 tools/release_notes.py --tag v3.2.0 [--changelog CHANGELOG.md] > notes.md

The text of the release is the matching "## <version>" section of CHANGELOG.md (the tag may carry
a leading "v"), followed by a table of the attached files and short install / verify steps. If the
changelog has no section for the tag a pointer to the commit history is used instead.
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def changelog_section(text: str, version: str):
    pattern = re.compile(r"^##\s+v?" + re.escape(version) + r"(?:\s|$).*?$", re.M)
    m = pattern.search(text)
    if not m:
        return None
    nxt = re.search(r"^##\s+", text[m.end():], re.M)
    end = m.end() + nxt.start() if nxt else len(text)
    body = text[m.end():end].strip()
    return body or None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--changelog", default=str(ROOT / "CHANGELOG.md"))
    a = ap.parse_args()

    version = a.tag[1:] if a.tag[:1] in ("v", "V") else a.tag
    path = pathlib.Path(a.changelog)
    body = changelog_section(path.read_text(encoding="utf-8"), version) if path.exists() else None

    out = []
    out.append(f"# FakeBots {a.tag}\n")
    out.append(body if body else f"_No changelog entry for `{version}`; see the commit history for this tag._")
    out.append(f"""
## Downloads

| File | What it is |
| --- | --- |
| `FakeBots-{a.tag}-linux-x86.tar.gz` | **Linux** package: `plugins/FakeBots.so`, translations, include, examples, docs |
| `FakeBots-{a.tag}-windows-x86.zip` | **Windows** package: `plugins/FakeBots.dll`, translations, include, examples, docs |
| `FakeBots.so` / `FakeBots.dll` | the bare plugin binaries (32-bit, like every SA-MP / open.mp server) |
| `FakeBots.inc` | the Pawn include (copy to `pawno/include` or `qawno/include`) |
| `SHA256SUMS.txt` | checksums of all files above |

## Install

1. Unpack the package for your OS into the root of your server (it contains `plugins/` and `plugins/FakeBots/lang/`).
2. Copy `include/FakeBots.inc` to your compiler's include folder and `#include <FakeBots>`.
3. SA-MP `server.cfg`: `plugins FakeBots.so` (Linux) / `plugins FakeBots.dll` (Windows).
   open.mp `config.json`: `"pawn": {{ "legacy_plugins": [ "FakeBots" ] }}`.
4. Raise `maxplayers` / `max_players` by the number of bots you want. Bots are ordinary players and never use NPC slots.

## Verify

```
sha256sum -c SHA256SUMS.txt --ignore-missing
```
""")
    sys.stdout.write("\n".join(out))


if __name__ == "__main__":
    main()
