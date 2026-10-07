// ============================================================================
//  FakeBots demo gamemode
//
//  Starts a server with a handful of bots that wander around Las Venturas, drive
//  a car and chat. Join with a normal SA-MP / open.mp client and try:
//
//      /bots <n>      add n more bots (1..50 per call, as many as there are free slots)
//      /botsclear     remove every bot
//      /botcount      players on the server: real vs. bots
//      /botsfollow    all bots follow you          /botsstop   they go back to wandering
//      /botcar        a bot drives a car on a loop around the Come-A-Lot
//      /botsay <text> a random bot says <text>
//
//  Every bot is an ordinary network player (a real game client would be indistinguishable
//  to this script): it has a player id, appears in the scoreboard and the player list a
//  server browser shows, and fires OnPlayerConnect / OnPlayerSpawn like anybody else.
//
//  Optional scriptfiles/fakebots_demo.cfg (one value per line):
//      1: bots to create at start (default 10)
//      2: seconds between console reports (default 15)
//      3: auto-exit after this many seconds, 0 = never (used by the automated test)
// ============================================================================
#include <a_samp>
#include <FakeBots>

#if defined _INC_open_mp
    #define DEMO_W(%0)      (WEAPON:(%0))
#else
    #define DEMO_W(%0)      (%0)
#endif

#define DEMO_CFG            "fakebots_demo.cfg"
#define DEMO_MAX            (200)

new const gFirst[][] =
{
    "Alex", "Ben", "Carlos", "Dmitri", "Emil", "Farid", "Gabe", "Hamid", "Ivan", "Jamal",
    "Kian", "Luca", "Mehdi", "Nico", "Omar", "Pavel", "Reza", "Sami", "Tariq", "Viktor"
};

new const gLast[][] =
{
    "Wolf", "Stone", "Rivera", "Novak", "Karimi", "Fox", "Hunter", "Moreau", "Sokolov", "Dane",
    "Cross", "Vega", "Hale", "Frost", "Reed", "Marsh", "Ward", "Cole", "Blake", "Shaw"
};

new const gSay[][] =
{
    "hello everyone", "anyone up for a race?", "nice weather today", "this server is great",
    "where is everybody going?", "lag? not here", "gg", "brb"
};

new gStartBots = 10;
new gReportSeconds = 15;
new gAutoExit = 0;
new gBots[DEMO_MAX];
new gBotCount;
new gTicks;
new gStartedAt;
new bool:gReady;
new gTimer;

stock Demo_ReadConfig()
{
    new File:f = fopen(DEMO_CFG, io_read);
    if (!f) return 0;
    new line[32];
    if (fread(f, line) && strval(line) > 0) gStartBots = strval(line);
    if (fread(f, line) && strval(line) > 0) gReportSeconds = strval(line);
    if (fread(f, line)) gAutoExit = strval(line);
    fclose(f);
    return 1;
}

stock Demo_RealPlayers()
{
    new n;
    for (new p = 0, m = GetMaxPlayers(); p < m; p++)
        if (IsPlayerConnected(p) && !FakeBotIsPlayer(p)) n++;
    return n;
}

stock Demo_AllPlayers()
{
    new n;
    for (new p = 0, m = GetMaxPlayers(); p < m; p++)
        if (IsPlayerConnected(p)) n++;
    return n;
}

stock Demo_AddBot()
{
    if (gBotCount >= DEMO_MAX) return -1;
    new name[MAX_PLAYER_NAME + 1], id;
    for (new attempt = 0; attempt < 20; attempt++)
    {
        format(name, sizeof name, "%s_%s", gFirst[random(sizeof gFirst)], gLast[random(sizeof gLast)]);
        // random spot in front of the Come-A-Lot casino, Las Venturas
        id = FakeBotCreate(name, 20 + random(260), 2030.0 + float(random(60) - 30), 1000.0 + float(random(60) - 30), 10.8, float(random(360)));
        if (id != FAKEBOTS_INVALID_ID)
        {
            gBots[gBotCount++] = id;
            return id;
        }
    }
    return -1;
}

