# Installation

## 1. Get the plugin

Either download a release (`FakeBots-<tag>-linux-x86.tar.gz` / `...-windows-x86.zip`, the packages
are laid out like a server folder) or build it: [Building.md](Building.md).

The plugin is **32-bit**: it loads into the 32-bit SA-MP and open.mp servers (Linux and Windows).

## 2. Copy the files

| File | Destination |
| --- | --- |
| `FakeBots.so` (Linux) / `FakeBots.dll` (Windows) | `plugins/` |
| `plugins/FakeBots/lang/*.json` | `plugins/FakeBots/lang/` (messages; missing files fall back to English) |
| `FakeBots.inc` | `pawno/include/` (SA-MP) or `qawno/include/` (open.mp) |

## 3. Register the plugin

SA-MP `server.cfg`:

```
plugins FakeBots.so            # Linux
plugins FakeBots               # Windows (or FakeBots.dll)
maxplayers 120                 # real players + bots
maxnpc 0                       # the bots are normal players - no NPC slots are needed
```

open.mp `config.json`:

```json
{
  "max_players": 120,
  "max_bots": 0,
  "pawn": { "legacy_plugins": [ "FakeBots" ] }
}
```

## 4. Use it

```pawn
#include <a_samp>
#include <FakeBots>

public OnGameModeInit()
{
    FakeBotCreate("John_Doe", 105, 1958.33, 1343.14, 15.37, 90.0);
    return 1;
}
```

Compile with `pawncc` against the includes of your server. Ready-made gamemodes:
`examples/gamemode/FakeBots_Demo.pwn`, `FakeBots_Sample.pwn`.

## How the bots connect

Every bot is a RakNet client of **the server it runs in**: `127.0.0.1` (or the server's `bind`
address), the server's port and password, read from its configuration. There is no NPC mode, no
`npcmodes/` folder, no recording and no second server. The bots occupy ordinary player slots, so
raise `maxplayers` / `max_players` by the number of bots; one slot always stays free for a real
player (`FakeBotSetReservedSlots`).

## Quick check

Start the server and look for the banner in the log:

```
  FakeBots v3.2.0
```

then `OnFakeBotConnect` / `OnFakeBotSpawn` for the first bot. Network trace if something is wrong:
start the server with `FAKEBOTS_DEBUG=1` ([FAQ-Troubleshooting.md](FAQ-Troubleshooting.md)).
