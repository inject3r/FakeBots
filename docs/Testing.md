# Testing

Everything below runs a **real server** (open.mp or SA-MP 0.3.7) with the plugin loaded. The bots
connect over UDP exactly like game clients; the result is judged from **inside** (the gamemode's
own assertions) and from **outside** (the public SA-MP query protocol - what a server browser
shows).

```bash
./run_all_tests.sh              # omp + samp, full matrix
./run_all_tests.sh omp quick    # one server, without the 300-bot run
```

or a single scenario:

```bash
python3 tests/run_server_test.py --kind omp  --server-dir omp  --plugin dist/FakeBots.so \
        --compiler omp/qawno/pawncc --scenario join --bots 100 --hold 8
```

Requirements: Linux x86-64, `python3`, 32-bit runtime (`libc6-i386 lib32stdc++6`). `omp/` is
prepared by `tools/setup_test_servers.sh`, `samp/` by `samp/get_samp_server.sh` (the SA-MP server
may not be redistributed, the script downloads it).

## Scenarios

| `--scenario` | gamemode | what must hold |
| --- | --- | --- |
| `basic` | `fbtest.pwn` | the whole API (70+ assertions): create/destroy, events, chat, commands, death + respawn, weapons, vehicles (driver and passenger), waypoints, wandering, groups, combat helpers, JSON loading, languages, pooling, nickname rules |
| `join` | `fbscn.pwn` | N bots join; **server player count == N** (inside and via query), unique nicknames, nobody is an NPC, still connected after the hold, all leave, server empty again |
| `join --password` | `fbscn.pwn` | same on a password protected server; the query reports the password flag |
| `overfill` | `fbscn.pwn` | more bots requested than slots: the plugin stops one slot short, no refusal, **no flood-protection ban**, and an outside player (127.0.0.2) still gets the last slot |
| `badname` | `fbscn.pwn` | nicknames the plugin refuses up front, a nickname only the server can refuse (reported as `-2002`), a valid one joins |
| `facing` | `fbscn.pwn` | a bot walks in 8 directions and back; the **server's** `GetPlayerFacingAngle` matches the heading (proves the rotation in the sync packets is decoded correctly) |
| `churn` | `fbscn.pwn` | N bots are created and destroyed R times; thread count and memory must not grow |
| `demo` | `examples/gamemode/FakeBots_Demo.pwn` | the demo gamemode populates the server |
| `sample` | `examples/gamemode/FakeBots_Sample.pwn` | the sample gamemode loads and runs |

After the scenario the driver checks that the server exits cleanly (no crash marker, exit code 0)
and writes `tests/results/<server>-<scenario>-<bots>.json|log`.

## No NPC, ever

* both servers run with **zero NPC slots** (`max_bots 0` / `maxnpc 0`): a bot that tried to be an
  NPC could not even connect;
* the test gamemodes assert `IsPlayerNPC(id) == 0` for every bot (this is the only place the
  native is mentioned: it proves the bots are ordinary players);
* `tools/source_audit.py` rejects any NPC API or `"NPC"` auth string in the plugin and in the
  vendored RakNet, and runs in CI before anything is built.

## Memory / thread safety

The plugin was also built with AddressSanitizer (`-fsanitize=address`) and loaded into both real
servers (`LD_PRELOAD` of the 32-bit `libasan`) for the `churn` scenario: no error reports.

## Diagnostics

* `tests/bin/rakprobe [name] [port] [seconds] [bind-ip]` - one bot without the plugin; prints every
  packet and RPC (built from `tools/probe/rakprobe.cpp`, `-DFAKEBOTS_BUILD_PROBE=ON`);
* `FAKEBOTS_DEBUG=1` in the server's environment - handshake and link-failure trace;
* `tests/sampquery.py` - the query client.
