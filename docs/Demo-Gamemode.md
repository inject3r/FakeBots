# Demo gamemode commands and movement recording

The demo gamemode is an example control panel built on top of the FakeBots API. It does not add commands to the plugin itself. Compile `examples/gamemode/FakeBots_Demo.pwn` and run it as your gamemode.

## Commands

| Command | Effect |
|---|---|
| `/help` | Show the command list |
| `/botcount` | Show connected players and bot counts |
| `/botlist [page]` | List bot IDs, names and states (10 per page) |
| `/bots <1-50>` | Create up to 50 bots per command |
| `/botsclear` | Request removal of every bot |
| `/botsfollow` | Make every bot follow the caller |
| `/botsstop` | Return bots to idle wandering |
| `/botcar` | Create a bot that drives a looping route |
| `/botsay <text>` | Have a random bot send a chat message |
| `/botwalk <id> <x> <y> <z> [speed]` | Walk a bot to a position |
| `/botdrive <id> <x> <y> <z> [speed]` | Drive a bot's current vehicle to a position |
| `/botfollow <id>` | Make one bot follow the caller |
| `/botwander <id> [radius]` | Enable idle wandering for one bot |
| `/botstop <id>` | Stop movement and active bot behaviours |
| `/botremove <id>` | Remove one bot |
| `/botrespawn <id>` | Request a respawn |
| `/botpos <id>` | Print a bot's position and player state |
| `/botskin <id> <skin>` | Change a bot's skin |
| `/bothealth <id> <hp>` | Set a bot's health |
| `/botweapon <id> <weapon> <ammo>` | Give a weapon to a bot |
| `/botpool <0|1>` | Toggle connection pooling |
| `/bottick <10-1000>` | Set the simulation tick interval in milliseconds |
| `/record` | Start recording the caller's movement |
| `/stoprecord` | Stop and keep the recorded path in memory |
| `/runrecord` | Create a bot and replay the saved path |
| `/stopreplay` | Stop the current playback |
| `/recordstatus` | Show recording/playback state and frame count |

Bot IDs in these commands are FakeBots IDs, not player IDs. Commands are intentionally available to all players in this demonstration; add permission checks before using it as a production gamemode.

## Recording behaviour

- Sampling interval: 250 ms.
- Capacity: 1,800 samples, roughly 7 minutes and 30 seconds at the configured interval.
- Storage: memory only. Restarting or unloading the gamemode clears the recording.
- Recorded data: position, heading, on-foot versus in-vehicle state, the vehicle model at each sample, and skin changes.
- Playback: a new FakeBots bot follows the sampled path using FakeBots movement and driving natives. Vehicles are created for recorded vehicle sections. A recorded passenger section is replayed with the bot as the driver.
- This is a sampled movement-path replay, not a byte-for-byte client input recording. It does not reproduce the original player's exact key presses, jumps, punches, aiming, gunfire, arbitrary animations, chat, or all vehicle physics. Position changes between samples are interpolated by the bot movement system, so sharp turns and very fast movement may look different.
- Recording belongs to the player who started it. If that player disconnects, recording stops and the data already captured remains in memory.

The path is kept in fixed-size arrays, so playback does not need a file or any extra plugin. The maximum frame count avoids unbounded memory growth on a server.

## Name pool

The demo contains 300 Finglish first names and 300 Finglish family names. It combines them with an underscore, giving up to 90,000 possible combinations before length and duplicate checks. Names are kept ASCII-only to satisfy the FakeBots nickname requirements, and combinations longer than the server's player-name limit are skipped.
