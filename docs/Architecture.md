# Architecture

## Threads

| Thread | What it does |
| ------ | ------------ |
| server main thread | `ProcessTick` (every few ms): pumps every bot's RakNet inbox (every 10 ms), runs the simulation (every 50 ms), forwards queued events to Pawn |
| one RakNet thread per bot | socket I/O, reliability, acks; wakes up every 20 ms (`SampBotClient::Config::threadSleepMs`) |
| disconnect worker | sends the disconnect notice and joins/frees the RakNet client of a destroyed bot, so the server thread never waits for it |

Everything that touches a bot's `RakClient` happens on the server thread; the only hand-off is
destroying it (queue plus mutex). Process-wide RakNet singletons that are not thread-safe
(string compressor, socket layer, RNG) are created/destroyed under one lock, and the socket layer
now lives until unload (see `third_party/raknet/FAKEBOTS_PATCHES.md`).

## Layers

```
Pawn natives (src/natives)            FakeBotCreate, FakeBotGoTo, ...
        |
BotManager / BotMovement / BotAI      simulation: what the bot wants to do (position, keys, vehicle)
        |
Bot  ->  SampBotClient (src/network)  turns state into packets: join flow, sync, RPCs
        |
RakNet client (third_party/raknet)    obfuscation, cookie, auth, reliability
        |  UDP 127.0.0.1
the server  ->  sampgdk hooks (src/callbacks)  ->  CallbackDispatcher  ->  OnFakeBot* in Pawn
```

## Join pacing and capacity

`FakeBotCreate` only registers the bot; `BotManager::ProcessStartQueue` opens at most 12
handshakes at a time. Before registering, `CountOccupiedSlots()` (connected players plus bots still
joining) is compared with `maxplayers - reservedSlots`; a refusal returns `-1` and logs the reason.
A handshake that is lost is repeated twice on a fresh socket before `OnFakeBotDisconnect` fires.

## Events

Server callbacks reach the plugin through sampgdk *inside* the AMX's `amx_Exec`. Calling back into
Pawn from there corrupts the pending call's parameters, so `CallbackDispatcher` only **queues**
the event; `ProcessTick` forwards the queue when no AMX is executing. Each queued item carries the
bot's generation number: a late event for a destroyed bot can never hit a new bot that reuses the id.

## Bot lifecycle

`Connecting -> Idle -> Spawned -> Dead -> Spawned ...`, `Removing`, `Pooled`. A death is reported
to the server by the bot (`RPC_Death`), as a game client does; `FakeBotRespawn` then walks
`RequestClass -> RequestSpawn -> Spawn` again. A pooled bot keeps its connection, is renamed and
re-spawned through a forced spawn (`SpawnPlayer` is the one reviewed use of that native; the client
still completes the `Spawn` RPC).

## What stays server-side

Natives are used only where the server owns the truth: vehicle creation, spawn information, health
set by scripts (received by the bot as RPCs and mirrored in its local state). Movement is never
produced by `SetPlayerPos`/`SetVehiclePos`: the bot's position is whatever its sync packets say.
