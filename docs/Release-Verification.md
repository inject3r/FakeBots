# Release verification

What has been verified, and how to repeat it.

## Automated, on every tag (`.github/workflows/release.yml`)

1. `tools/source_audit.py` - no NPC code path, no forbidden API;
2. Linux x86 build + check that the result is an ELF 32-bit shared object exporting the plugin API;
3. Windows x86 cross build + check that it is a self-contained 32-bit DLL (imports only
   `KERNEL32`, `WS2_32`, `msvcrt`);
4. **live test**: a real open.mp server with the freshly built plugin runs the matrix
   (`basic`, `join 100`, `overfill`, `facing`, `churn`);
5. checksums (`SHA256SUMS.txt`) of every release file.

## Done by hand for 3.2.0

| Check | open.mp 1.5.8.3079 | SA-MP 0.3.7-R2 |
| --- | --- | --- |
| 20 / 30 / 100 / 300 bots join, external player count | yes | yes |
| password protected server | yes | yes |
| full server: a real player still gets the last slot, no ban | yes | yes |
| nickname rules, duplicate / invalid nickname | yes | yes |
| create/destroy x4, thread + memory growth | yes | yes |
| walking headings (8 directions, out and back) | yes | yes |
| death, respawn, pooling, chat, groups, vehicles, waypoints (70+ assertions) | yes | yes |
| demo and sample gamemodes | yes | yes |
| AddressSanitizer build of the plugin, no errors | yes | yes |
| clean server shutdown | yes | yes |

## Not verified

* the Windows `FakeBots.dll` was never executed;
* SA-MP 0.3.7-R3 / R4 / R5 and 0.3.DL servers;
* servers with custom anti-cheat plugins;
* more than 300 bots.

## Repeating it

```bash
./run_all_tests.sh            # omp + samp, ~25 min
./run_all_tests.sh omp quick
```

[Testing.md](Testing.md) explains the scenarios and the driver.
