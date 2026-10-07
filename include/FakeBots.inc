// ============================================================================
//  FakeBots.inc
//
//  Official Pawn API for the FakeBots plugin.
//  Author  : Abolfazl Hosseini
//  Repo    : https://github.com/inject3r/FakeBots
//  License : MIT
//  Version : 3.2.0
//
//  This include exposes every piece of functionality the plugin offers as
//  plain natives and optional callbacks. There is no built-in admin system,
//  command handler, or menu - FakeBots only gives you the primitives; how
//  bots are created, controlled and administrated is entirely up to your
//  own gamemode/filterscript code.
//
//  A FakeBots bot is a real network player backed by an embedded RakNet
//  client. The implementation targets the
//  legacy SA-MP 0.3.7 client protocol and exposes the backing player id so
//  normal server-side player natives can operate on that player. The bot is
//  deliberately headless: it does not emulate GTA executable memory, every
//  anti-cheat check, or every modern open.mp extension.
// ============================================================================

#if defined _fakebots_included
    #endinput
#endif
#define _fakebots_included

#pragma library FakeBots

// ----------------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------------
#define FAKEBOTS_INVALID_ID        (-1)
#define FAKEBOTS_MAX_BOTS          (1000)

// On-foot movement styles, used with FakeBotGoTo() / FakeBotAddWaypoint().
#define FAKEBOTS_MOVE_WALK         (1)
#define FAKEBOTS_MOVE_RUN          (2)
#define FAKEBOTS_MOVE_SPRINT       (3)

// Bot lifecycle states, returned by FakeBotGetState().
#define FAKEBOTS_STATE_CONNECTING  (0)
#define FAKEBOTS_STATE_IDLE        (1)
#define FAKEBOTS_STATE_SPAWNED     (2)
#define FAKEBOTS_STATE_DEAD        (3)
#define FAKEBOTS_STATE_REMOVING    (4)
#define FAKEBOTS_STATE_POOLED      (5)

// ----------------------------------------------------------------------------
// Creation / destruction
// ----------------------------------------------------------------------------

/// Creates a new fake bot and begins the connection handshake. The bot is
/// not usable yet when this native returns - wait for OnFakeBotConnect(botid)
/// before calling other FakeBots_* natives on it. Connection and spawn are
/// asynchronous because the embedded client must complete the network handshake.
native FakeBotCreate(const name[], skinid, Float:x, Float:y, Float:z, Float:angle = 0.0, weapon = 0, ammo = 0, virtualworld = 0, interior = 0);

/// Disconnects and permanently removes a bot.
native bool:FakeBotDestroy(botid);

/// Re-spawns a bot using the embedded client RequestClass/RequestSpawn/Spawn flow.
// Use FAKEBOTS_RESPAWN_KEEP for a coordinate/angle that should be preserved.
#define FAKEBOTS_RESPAWN_KEEP Float:0x7FC00000
native bool:FakeBotRespawn(botid, skinid = -1, Float:x = FAKEBOTS_RESPAWN_KEEP, Float:y = FAKEBOTS_RESPAWN_KEEP, Float:z = FAKEBOTS_RESPAWN_KEEP, Float:angle = FAKEBOTS_RESPAWN_KEEP);

/// Returns 1 if `botid` refers to a currently allocated bot slot.
native bool:FakeBotIsValid(botid);

/// Returns the amount of bots currently connected/connecting.
native FakeBotGetCount();

/// Returns the lifecycle state of a bot (see FAKEBOTS_STATE_*).
native FakeBotGetState(botid);

/// Copies the bot's name into `name[]`. Returns the string length.
native FakeBotGetName(botid, name[], size = sizeof(name));

// ----------------------------------------------------------------------------
// Id mapping - the bridge to the standard player natives
// ----------------------------------------------------------------------------

/// Returns the real SA-MP/open.mp player id backing `botid`, or
/// FAKEBOTS_INVALID_ID if the bot hasn't finished connecting yet.
/// Feed this into any standard Set/GetPlayer* native.
native FakeBotGetPlayerID(botid);

/// Reverse lookup: returns the bot id owning `playerid`, or
/// FAKEBOTS_INVALID_ID if that player is not a FakeBots bot.
native FakeBotGetID(playerid);