stock Demo_ForgetBot(botid)
{
    for (new i = 0; i < gBotCount; i++)
    {
        if (gBots[i] == botid)
        {
            gBots[i] = gBots[--gBotCount];
            return 1;
        }
    }
    return 0;
}

main() {}

public OnGameModeInit()
{
    SetGameModeText("FakeBots demo");
    AddPlayerClass(0, 2030.0, 1000.0, 10.8, 0.0, DEMO_W(0), 0, DEMO_W(0), 0, DEMO_W(0), 0);
#if defined _INC_open_mp
    ShowPlayerMarkers(PLAYER_MARKERS_MODE_GLOBAL);
#else
    ShowPlayerMarkers(1);
#endif
    UsePlayerPedAnims();
    Demo_ReadConfig();
    gStartedAt = GetTickCount();

    for (new i = 0; i < gStartBots; i++) Demo_AddBot();
    printf("[DEMO] creating %d bots (maxplayers=%d, one slot stays free for real players)", gBotCount, GetMaxPlayers());

    gTimer = SetTimer("Demo_Tick", 1000, true);
    return 1;
}

public OnGameModeExit()
{
    KillTimer(gTimer);
    return 1;
}

public OnPlayerRequestClass(playerid, classid)
{
    SetPlayerPos(playerid, 2030.0, 1000.0, 10.8);
    SetPlayerCameraPos(playerid, 2030.0, 1010.0, 14.0);
    SetPlayerCameraLookAt(playerid, 2030.0, 1000.0, 10.8);
    return 1;
}

public OnPlayerConnect(playerid)
{
    if (FakeBotIsPlayer(playerid))
        return 1;
    new name[MAX_PLAYER_NAME + 1], msg[96];
    GetPlayerName(playerid, name, sizeof name);
    format(msg, sizeof msg, "Welcome %s - %d bots are on the server. Try /botcount, /bots 5, /botsfollow", name, gBotCount);
    SendClientMessage(playerid, 0x33CC66FF, msg);
    return 1;
}

public OnPlayerText(playerid, text[])
{
    return 1;
}

// ---------------------------------------------------------------------------
//  Bot events
// ---------------------------------------------------------------------------
public OnFakeBotSpawn(botid)
{
    // let the bot stroll around where it stands
    FakeBotSetIdleWander(botid, true, 25.0, 1.0);
    return 1;
}

public OnFakeBotDisconnect(botid, reason)
{
    Demo_ForgetBot(botid);
    if (reason != 0)
        printf("[DEMO] bot %d left unexpectedly (reason %d)", botid, reason);
    return 1;
}

// ---------------------------------------------------------------------------
//  Console report + readiness line used by the automated test
// ---------------------------------------------------------------------------
forward Demo_Tick();
public Demo_Tick()
{
    gTicks++;

    if (!gReady)
    {
        new spawned;
        for (new i = 0; i < gBotCount; i++)
            if (FakeBotGetState(gBots[i]) == FAKEBOTS_STATE_SPAWNED) spawned++;
        if (spawned >= gBotCount)
        {
            gReady = true;
            printf("[DEMO] READY bots=%d players=%d (all %d bots joined in %d ms)", gBotCount, Demo_AllPlayers(), spawned, GetTickCount() - gStartedAt);
        }
    }

    if (gTicks % gReportSeconds == 0)
        printf("[DEMO] players=%d  bots=%d  real=%d  max=%d", Demo_AllPlayers(), FakeBotGetCount(), Demo_RealPlayers(), GetMaxPlayers());

    // now and then a random bot says something
    if (gBotCount > 0 && random(12) == 0)
        FakeBotSendMessage(gBots[random(gBotCount)], gSay[random(sizeof gSay)]);

    if (gAutoExit > 0 && gTicks >= gAutoExit)
    {
        print("[DEMO] auto-exit");
        SendRconCommand("exit");
    }
    return 1;
}

