// ============================================================================
//  FakeBots_Sample.pwn
//
//  A self-contained sample gamemode (no extra plugins or includes needed) that exercises every feature of the
//  FakeBots plugin: creation, on-foot patrol routes, vehicle driving
//  routes, localisation and every callback. Copy this into your server's
//  gamemodes/ folder, set it in server.cfg and connect to see it in action.
//
//  This file intentionally builds its own tiny "/bot" admin command set on
//  top of the raw FakeBots API to demonstrate the kind of admin tooling the
//  plugin expects *you* to write - FakeBots itself ships no commands.
// ============================================================================
#include <a_samp>
#include <FakeBots>

#define COLOR_WHITE     0xFFFFFFFF
#define COLOR_GREEN     0x33CC33FF
#define COLOR_YELLOW    0xFFCC00FF

// Walking patrol route around a small block downtown Los Santos.
new Float:g_PatrolRoute[][3] =
{
    {1958.3311, 1343.1421, 15.3746},
    {1975.0233, 1343.9611, 15.3746},
    {1975.6912, 1361.0093, 15.3746},
    {1958.9052, 1360.2115, 15.3746}
};

// A simple driving loop for a taxi-style bot.
new Float:g_DriveRoute[][3] =
{
    {1742.379, -1857.244, 13.547},
    {1846.803, -1857.481, 13.140},
    {1846.988, -1935.951, 13.246},
    {1742.573, -1935.735, 13.312}
};

new g_PatrolBot = FAKEBOTS_INVALID_ID;
new g_DriverBot = FAKEBOTS_INVALID_ID;
new g_PatrolRouteIndex = 0;
new g_DriveRouteIndex = 0;

// ----------------------------------------------------------------------------
main() {}

public OnGameModeInit()
{
    SetGameModeText("FakeBots sample");

    // Pick your plugin language once at startup. Falls back to English for
    // any key missing from the chosen file.
    FakeBotSetLanguage("en"); // try "fa" or "ru" as well

    // A stationary greeter bot: spawns, does nothing else.
    new greeterBot = FakeBotCreate("Greeter_Bot", 105, 1958.33, 1343.14, 15.37, 90.0);
    printf("[FakeBots sample] Requested greeter bot, id=%d", greeterBot);

    // A patrolling bot: created here, route is queued once it connects
    // (see OnFakeBotConnect below).
    g_PatrolBot = FakeBotCreate("Patrol_Bot", 280, g_PatrolRoute[0][0], g_PatrolRoute[0][1], g_PatrolRoute[0][2], 0.0);

    // A driving bot: created here, put in a vehicle and sent driving once
    // it connects.
    g_DriverBot = FakeBotCreate("Driver_Bot", 111, 1742.37, -1857.24, 13.54, 0.0);

    // Give the bots a few seconds to finish joining, then demo group
    // chat (see DemoGroupGreeting below).
    SetTimer("DemoGroupGreeting", 6000, false);

    return 1;
}

// ----------------------------------------------------------------------------
// FakeBots callbacks
// ----------------------------------------------------------------------------

public OnFakeBotConnect(botid)
{
    new name[MAX_PLAYER_NAME];
    FakeBotGetName(botid, name, sizeof(name));
    printf("[FakeBots sample] %s (bot #%d) connected, playerid=%d", name, botid, FakeBotGetPlayerID(botid));

    if (botid == g_PatrolBot)
    {
        // Queue the whole patrol loop and let it run forever.
        for (new i = 0; i < sizeof(g_PatrolRoute); i++)
            FakeBotAddWaypoint(botid, g_PatrolRoute[i][0], g_PatrolRoute[i][1], g_PatrolRoute[i][2], 1.2, FAKEBOTS_MOVE_WALK);
        FakeBotSetLoopWaypoints(botid, true);
    }
    else if (botid == g_DriverBot)
    {
        new vehicleid = FakeBotPutInNewVehicle(botid, 420 /* Taxi */, 1742.37, -1857.24, 13.54, 0.0, 3, 3);
        if (vehicleid != FAKEBOTS_INVALID_ID)
        {
            g_DriveRouteIndex = 1 % sizeof(g_DriveRoute);
            FakeBotDriveTo(botid, g_DriveRoute[g_DriveRouteIndex][0], g_DriveRoute[g_DriveRouteIndex][1], g_DriveRoute[g_DriveRouteIndex][2], 25.0);
        }
    }

    return 1;
}

public OnFakeBotSpawn(botid)
{
    // Every standard native works directly on the bot's real player id.
    new playerid = FakeBotGetPlayerID(botid);
    SetPlayerHealth(playerid, 100.0);
    SetPlayerArmour(playerid, 0.0);
    return 1;
}