/// Convenience wrapper around FakeBotGetID(playerid) != FAKEBOTS_INVALID_ID.
native bool:FakeBotIsPlayer(playerid);

// ----------------------------------------------------------------------------
// On-foot movement
// ----------------------------------------------------------------------------

/// Steers the bot on foot toward (x, y, z) in a straight line at `speed`
/// GTA units/second, playing the matching walk/run/sprint animation and
/// keeping the bot's facing angle aligned with its heading. Fires
/// OnFakeBotReachDestination() once it arrives.
native bool:FakeBotGoTo(botid, Float:x, Float:y, Float:z, Float:speed = 1.0, movetype = FAKEBOTS_MOVE_WALK);

/// Cancels any active FakeBotGoTo()/waypoint route.
native bool:FakeBotStopMoving(botid);

/// Returns 1 if the bot is currently walking/running toward a target.
native bool:FakeBotIsMoving(botid);

/// Retrieves the bot's current on-foot movement target.
native bool:FakeBotGetMoveTarget(botid, &Float:x, &Float:y, &Float:z);

/// Appends a point to the bot's on-foot patrol route. The bot starts
/// following the queue automatically as soon as at least one waypoint is
/// present (no need to also call FakeBotGoTo()).
native bool:FakeBotAddWaypoint(botid, Float:x, Float:y, Float:z, Float:speed = 1.0, movetype = FAKEBOTS_MOVE_WALK);

/// Clears the bot's waypoint queue and stops movement.
native bool:FakeBotClearWaypoints(botid);

/// When enabled, waypoints are re-queued at the back of the list as they
/// are reached, turning the route into an endless patrol loop.
native bool:FakeBotSetLoopWaypoints(botid, bool:loop);

// ----------------------------------------------------------------------------
// Vehicles / driving
// ----------------------------------------------------------------------------

/// Creates a vehicle and immediately places the bot in the driver seat.
/// Returns the new vehicle id, or FAKEBOTS_INVALID_ID on failure.
native FakeBotPutInNewVehicle(botid, vehiclemodel, Float:x, Float:y, Float:z, Float:angle, color1 = -1, color2 = -1);

/// Places the bot into an already-existing vehicle (driver by default).
native bool:FakeBotPutInVehicle(botid, vehicleid, seatid = 0);

/// Removes the bot from whatever vehicle it currently occupies.
native bool:FakeBotRemoveFromVehicle(botid);

/// Drives the bot's current vehicle toward (x, y, z) in a straight line at
/// `speed` GTA units/second. Fires OnFakeBotVehicleReachDest() on
/// arrival. The bot must already be the vehicle's driver.
native bool:FakeBotDriveTo(botid, Float:x, Float:y, Float:z, Float:speed = 30.0);

/// Cancels an active FakeBotDriveTo() route (the vehicle simply stops).
native bool:FakeBotStopDriving(botid);

/// Returns 1 if the bot is currently driving toward a target.
native bool:FakeBotIsDriving(botid);

// ----------------------------------------------------------------------------
// Chat
// ----------------------------------------------------------------------------

/// Makes the bot "say" `text` in global chat exactly as a real player
/// would: the message is run through every loaded script's real
/// OnPlayerText(playerid, text) handler first (so chat commands, filters
/// and logging all see it), then broadcast as "Name: text" using the
/// bot's own name and chat color - unless a script explicitly returned 0
/// from OnPlayerText, in which case the broadcast is suppressed, matching
/// standard SA-MP chat behaviour for a blocked message.
native bool:FakeBotSendMessage(botid, const text[]);

// ----------------------------------------------------------------------------
// Groups - bulk control over sets of bots
// ----------------------------------------------------------------------------

/// Creates an empty group identified by `groupid` (any integer you choose
/// - group ids are your own namespace, not allocated by the plugin).
native bool:FakeBotCreateGroup(groupid);

/// Destroys a group. Member bots are NOT destroyed, only ungrouped.
native bool:FakeBotDestroyGroup(groupid);

/// Returns 1 if `groupid` currently exists.
native bool:FakeBotIsValidGroup(groupid);

/// Adds a bot to a group. A bot may belong to any number of groups.
native bool:FakeBotAddToGroup(botid, groupid);

