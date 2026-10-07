# Troubleshooting

## First: look at the log line

Every failed bot logs one line with the reason:

```
[FakeBots] Bot 3 ('Jane_Doe', player -1): connection ended - the server rejected the nickname (code -2002).
```

| code | meaning | what to do |
| --- | --- | --- |
| `29` | the server did not answer the connection attempt | is the server up and reachable on 127.0.0.1 (or its `bind` address)? firewall on the loopback? |
| `31` | the server has no free player slot | raise `maxplayers` / `max_players` |
| `32` | the server closed the connection (kick / ban / `Kick()` in a script) | check `OnPlayerConnect` of your scripts and the ban list |
| `33` | the connection was lost (timeout) | the server stalled for more than 15 s |
| `36` | the server banned the address | `127.0.0.1` is in `samp.ban` / `bans.json`; remove it |
| `37` | wrong server password | the plugin reads the server's own `password`; check it is set the same way for the process |
| `-2001` | the server rejected the client version | a 0.3.7 server is expected (netgame version 4057) |
| `-2002` | the server rejected the nickname | already online, or characters / length not allowed by the server |
| `-2004` | no free slot (open.mp) | as `31` |
| `-1001`..`-1007` | a join stage timed out (the number is the stage) | the server is overloaded, or a script blocks the class/spawn request |

## `FakeBotCreate` returns -1

The reason is logged right before: empty / shorter than 3 / longer than 24 / contains blanks or
non-printable characters, **or no slot is left** ("... must stay free for real players"). The plugin
never takes the last free slot (`FakeBotSetReservedSlots`, default 1): a burst of connections from
one address that fills the server makes the server's own flood protection ban that address.

## The bot connects but never spawns

The plugin runs the real handshake (`ClientJoin -> InitGame -> RequestClass -> RequestSpawn ->
Spawn`). A script can stop it: `OnPlayerRequestClass` / `OnPlayerRequestSpawn` returning `0`,
a kick in `OnPlayerConnect`, a gamemode without any `AddPlayerClass`. `SpawnPlayer()` is not used
as a shortcut on purpose.

## Debugging the network handshake

```bash
FAKEBOTS_DEBUG=1 ./omp-server        # or ./samp03svr
```

prints the open-connection / cookie exchange and why a link was declared lost.
`tests/bin/rakprobe` joins one bot without the plugin and prints every packet.

## open.mp

* load the plugin through `pawn.legacy_plugins`, not `components`;
* `max_bots` can stay `0`;
* the plugin reads `network.port` (and `bind`); the old `port` variable is only a fallback;
* the legacy network protocol is used (the one every 0.3.7 client speaks). Custom anti-cheat that
  requires a real GTA process cannot be satisfied by a headless client.

## SA-MP

* `plugins FakeBots.so` (Linux), `maxnpc 0` is fine;
* the server writes `server_log.txt`; when stdout is a file it is block-buffered, read the log file;
* 0.3.7-R2 was tested; 0.3.7-R3/R4/R5 use the same join protocol but were not available for testing.

## ClientCheck

`ClientCheck` requests are answered with a safe zero result. A headless client has no GTA process
in memory, so memory-backed checks cannot produce a genuine result.

## Cost

One small thread per bot (wakes every 20 ms), about 0.2 % of a core and 0.5 MB each at steady
state; 300 bots join in 8-11 s.

## Pooling

Pooling keeps a destroyed bot's connection alive for reuse and therefore keeps its player slot.
Off by default.

## Windows

The DLL is cross-compiled and its imports/exports are verified, but it has **not been executed**
by the project's tests (no Windows machine). If it does not load: install the "Visual C++ 2015-2022"
runtime is *not* needed (the MinGW runtime is linked in); make sure the server is the 32-bit one.
