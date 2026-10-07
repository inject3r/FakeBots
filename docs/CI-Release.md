# Releases and CI

`.github/workflows/release.yml` builds, tests and publishes a GitHub release **whenever a tag is
pushed**. The release is created with the repository secret **`PERSONAL_TOKEN`**.

## One-time setup

1. Create a personal access token that may create releases in this repository
   (fine-grained: *Contents: Read and write* on the repo; classic: `repo` / `public_repo`).
2. Repository -> Settings -> Secrets and variables -> Actions -> **New repository secret**,
   name `PERSONAL_TOKEN`, value = the token.

## Releasing

```bash
# 1. bump FAKEBOTS_VERSION_* in src/core/Common.h and add a "## 3.3.0 - ..." section to CHANGELOG.md
git commit -am "Release 3.3.0"
# 2. tag and push
git tag v3.3.0
git push origin v3.3.0
```

Any tag starts the workflow. A tag containing `-` (for example `v3.3.0-rc1`) is published as a
pre-release.

## What the workflow does

| job | work |
| --- | --- |
| `audit` | `tools/source_audit.py` (no NPC code path); warns when the tag and `FAKEBOTS_VERSION_STRING` differ |
| `build-linux` | 32-bit Linux build -> `FakeBots.so` (+ `rakprobe`), verifies ELF 32-bit and the plugin exports |
| `build-windows` | MinGW-w64 i686 cross build -> `FakeBots.dll`, verifies a self-contained 32-bit DLL (imports only KERNEL32 / WS2_32 / msvcrt) |
| `smoke-test` | downloads the pinned open.mp server and runs `basic`, `join 100`, `overfill`, `facing`, `churn` with the freshly built plugin; logs are kept as an artifact |
| `release` | needs all of the above: packages the files, writes the notes from `CHANGELOG.md`, creates (or updates) the release with `gh release` and `PERSONAL_TOKEN` |

## Release assets

| file | content |
| --- | --- |
| `FakeBots-<tag>-linux-x86.tar.gz` | `plugins/FakeBots.so`, `plugins/FakeBots/lang/*.json`, `include/FakeBots.inc`, examples, docs, licences |
| `FakeBots-<tag>-windows-x86.zip` | the same with `plugins/FakeBots.dll` |
| `FakeBots.so`, `FakeBots.dll` | the bare binaries |
| `FakeBots.inc` | the Pawn include |
| `SHA256SUMS.txt` | checksums of everything above |

Archives are byte-for-byte reproducible from the same inputs (fixed timestamps and ownership).

## Notes

* Re-running a failed workflow for the same tag updates the existing release (`--clobber`).
* **Run manually** (Actions -> Release -> *Run workflow*): builds and tests, uploads the artifacts,
  does not publish (there is no tag).
* The workflow needs no other secret; `GITHUB_TOKEN` only reads the code.
* To skip the live test temporarily, remove `smoke-test` from the `needs:` list of `release`.
* Check the file locally before pushing: `python3 tools/package_release.py --help`,
  `actionlint .github/workflows/release.yml`.
