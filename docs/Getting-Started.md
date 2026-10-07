# Getting Started

## Your first bot

```pawn
#include <a_samp>
#include <FakeBots>

new g_MyBot = FAKEBOTS_INVALID_ID;

public OnGameModeInit()
{
    g_MyBot = FakeBotCreate("John_Doe", 105, 1958.33, 1343.14, 15.37, 90.0);
    return 1;
}

public OnFakeBotConnect(botid)
{
    if (botid == g_MyBot)
        printf("My bot is ready! playerid=%d", FakeBotGetPlayerID(botid));
    return 1;
}
```

`FakeBotCreate()` returns immediately with a *bot id* - the bot is not
usable yet at that point because establishing the RakNet client connection is asynchronous, just
like a real client connecting. Always wait for `OnFakeBotConnect(botid)`
before doing anything else with a freshly created bot.

## Using standard natives on a bot

A FakeBots bot is a real player. Once you have its player id via
`FakeBotGetPlayerID(botid)`, every native that works on a real player
works on it identically - no special "bot version" of anything is needed:

```pawn
public OnFakeBotSpawn(botid)
{
    new playerid = FakeBotGetPlayerID(botid);

    SetPlayerSkin(playerid, 105);
    SetPlayerHealth(playerid, 100.0);
    GivePlayerWeapon(playerid, 24, 9999);
    SetPlayerColor(playerid, 0xFF0000FF);
    return 1;
}
```

## Making a bot walk somewhere

```pawn
FakeBotGoTo(botid, 1500.0, 1200.0, 15.0, 1.2, FAKEBOTS_MOVE_WALK);
```

Listen for `OnFakeBotReachDestination` to chain the next action.

## Building a patrol route

```pawn
FakeBotAddWaypoint(botid, 1958.3, 1343.1, 15.3, 1.2, FAKEBOTS_MOVE_WALK);
FakeBotAddWaypoint(botid, 1975.0, 1343.9, 15.3, 1.2, FAKEBOTS_MOVE_WALK);
FakeBotAddWaypoint(botid, 1975.6, 1361.0, 15.3, 1.2, FAKEBOTS_MOVE_WALK);
FakeBotSetLoopWaypoints(botid, true); // patrol forever
```

## Putting a bot behind the wheel

```pawn
new vehicleid = FakeBotPutInNewVehicle(botid, 420, 1742.3, -1857.2, 13.5, 0.0, 3, 3);
FakeBotDriveTo(botid, 1846.8, -1857.4, 13.1, 25.0);
```

Listen for `OnFakeBotVehicleReachDest` to send it to the next stop
(see `examples/gamemode/FakeBots_Sample.pwn` for a full looping taxi bot).

## Cleaning up

```pawn
FakeBotDestroy(botid);
```

For the complete native/callback list, see
[API-Reference.md](API-Reference.md).