/// Removes a bot from a group.
native bool:FakeBotRemoveFromGroup(botid, groupid);

/// Returns 1 if `botid` is currently a member of `groupid`.
native bool:FakeBotIsInGroup(botid, groupid);

/// Returns the number of bots currently in a group.
native FakeBotGetGroupCount(groupid);

/// Sends every bot in the group toward the same point - see FakeBotGoTo().
/// Returns the number of bots affected.
native FakeBotGroupGoTo(groupid, Float:x, Float:y, Float:z, Float:speed = 1.0, movetype = FAKEBOTS_MOVE_WALK);

/// Stops movement for every bot in the group. Returns the number affected.
native FakeBotGroupStopMoving(groupid);

/// Appends the same waypoint to every bot in the group - see
/// FakeBotAddWaypoint(). Returns the number of bots affected.
native FakeBotGroupAddWaypoint(groupid, Float:x, Float:y, Float:z, Float:speed = 1.0, movetype = FAKEBOTS_MOVE_WALK);

/// Clears the waypoint queue for every bot in the group.
native FakeBotGroupClearWaypoints(groupid);

/// Toggles waypoint looping for every bot in the group.
native FakeBotGroupSetLoopWaypoints(groupid, bool:loop);

/// Destroys every bot currently in the group (the group itself remains,
/// now empty). Returns the number of bots destroyed.
native FakeBotGroupDestroyBots(groupid);

/// Makes every bot in the group send the same chat message - see
/// FakeBotSendMessage(). Returns the number of bots affected.
native FakeBotGroupSendMessage(groupid, const text[]);

/// Arranges every bot in the group into a staggered-line formation behind
/// `leaderplayerid`: bot N in iteration order sits at
/// (offsetx, offsety - N*spacing) in the leader's local (facing-relative)
/// space, and continuously re-follows every tick as the leader moves -
/// see FakeBotFollow() for the underlying per-bot behaviour.
native FakeBotGroupFollow(groupid, leaderplayerid, Float:offsetx, Float:offsety, Float:speed = 2.2, Float:spacing = 1.5);

// ----------------------------------------------------------------------------
// Follow AI
// ----------------------------------------------------------------------------

/// Continuously steers the bot to stay near `targetplayerid` at a fixed
/// local (offsetx, offsety) offset - the offset rotates with the target's
/// facing angle, so it stays consistent (e.g. "always behind-left") as
/// the target turns. Re-evaluated every simulation tick, unlike the
/// one-shot FakeBotGoTo().
native bool:FakeBotFollow(botid, targetplayerid, Float:offsetx, Float:offsety, Float:speed = 2.2);

/// Cancels an active FakeBotFollow() route.
native bool:FakeBotStopFollowing(botid);

/// Returns 1 if the bot is currently following a target.
native bool:FakeBotIsFollowing(botid);

// ----------------------------------------------------------------------------
// Vehicle driving - multi-stop routes
// ----------------------------------------------------------------------------

/// Appends a point to the bot's driving route (the bot must already be
/// the driver of a vehicle). Movement starts automatically once at least
/// one waypoint is queued - feed this a dense, road-following point
/// sequence (e.g. exported from your map's road nodes) for routes that
/// need to stay on roads, since FakeBotDriveTo() alone only goes in a
/// straight line between two points.
native bool:FakeBotAddDriveWaypoint(botid, Float:x, Float:y, Float:z, Float:speed = 30.0);

/// Clears the bot's driving waypoint queue and stops the vehicle.
native bool:FakeBotClearDriveWaypoints(botid);

/// When enabled, driving waypoints are re-queued as they're reached,
/// turning the route into an endless loop (taxi/bus-style patrol).
native bool:FakeBotSetLoopDriveWaypoints(botid, bool:loop);

// ----------------------------------------------------------------------------
// Combat AI (lightweight, opt-in)
// ----------------------------------------------------------------------------

/// Makes the bot face and periodically damage `targetplayerid`: while
/// both are connected/alive, the bot walks into `range` if needed, faces
/// the target, plays a firing animation and reduces the target's health
/// by `damageperhit` every `fireinterval` seconds. This is a simple
/// lightweight scripted combat model, not a full GTA weapon/hit-detection simulation -
/// see docs/API-Reference.md for exactly what it does and doesn't do.
native bool:FakeBotSetCombatTarget(botid, targetplayerid, Float:damageperhit = 5.0, Float:fireinterval = 1.0, Float:range = 20.0);