// ---------------------------------------------------------------------------
//  Player commands
// ---------------------------------------------------------------------------
public OnPlayerCommandText(playerid, cmdtext[])
{
    new cmd[24], idx;
    // first word
    while (cmdtext[idx] > ' ' && idx < sizeof cmd - 1) { cmd[idx] = cmdtext[idx]; idx++; }
    cmd[idx] = 0;
    while (cmdtext[idx] == ' ') idx++;

    if (!strcmp(cmd, "/botcount", true))
    {
        new msg[96];
        format(msg, sizeof msg, "Players: %d  (bots: %d, real players: %d, slots: %d)", Demo_AllPlayers(), FakeBotGetCount(), Demo_RealPlayers(), GetMaxPlayers());
        SendClientMessage(playerid, 0xFFFFFFFF, msg);
        return 1;
    }

    if (!strcmp(cmd, "/bots", true))
    {
        new want = strval(cmdtext[idx]);
        if (want < 1 || want > 50) return SendClientMessage(playerid, 0xFF6666FF, "Usage: /bots <1-50>"), 1;
        new made;
        for (new i = 0; i < want; i++) if (Demo_AddBot() != -1) made++;
        new msg[80];
        format(msg, sizeof msg, "Created %d of %d bots (the rest was refused: no free slot or name clash).", made, want);
        SendClientMessage(playerid, 0x33CC66FF, msg);
        return 1;
    }

    if (!strcmp(cmd, "/botsclear", true))
    {
        for (new i = gBotCount - 1; i >= 0; i--) FakeBotDestroy(gBots[i]);
        gBotCount = 0;
        SendClientMessage(playerid, 0x33CC66FF, "All bots removed.");
        return 1;
    }

    if (!strcmp(cmd, "/botsfollow", true))
    {
        for (new i = 0; i < gBotCount; i++)
        {
            FakeBotSetIdleWander(gBots[i], false);
            FakeBotFollow(gBots[i], playerid, float(random(7) - 3), float(random(7) - 3), 2.2);
        }
        SendClientMessage(playerid, 0x33CC66FF, "The bots follow you.");
        return 1;
    }

    if (!strcmp(cmd, "/botsstop", true))
    {
        for (new i = 0; i < gBotCount; i++)
        {
            FakeBotStopFollowing(gBots[i]);
            FakeBotSetIdleWander(gBots[i], true, 25.0, 1.0);
        }
        SendClientMessage(playerid, 0x33CC66FF, "The bots wander again.");
        return 1;
    }

    if (!strcmp(cmd, "/botcar", true))
    {
        new botid = Demo_AddBot();
        if (botid == -1) return SendClientMessage(playerid, 0xFF6666FF, "No free slot for another bot."), 1;
        // the bot is still connecting: it gets the car as soon as it has spawned
        SetTimerEx("Demo_GiveCar", 3000, false, "i", botid);
        SendClientMessage(playerid, 0x33CC66FF, "A new bot is on its way; it will drive around the casino.");
        return 1;
    }

    if (!strcmp(cmd, "/botsay", true))
    {
        if (!cmdtext[idx] || gBotCount == 0) return SendClientMessage(playerid, 0xFF6666FF, "Usage: /botsay <text>   (needs at least one bot)"), 1;
        FakeBotSendMessage(gBots[random(gBotCount)], cmdtext[idx]);
        return 1;
    }
    return 0;
}

forward Demo_GiveCar(botid);
public Demo_GiveCar(botid)
{
    if (!FakeBotIsValid(botid)) return 0;
    if (FakeBotGetState(botid) != FAKEBOTS_STATE_SPAWNED)
    {
        SetTimerEx("Demo_GiveCar", 1500, false, "i", botid); // still joining
        return 0;
    }
    FakeBotSetIdleWander(botid, false);
    FakeBotPutInNewVehicle(botid, 411, 2030.0, 1020.0, 10.8, 0.0, 1, 1);
    FakeBotAddDriveWaypoint(botid, 2030.0, 1060.0, 10.8, 20.0);
    FakeBotAddDriveWaypoint(botid, 2090.0, 1060.0, 10.8, 20.0);
    FakeBotAddDriveWaypoint(botid, 2090.0,  990.0, 10.8, 20.0);
    FakeBotAddDriveWaypoint(botid, 2030.0,  990.0, 10.8, 20.0);
    FakeBotSetLoopDriveWaypoints(botid, true);
    return 1;
}