// Fired when a FakeBotGoTo() target - or the last waypoint of a route - is reached.
public OnFakeBotReachDestination(botid, Float:x, Float:y, Float:z)
{
    printf("[FakeBots sample] Bot #%d reached its destination (%.2f, %.2f, %.2f).", botid, x, y, z);
    return 1;
}

// Fired for every intermediate stop of a queued (looping) route.
public OnFakeBotWaypointReached(botid, waypointindex)
{
    if (botid == g_PatrolBot)
    {
        g_PatrolRouteIndex = waypointindex;
        printf("[FakeBots sample] Patrol bot reached waypoint #%d.", waypointindex);
    }
    return 1;
}

public OnFakeBotVehicleReachDest(botid, vehicleid, Float:x, Float:y, Float:z)
{
    if (botid != g_DriverBot)
        return 1;

    // Loop the taxi route forever.
    g_DriveRouteIndex = (g_DriveRouteIndex + 1) % sizeof(g_DriveRoute);
    FakeBotDriveTo(botid, g_DriveRoute[g_DriveRouteIndex][0], g_DriveRoute[g_DriveRouteIndex][1], g_DriveRoute[g_DriveRouteIndex][2], 25.0);
    return 1;
}

public OnFakeBotDeath(botid, killerid, reason)
{
    new text[128];
    FakeBotGetText("bot_died", text, sizeof(text));
    printf(text, botid);

    // Simple auto-respawn after death.
    FakeBotRespawn(botid);
    return 1;
}

public OnFakeBotDisconnect(botid, reason)
{
    printf("[FakeBots sample] Bot #%d disconnected (reason %d).", botid, reason);
    return 1;
}

public OnFakeBotEnterVehicle(botid, vehicleid, bool:ispassenger)
{
    printf("[FakeBots sample] Bot #%d entered vehicle #%d (passenger=%d).", botid, vehicleid, ispassenger);
    return 1;
}

// ----------------------------------------------------------------------------
// Group demo: gather every bot into one "crowd" group and make them greet
// the server together a few seconds after startup.
// ----------------------------------------------------------------------------
#define GROUP_TOWN_CROWD 1

forward DemoGroupGreeting();
public DemoGroupGreeting()
{
    FakeBotCreateGroup(GROUP_TOWN_CROWD);
    // Every bot created in OnGameModeInit() joins the group once it exists.
    for (new botid = 0; botid < FAKEBOTS_MAX_BOTS; botid++)
    {
        if (FakeBotIsValid(botid))
            FakeBotAddToGroup(botid, GROUP_TOWN_CROWD);
    }

    FakeBotGroupSendMessage(GROUP_TOWN_CROWD, "Hey, welcome to the server!");
    return 1;
}

// ----------------------------------------------------------------------------
// A minimal /bot admin command set built entirely on the raw FakeBots API -
// this is the kind of layer FakeBots expects gamemode authors to write
// themselves; the plugin has no opinion on permissions or syntax.
// ----------------------------------------------------------------------------
public OnPlayerCommandText(playerid, cmdtext[])
{
    if (!strcmp(cmdtext, "/bots", true))
    {
        new msg[144];
        FakeBotGetText("bot_list_header", msg, sizeof(msg));
        new header[144];
        format(header, sizeof(header), msg, FakeBotGetCount());
        SendClientMessage(playerid, COLOR_YELLOW, header);

        for (new botid = 0; botid < FAKEBOTS_MAX_BOTS; botid++)
        {
            if (!FakeBotIsValid(botid))
                continue;

            new name[MAX_PLAYER_NAME];
            FakeBotGetName(botid, name, sizeof(name));

            new stateKey[32];
            switch (FakeBotGetState(botid))
            {
                case FAKEBOTS_STATE_CONNECTING: stateKey = "bot_state_connecting";
                case FAKEBOTS_STATE_IDLE:       stateKey = "bot_state_idle";
                case FAKEBOTS_STATE_SPAWNED:    stateKey = "bot_state_spawned";
                case FAKEBOTS_STATE_DEAD:       stateKey = "bot_state_dead";
                default:                        stateKey = "bot_state_removing";
            }

            new stateText[32];
            FakeBotGetText(stateKey, stateText, sizeof(stateText));

            new entryFormat[144], line[144];
            FakeBotGetText("bot_list_entry", entryFormat, sizeof(entryFormat));
            format(line, sizeof(line), entryFormat, botid, name, stateText);
            SendClientMessage(playerid, COLOR_WHITE, line);
        }
        return 1;
    }

    return 0;
}
