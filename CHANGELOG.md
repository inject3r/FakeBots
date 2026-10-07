# Changelog

## 3.2.0 - A network layer that actually joins real servers

### The embedded RakNet client
- RakNet is vendored in `third_party/raknet/` with client-mode patches (`FAKEBOTS_PATCHES.md`). The old build downloaded `openmultiplayer/RakNet` at configure time and failed to compile (`SAMPRakNet.hpp` includes open.mp server headers) - and that fork is server-side code that decodes datagrams instead of encoding them.
- Client side of the SA-MP protocol implemented: datagram obfuscation (client to server), connection cookie, `ID_AUTH_KEY` challenge/response with the SA-MP and open.mp tables. Without them a server silently ignores the client.
- A server-only rule made every bot disconnect itself exactly 30 seconds after connecting; removed for client connections.
- Several bots shared one process-wide socket layer that the first destroyed bot deleted; two bots could bind the same UDP port; the outgoing encryption buffer was shared between bot threads; reference-counted RakNet singletons were created/destroyed on two threads without a lock; list nodes were deleted through the wrong type. All fixed (ASAN clean on both servers).
- A handshake that is lost (or collides with a stale peer) is retried on a fresh socket before the script is told.

### Protocol
- `ClientJoin`: valid serial (hex, divisible by 1001), challenge response `token ^ 4057`, name/version fields as the servers read them.
- Sync packets rewritten to the layouts the servers read: raw (uncompressed) `PLAYER_SYNC` / `VEHICLE_SYNC` / `PASSENGER_SYNC`, quaternion order `w,x,y,z`, plain health/armour bytes. The previous packets (angle instead of quaternion, packed health/armour) were misparsed by the server.
- Heading convention fixed: bots used to face the mirror image of the direction they walked (`AngleTowards`).
- `RequestSpawn` answer is one byte on SA-MP and four on open.mp; both are accepted.
- Bots report their own death (`RPC_Death`), chat uses the chat RPC and slash-commands the command RPC, `ClientCheck` is answered, RPCs without payload are dispatched.
- A bot seated in a vehicle now sends vehicle/passenger sync even when it is not driving anywhere (it used to stay on foot forever and never became DRIVER).

### Plugin
- Server events are queued and forwarded to `OnFakeBot*` from `ProcessTick`. Calling `amx_Push`/`amx_Exec` from inside the sampgdk hook of another callback swallowed that callback's parameters.
- `OnPlayerRequestClass` is exported (`FakeBots.def`).
- Connection start is paced (12 handshakes in flight) and one player slot is always left free (`FakeBotSetReservedSlots` / `FakeBotGetReservedSlots`): the server bans an address whose burst of connections takes the last slot, and it did ban 127.0.0.1.
- `FakeBotCreate`: empty/too short/blank nicknames are refused up front with a log line instead of creating a bot called "FakeBot" or failing later; failures are reported with readable reasons.
- Pooled bots can be reused (`ForceClassSelection` never worked on a spawned player).
- Bot files (`FakeBotLoadFromFile`) and language files can no longer throw through the plugin boundary; language codes are validated (no path traversal).
- `network.port` and the `bind` address are honoured; the port is read once (no more deprecation warning per bot).
- Bot threads sleep 20 ms, bot inboxes are pumped every 10 ms, disconnects/joins happen off the server thread: 300 bots cost about 66 % of one core on a 1-vCPU VM.
- No NPC wording left in the language files, docs, comments or examples.

## 3.1.0 - Protocol/lifecycle hardening

- Corrected legacy SA-MP 0.3.7 client handshake ordering: `ClientJoin` -> `InitGame` -> `RequestClass` -> `RequestSpawn` -> `Spawn`.
- Corrected legacy player/vehicle sync field widths and serialization order used by the embedded client.
- Added generation-safe transport callbacks and duplicate disconnect suppression.
- Added nickname collision protection before opening a new RakNet session.
- Fixed Pawn output-pointer validation for critical natives and replaced the respawn `(0,0,0)` ambiguity with an explicit NaN sentinel.
- Added symbol visibility controls when RakNet is statically linked into the plugin.
- Added source-level compatibility checks and clarified legacy-protocol scope/known headless-client limitations.


## 3.0.0 - Embedded RakNet client

- Replaced `ConnectNPC()`/npcmode creation with an embedded `RakNet::RakClient` per bot.
- Bots now connect as normal SA-MP/open.mp network players.
- Added real `RPC_ClientJoin`, `RPC_RequestClass`, `RPC_CHAT`, `PLAYER_SYNC` and `PLAYER_VEHICLE_SYNC` handling.
- Removed server-side per-tick `SetPlayerPos()` and `SetVehiclePos()` as the bot movement transport.
- Fixed bot/player mapping so `OnPlayerConnect` does not depend on `IsPlayerNPC()`.
- Fixed destruction and transport-loss bookkeeping so mappings are not silently leaked.
- Fixed `FakeBotSendMessage()` to use the actual client chat RPC.
- Fixed idle-wander state leakage caused by a static bot-id map surviving bot destruction/reuse.
- Removed bundled SA-MP/open.mp test servers and NPC mode test files from the repository.
- Added an explicit RakNet CMake dependency and offline source override.