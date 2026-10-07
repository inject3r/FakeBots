# Contributing to FakeBots

Thanks for considering a contribution! This project welcomes issues, pull
requests and documentation improvements.

## Reporting bugs

Please include:

- Your OS and whether you're running SA-MP or open.mp (and version).
- Whether you built from source or used a release binary.
- Server console output around the issue, if any.
- A minimal Pawn snippet that reproduces the problem, if applicable.

## Development setup

1. Fork and clone the repository.
2. Follow [docs/Building.md](docs/Building.md) to get a local build
   working on your platform before making changes.
3. Create a branch: `git checkout -b fix/short-description`.

## Code style

- C++17, 4-space indentation, braces on their own line (see any existing
  `.cpp`/`.h` file for reference).
- Keep natives thin: validate input, delegate to `BotManager` /
  `BotGroupManager` / `LanguageManager`, convert back to AMX cells. Avoid
  putting business logic directly inside `Natives.cpp`.
- Every new native needs:
  1. An entry in `include/FakeBots.inc` with a doc comment.
  2. A corresponding row in `docs/API-Reference.md`.
  3. Registration in `Natives::RegisterAll()`.
- Every new callback needs a `forward` in `FakeBots.inc`, a method on
  `CallbackDispatcher`, and a call site wherever it should fire.
- Comments should explain **why**, not restate the code. Match the
  existing tone - plain, professional, no filler.

## Adding a language

Language files are plain data - no code changes required. Copy
`lang/FakeBots.json`, translate the values (keep the keys and any `%s`/
`%d` placeholders identical), save it as `lang/FakeBots.<code>.json`, and
open a PR. Consider also adding a short mention in
`docs/Localization.md`.

## Pull requests

- Keep PRs focused - one feature or fix per PR is much easier to review.
- Make sure the project still builds on both Linux and Windows (see
  [docs/Building.md](docs/Building.md); CI will also check this
  automatically once you open the PR).
- Update `CHANGELOG.md` under an "Unreleased" heading.
- Describe *what* changed and *why* in the PR description; link any
  related issue.

## Questions

Open a GitHub issue on <https://github.com/inject3r/FakeBots/issues> -
happy to help.