/// Cancels an active FakeBotSetCombatTarget() engagement.
native bool:FakeBotStopCombat(botid);

/// Returns 1 if the bot currently has an active combat target.
native bool:FakeBotIsInCombat(botid);

// ----------------------------------------------------------------------------
// Idle wander
// ----------------------------------------------------------------------------

/// When `enabled`, a spawned bot with no other active route/follow/combat
/// command periodically picks a random point within `radius` of its
/// position at the time this native was called, walks there, pauses
/// briefly, then picks a new point - useful for ambient crowd filler.
native bool:FakeBotSetIdleWander(botid, bool:enabled, Float:radius = 10.0, Float:speed = 1.0);

/// Returns 1 if idle wander is currently enabled for the bot.
native bool:FakeBotIsIdleWanderEnabled(botid);

// ----------------------------------------------------------------------------
// Random appearance
// ----------------------------------------------------------------------------

/// Overrides the built-in pool of skin ids FakeBotRandomizeAppearance()
/// picks from (the built-in default is a conservative set of universally
/// valid pedestrian skins). Call once, e.g. in OnGameModeInit().
native FakeBotSetAppearancePool(const skinids[], count = sizeof(skinids));

/// Applies a random skin from the appearance pool to the bot and returns
/// the chosen skin id (or -1 on failure / empty pool).
native FakeBotRandomizeAppearance(botid);

// ----------------------------------------------------------------------------
// Bulk loading
// ----------------------------------------------------------------------------

/// Reads a JSON array of bot definitions from `filename` (relative to the
/// server's working directory) and creates them all via FakeBotCreate() -
/// a bulk-creation convenience, not a replacement for the natives/
/// callbacks API. See docs/API-Reference.md for the expected JSON shape.
/// Returns the number of bots successfully created.
native FakeBotLoadFromFile(const filename[]);

// ----------------------------------------------------------------------------
// Performance / tuning
// ----------------------------------------------------------------------------

/// Sets the simulation tick interval in milliseconds (default 50 = 20Hz).
/// Higher values reduce CPU usage on servers with many bots at the cost
/// of coarser movement steps; refuses anything below 10ms.
native bool:FakeBotSetTickRate(milliseconds);

/// Returns the current simulation tick interval in milliseconds.
native FakeBotGetTickRate();

/// Enables/disables the connection pool: when enabled, FakeBotDestroy()
/// hides bots instead of disconnecting them, and FakeBotCreate() reuses
/// a hidden bot (skipping a fresh RakNet handshake) when one
/// is available - useful for servers that create/destroy bots often.
native FakeBotSetPoolingEnabled(bool:enabled);

/// Returns 1 if pooling is currently enabled.
native bool:FakeBotIsPoolingEnabled();

/// Number of player slots bots must leave free for real players (default 1,
/// minimum 1). FakeBotCreate() returns -1 instead of taking one of them.
/// One free slot is mandatory: every bot connects from 127.0.0.1, and a server
/// treats a connection that takes the LAST free slot as a "server full" attack
/// and bans the address for two minutes.
native FakeBotSetReservedSlots(slots);

/// Returns the current number of reserved slots.
native FakeBotGetReservedSlots();

/// Returns the number of bots currently held in the reuse pool.
native FakeBotGetPooledCount();

/// Sets the distance (GTA units) within which a real player triggers
/// OnFakeBotPlayerNearby() for a bot. Default 15.0.
native FakeBotSetNearbyRadius(Float:radius);

/// Sets the distance (GTA units) beyond which a bot is considered "far"
/// from every real player and has its simulation rate throttled to save
/// CPU. Default 60.0.
native FakeBotSetFarUpdateDistance(Float:distance);

/// Returns how long (in milliseconds) the last simulation tick took to
/// process every active bot - useful for monitoring plugin overhead on
/// busy servers.
native Float:FakeBotGetLastTickTime();

