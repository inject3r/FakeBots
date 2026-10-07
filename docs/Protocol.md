# Protocol

What a bot says to a SA-MP 0.3.7 / open.mp server, verified against the open.mp netcode
definitions (`Shared/NetCode`) **and** against live servers: open.mp 1.5.8.3079 and
SA-MP 0.3.7-R2. `tools/probe/rakprobe.cpp` walks through the same steps and prints them.

## 1. Datagrams

* UDP to the server's port. Every datagram a client sends is obfuscated: byte 0 is a checksum
  (XOR of `plain & 0xAA` over the payload), the payload goes through a 256-byte substitution table
  and every second byte is XORed with `(serverPort ^ 0xCC)`. The server rejects anything else.
  The table is a bijection; `SAMPRakNet::EncryptClientDatagram` applies its inverse.
* Datagrams from the server are plain RakNet (the open.mp-specific encryption is only negotiated
  by open.mp clients, which a bot is not).
* Reliability, ordering, splitting and acks are RakNet 2.52 (`RAKNET_LEGACY=1` selects the
  SA-MP numbering). RPCs travel as `[ID_RPC=20][rpcId u8][bit length][payload]`.

## 2. Connecting

| # | Direction | Packet |
| - | --------- | ------ |
| 1 | C to S | `ID_OPEN_CONNECTION_REQUEST` (24) + `u16 cookie ^ 0x6969` (first with cookie 0) |
| 2 | S to C | `ID_OPEN_CONNECTION_COOKIE` (26) + `u16 cookie`; the client repeats 1 with that cookie |
| 3 | S to C | `ID_OPEN_CONNECTION_REPLY` (25) |
| 4 | C to S | `ID_CONNECTION_REQUEST` (11) + the server password (if any) |
| 5 | S to C | `ID_AUTH_KEY` (12) + `u8 len` + challenge string (taken from the server's table) |
| 6 | C to S | `ID_AUTH_KEY` + `u8 len` + the matching 40 character response. A response from the genuine table is what makes the server create a **player** (the NPC answer is never sent) |
| 7 | S to C | `ID_CONNECTION_REQUEST_ACCEPTED` (34): `u32 ip, u16 port, u16 playerIndex, u32 challenge` |
| 8 | C to S | `ID_NEW_INCOMING_CONNECTION` (30), sent by RakNet |

The challenge/response tables of SA-MP and open.mp share no entry; the client carries both
(`SampCryptData.inc`). The server refuses with `ID_NO_FREE_INCOMING_CONNECTIONS` (31) when it is
full and `ID_CONNECTION_BANNED` (36) when the address is banned (reported to the script as
`OnFakeBotDisconnect(botid, 31 / 36)`).

Server-side flood protection to be aware of: more than 30 half-open connections from one address,
or a burst of connections that takes the last free slot, makes the server ban the address for two
minutes. The plugin therefore paces handshakes and never takes the last slot.

## 3. Joining the game

| RPC | Dir | Payload |
| --- | --- | ------- |
| `ClientJoin` 25 | C to S | `u32 4057, u8 modded, u8 nameLen + name, u32 challenge ^ 4057, u8 serialLen + serial, u8 versionLen + "0.3.7-R2"`. The serial is hexadecimal and divisible by 1001 (the servers check it) |
| `ConnectionRejected` 130 | S to C | `u8 reason`: 1 version, 2 nickname (invalid or taken), 3 modification, 4 no slot |
| `InitGame` 139 | S to C | game settings; carries the player id and the sync rates the server wants |
| `RequestClass` 128 | C to S | `i32 classId` |
| `RequestClass` 128 | S to C | `u8 selectable, u8 team, ...` (spawn info) |
| `RequestSpawn` 129 | C to S | (empty) |
| `RequestSpawn` 129 | S to C | **1 byte on SA-MP, 4 bytes on open.mp**: 0 refused, 1 allowed, 2 forced (`SpawnPlayer`) |
| `Spawn` 52 | C to S | (empty) |
| `ClientCheck` 103 | both | `u8 type, u32 address ...`: answered with a well-formed zero result |

Only after `Spawn` does the bot send sync packets. A server only accepts `RequestClass` from a
player that is not alive, which is why a pooled bot is re-spawned through the forced
`RequestSpawn` (see Architecture).

## 4. Sync packets (client to server)

All raw little-endian, no compression, sent `UNRELIABLE_SEQUENCED` on channel 0 at the rate the
server announced in `InitGame` (default 50 ms). A bot that is not being simulated (far from every
real player) still sends a heartbeat at least every 250 ms.

**On foot - id 207**: `u16 leftRight, u16 upDown, u16 keys, f32 x y z, f32 qw qx qy qz,
u8 health, u8 armour, u8 weapon, u8 specialAction, f32 vx vy vz, f32 surfX surfY surfZ,
u16 surfId, u16 animationId, u16 animationFlags`.

**In vehicle - id 200**: `u16 vehicleId, u16 leftRight, u16 upDown, u16 keys, f32 qw qx qy qz,
f32 x y z, f32 vx vy vz, f32 vehicleHealth, u8 playerHealth, u8 playerArmour, u8 weapon,
u8 siren, u8 landingGear, u16 trailerId, u32 hydraThrust/trainSpeed`.

**Passenger - id 211**: `u16 vehicleId, u8 seat, u8 weapon, u8 health, u8 armour, u16 leftRight,
u16 upDown, u16 keys, f32 x y z`.

Conventions the servers enforce (violating them gets the packet dropped):

* Quaternion order is `w, x, y, z`. The heading is a rotation about Z; SA-MP headings: 0 = north (+y),
  90 = west (-x), 180 = south, 270 = east. A walking bot reports the quaternion of its walking
  direction; the test `facing` makes it walk in 8 directions and checks `GetPlayerFacingAngle`.
* Positions must be finite and inside the world (+-20000), velocity vectors shorter than 100.
* An idle bot keeps sending (a silent client is a frozen client).

## 5. Other client RPCs

`Chat` 101 (`u8 length + text`), `ServerCommand` 50 (`u32 length + "/cmd ..."`) for messages that
start with `/`, `Death` 53 (`u8 reason, u16 killer`) when the bot's health reaches 0.

## Deliberate limits

The plugin is not a GTA process: no renderer, no GTA physics, no camera/aim simulation (combat
helpers are server-authoritative), and memory-probing `ClientCheck`s get a safe zero answer. Servers
with an anti-cheat that insists on real GTA modules will treat a bot like any other headless client.
