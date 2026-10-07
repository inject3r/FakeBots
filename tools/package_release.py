#!/usr/bin/env python3
"""
Packages the FakeBots plugin for a release.

    python3 tools/package_release.py --tag v3.2.0 \
        --linux   build/FakeBots.so  \
        --windows build-win/FakeBots.dll \
        --out dist

Creates in --out
    FakeBots-<tag>-linux-x86.tar.gz     plugins/FakeBots.so + everything the plugin needs
    FakeBots-<tag>-windows-x86.zip      plugins/FakeBots.dll + everything the plugin needs
    FakeBots.so, FakeBots.dll           the bare binaries
    FakeBots.inc                        the Pawn include
    SHA256SUMS.txt                      checksums of every file above

Each archive unpacks into a folder that mirrors a server root:

    FakeBots-<tag>-<platform>-x86/
        plugins/FakeBots.so|dll
        plugins/FakeBots/lang/*.json    translations (the plugin looks them up here)
        include/FakeBots.inc            copy into pawno/include (SA-MP) or qawno/include (open.mp)
        examples/                       sample gamemode
        docs/ README.md LICENSE CHANGELOG.md THIRD_PARTY.md INSTALL.txt

Either platform can be omitted; only what is given gets packaged. The archives are reproducible
(fixed timestamps, owners and ordering).
"""
import argparse
import gzip
import hashlib
import io
import pathlib
import re
import shutil
import sys
import tarfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXED_TIME = 1735689600  # 2025-01-01T00:00:00Z - archives do not depend on the build time

INSTALL_TXT = """FakeBots {tag} - installation ({platform})
================================================

1. Copy plugins/{binary} and the folder plugins/FakeBots/ into the plugins/ folder of your server.
   (plugins/FakeBots/lang/ holds the translations; without it the plugin falls back to English.)

2. Copy include/FakeBots.inc into your compiler's include folder
   (pawno/include for SA-MP, qawno/include for open.mp) and add  #include <FakeBots>  to your script.

3. Register the plugin.
   SA-MP    server.cfg :   plugins {plugin_cfg}
   open.mp  config.json:   "pawn": {{ "legacy_plugins": [ "FakeBots" ] }}

4. The server needs free player slots for the bots (maxplayers / max_players). Bots are ordinary
   players; they never use NPC slots. The plugin always keeps one slot free for real players
   (FakeBotSetReservedSlots).

The plugin is 32-bit (like every SA-MP / open.mp server) and joins the server it runs in over
127.0.0.1 - it cannot be pointed at another host.

See docs/Getting-Started.md and examples/gamemode/FakeBots_Sample.pwn.
"""


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def collect(platform: str, binary: pathlib.Path, tag: str):
    """Returns [(archive_path, source_path_or_bytes, is_executable)] in a stable order."""
    top = f"FakeBots-{tag}-{platform}-x86"
    ext = "so" if platform == "linux" else "dll"
    files = []
    files.append((f"{top}/plugins/FakeBots.{ext}", binary, True))
    for lang in sorted((ROOT / "lang").glob("*.json")):
        files.append((f"{top}/plugins/FakeBots/lang/{lang.name}", lang, False))
    files.append((f"{top}/include/FakeBots.inc", ROOT / "include/FakeBots.inc", False))
    for ex in sorted((ROOT / "examples").rglob("*")):
        if ex.is_file():
            files.append((f"{top}/examples/{ex.relative_to(ROOT / 'examples').as_posix()}", ex, False))
    for doc in sorted((ROOT / "docs").glob("*.md")):
        files.append((f"{top}/docs/{doc.name}", doc, False))
    for name in ("README.md", "LICENSE", "CHANGELOG.md", "THIRD_PARTY.md"):
        files.append((f"{top}/{name}", ROOT / name, False))
    install = INSTALL_TXT.format(
        tag=tag,
        platform="Linux x86" if platform == "linux" else "Windows x86",
        binary=f"FakeBots.{ext}",
        plugin_cfg=f"FakeBots.{ext}" if platform == "windows" else "FakeBots.so",
    ).encode("utf-8")
    files.append((f"{top}/INSTALL.txt", install, False))
    return files


def read(src):
    return src if isinstance(src, bytes) else pathlib.Path(src).read_bytes()


def write_targz(path: pathlib.Path, entries):
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.PAX_FORMAT) as tar:
        dirs = set()
        for name, _, _ in entries:
            parts = name.split("/")
            for i in range(1, len(parts)):
                dirs.add("/".join(parts[:i]))
        for d in sorted(dirs):
            info = tarfile.TarInfo(d)
            info.type = tarfile.DIRTYPE
            info.mode, info.mtime, info.uid, info.gid, info.uname, info.gname = 0o755, FIXED_TIME, 0, 0, "", ""
            tar.addfile(info)
        for name, src, exe in entries:
            data = read(src)
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = 0o755 if exe else 0o644
            info.mtime, info.uid, info.gid, info.uname, info.gname = FIXED_TIME, 0, 0, "", ""
            tar.addfile(info, io.BytesIO(data))
    with open(path, "wb") as f:
        with gzip.GzipFile(filename="", mode="wb", fileobj=f, mtime=0, compresslevel=9) as gz:
            gz.write(raw.getvalue())


def write_zip(path: pathlib.Path, entries):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, src, exe in entries:
            info = zipfile.ZipInfo(name, date_time=(2025, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o755 if exe else 0o644) << 16
            z.writestr(info, read(src))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tag", required=True, help="release tag, e.g. v3.2.0")
    ap.add_argument("--linux", help="path of the Linux FakeBots.so")
    ap.add_argument("--windows", help="path of the Windows FakeBots.dll")
    ap.add_argument("--out", default="dist")
    a = ap.parse_args()

    if not re.fullmatch(r"[A-Za-z0-9._+-]+", a.tag):
        raise SystemExit(f"unusable tag name: {a.tag!r}")
    if not a.linux and not a.windows:
        raise SystemExit("nothing to package: pass --linux and/or --windows")

    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    produced = []

    for platform, given in (("linux", a.linux), ("windows", a.windows)):
        if not given:
            continue
        binary = pathlib.Path(given)
        if not binary.is_file() or binary.stat().st_size < 100_000:
            raise SystemExit(f"{platform} binary missing or implausibly small: {binary}")
        head = binary.read_bytes()[:4]
        if platform == "linux" and head != b"\x7fELF":
            raise SystemExit(f"{binary} is not an ELF file")
        if platform == "windows" and head[:2] != b"MZ":
            raise SystemExit(f"{binary} is not a PE file")

        entries = collect(platform, binary, a.tag)
        archive = out / (f"FakeBots-{a.tag}-linux-x86.tar.gz" if platform == "linux" else f"FakeBots-{a.tag}-windows-x86.zip")
        (write_targz if platform == "linux" else write_zip)(archive, entries)
        produced.append(archive)
        bare = out / ("FakeBots.so" if platform == "linux" else "FakeBots.dll")
        shutil.copy2(binary, bare)
        produced.append(bare)
        print(f"{archive.name}: {len(entries)} files, {archive.stat().st_size} bytes")

    inc = out / "FakeBots.inc"
    shutil.copy2(ROOT / "include/FakeBots.inc", inc)
    produced.append(inc)

    sums = out / "SHA256SUMS.txt"
    sums.write_text("".join(f"{sha256(p)}  {p.name}\n" for p in sorted(produced, key=lambda p: p.name)))
    produced.append(sums)
    print(f"{sums.name}: {len(produced) - 1} entries")
    print("files for the release: " + " ".join(p.name for p in produced))


if __name__ == "__main__":
    sys.exit(main())