/// Returns how many bots are currently walking/running/sprinting toward
/// a target (on-foot movement, follow, or idle wander in motion).
native FakeBotGetMovingCount();

/// Returns how many bots are currently driving toward a target.
native FakeBotGetDrivingCount();

// ----------------------------------------------------------------------------
// Localization (i18n)
// ----------------------------------------------------------------------------

/// Switches the plugin's active language and reloads its JSON text table.
/// `lang` is a short code matching a file named "FakeBots.<lang>.json" in
/// the language directory (e.g. "fa" -> FakeBots.fa.json, "ru" ->
/// FakeBots.ru.json). The default file is always "FakeBots.json" (English)
/// and is used both for lang="en" and as a fallback for missing keys in
/// any other language file.
native bool:FakeBotSetLanguage(const lang[]);

/// Copies the currently active language code into `lang[]`.
native FakeBotGetLanguage(lang[], size = sizeof(lang));

/// Re-reads the active language file from disk (hot-reload translations).
native bool:FakeBotReloadLanguage();

/// Overrides the directory FakeBots looks for language JSON files in.
/// Defaults to "plugins/FakeBots/lang" (relative to the server's working
/// directory). Call this before FakeBotSetLanguage() if you keep your
/// language files somewhere else.
native FakeBotSetLanguageDirectory(const path[]);

/// Looks up `key` in the active language file and copies the localized
/// text into `dest[]`. If the key doesn't exist in either the active
/// language or the default English file, `key` itself is copied back so
/// missing translations are always visible instead of silently empty.
native FakeBotGetText(const key[], dest[], size = sizeof(dest));

// ----------------------------------------------------------------------------
// Callbacks (all optional - implement only the ones you need)
// ----------------------------------------------------------------------------

/// Fired once a bot's RakNet client connection has fully completed and its player id
/// has been assigned; this is the first point at which FakeBotGetPlayerID()
/// returns a valid id and other FakeBots_* natives may be used.
forward OnFakeBotConnect(botid);

/// Fired when a bot spawns into the world (mirrors OnPlayerSpawn).
forward OnFakeBotSpawn(botid);

/// Fired when a bot dies or is killed (mirrors OnPlayerDeath).
forward OnFakeBotDeath(botid, killerid, reason);

/// Fired when a bot disconnects, whether via FakeBotDestroy() or an
/// external kick/timeout (mirrors OnPlayerDisconnect).
forward OnFakeBotDisconnect(botid, reason);

/// Fired when a bot on-foot route (FakeBotGoTo() or the last queued
/// waypoint) has been reached.
forward OnFakeBotReachDestination(botid, Float:x, Float:y, Float:z);

/// Fired every time an intermediate waypoint in a queued route is reached
/// (waypointindex starts at 0 and always increases, even when looping).
forward OnFakeBotWaypointReached(botid, waypointindex);

/// Fired when a bot enters a vehicle (mirrors OnPlayerEnterVehicle).
forward OnFakeBotEnterVehicle(botid, vehicleid, bool:ispassenger);

/// Fired when a bot exits a vehicle (mirrors OnPlayerExitVehicle).
forward OnFakeBotExitVehicle(botid, vehicleid);

/// Fired when a FakeBotDriveTo() route has been completed.
forward OnFakeBotVehicleReachDest(botid, vehicleid, Float:x, Float:y, Float:z);

/// Fired on every player-state transition (mirrors OnPlayerStateChange -
/// see the PLAYER_STATE_* constants in a_samp.inc).
forward OnFakeBotStateChange(botid, newstate, oldstate);

/// Fired when a bot takes damage. Only available on open.mp, or on SA-MP
/// servers/plugins that also provide OnPlayerTakeDamage; silently unused
/// otherwise.
forward OnFakeBotTakeDamage(botid, issuerid, Float:amount, weaponid, bodypart);

/// Fired when a bot deals damage to someone else. Same availability notes
/// as OnFakeBotTakeDamage.
forward OnFakeBotGiveDamage(botid, damagedid, Float:amount, weaponid, bodypart);

/// Fired the moment a real player comes within FakeBotSetNearbyRadius()'s
/// configured distance of a bot (not on every tick they stay close - only
/// on the "entered range" transition).
forward OnFakeBotPlayerNearby(botid, playerid, Float:distance);
