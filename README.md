# FakeBots

FakeBots is a SA-MP / open.mp plugin that fills a server with **real network players**. Every bot
owns an embedded RakNet client that joins the server it runs inside of (over `127.0.0.1`) exactly
like a game client does, so for the server and for every script it is an ordinary player.

## Quick start

1. Copy `FakeBots.so` (Linux) or `FakeBots.dll` (Windows) and the folder `plugins/FakeBots/lang/`
   into your server's `plugins/` folder, and copy `FakeBots.inc` to `pawno/include` (SA-MP) or
   `qawno/include` (open.mp). Release packages (`FakeBots-<tag>-linux-x86.tar.gz`,
   `...-windows-x86.zip`) are laid out exactly like that.
2. Register the plugin: SA-MP `server.cfg`: `plugins FakeBots.so` / open.mp `config.json`:
   `"pawn": { "legacy_plugins": [ "FakeBots" ] }`.
3. Raise `maxplayers` / `max_players` by the number of bots you want.
4. In Pawn:

```pawn
#include <a_samp>
#include <FakeBots>

public OnGameModeInit()
{
    FakeBotCreate("John_Doe", 105, 1958.33, 1343.14, 15.37, 90.0);
    return 1;
}

public OnFakeBotSpawn(botid)
{
    FakeBotSetIdleWander(botid, true, 25.0, 1.0);   // stroll around
    return 1;
}
```

`examples/gamemode/FakeBots_Demo.pwn` is a complete demo (bots, a driving bot, chat, `/bots <n>`,
`/botsfollow`, ...); `examples/gamemode/FakeBots_Sample.pwn` shows every callback.

## Things worth knowing

* **Slots.** A bot occupies a normal player slot. The plugin always keeps **one slot free** for a
  real player (`FakeBotSetReservedSlots`): on a server whose last slot is taken by a burst of
  connections from one address, the server's own flood protection bans that address. When no slot is
  left `FakeBotCreate()` returns `-1` and logs why.
* **Nicknames.** 3-24 printable characters, no blanks. The server applies its own rules too; its
  refusal arrives as `OnFakeBotDisconnect(botid, -2002)`.
* **Local only.** The bots connect to `127.0.0.1` (or the `bind` address of the server) and the
  port/password of the server they run in. There is no way to point them at another host.
* **Cost.** One small network thread per bot (wakes up every 20 ms). Roughly 0.2 % of a CPU core per
  bot at steady state, about 0.5 MB of memory each (300 bots: ~150 MB on open.mp, ~145 MB on SA-MP).
  Joins are paced (at most 12 handshakes in flight), 300 bots are in the server in about 8-11 s.
* **Callbacks are deferred.** Server events (`OnPlayerConnect`, `OnPlayerSpawn`, ...) are queued and
  forwarded to `OnFakeBot*` from the plugin's tick (a few ms later), never from inside another
  script's callback.
* **Pooling** (`FakeBotSetPoolingEnabled`) keeps a destroyed bot's connection alive for reuse; it
  still occupies a slot. Off by default.
* **Debugging.** Start the server with the environment variable `FAKEBOTS_DEBUG=1` to get the
  network trace of the handshakes in the server log.

## Building

CMake, no downloads (RakNet is vendored in `third_party/raknet`). The plugin is 32-bit.

```bash
sudo apt-get install -y cmake gcc-multilib g++-multilib      # Linux
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
# Windows DLL, cross-compiled from Linux:
sudo apt-get install -y g++-mingw-w64-i686
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-i686.cmake -DCMAKE_BUILD_TYPE=Release && cmake --build build-win -j
```

More: [docs/Building.md](docs/Building.md).

## Layout

```text
src/network/    the embedded client (join flow, sync packets)       src/bots/       bot state + simulation
src/callbacks/  server hooks + deferred callback dispatch           src/natives/    the Pawn API
third_party/raknet/   vendored RakNet, client-mode patches          include/FakeBots.inc   the Pawn include
tests/          live-server test driver + test gamemodes            tools/          audit, packaging, generators
omp/  samp/     the two test servers (see below)                    docs/           documentation
```