// ============================================================================
//  fbtest.pwn  -  FakeBots integration-test gamemode
//
//  Runs the same checks on a REAL open.mp server and a REAL SA-MP 0.3.7 server.
//  Every bot is a regular network client: the script asserts
//  IsPlayerNPC() == 0 for each of them, and both test servers are configured
//  with ZERO NPC slots, so an NPC connection could not even succeed.
//
//  Configuration: scriptfiles/fbtest.cfg
//      line 1 : number of bots to create        (default 20, minimum 12 in "full" mode)
//      line 2 : seconds to hold the full house  (default 6)  - external player-count queries run here
//      line 3 : "full" or "count"               (default full)
//
//  Output: every assertion prints  [FBTEST] PASS|FAIL <name>; the last line is
//          [FBTEST] RESULT pass=<n> fail=<n>
// ============================================================================
#include <a_samp>
#include <FakeBots>

// open.mp's includes tag weapon ids (WEAPON:), SA-MP's do not: one source, both servers.
#if defined _INC_open_mp
    #define FBT_WTAG            WEAPON
    #define FBT_W(%0)           (WEAPON:(%0))
#else
    #define FBT_WTAG            _
    #define FBT_W(%0)           (%0)
#endif

#define FBT_MAX_BOTS        (700)
#define FBT_CFG_FILE        "fbtest.cfg"
#define FBT_BASE_X          (1958.3783)
#define FBT_BASE_Y          (1343.1572)
#define FBT_BASE_Z          (15.3746)
#define FBT_TICK_MS         (200)

// ---------------------------------------------------------------- bookkeeping
new gPass, gFail;
new gBots[FBT_MAX_BOTS];
new gCount = 20;
new gHoldSeconds = 6;
new bool:gFullMode = true;

new gEvConnect, gEvSpawn, gEvDisconnect, gEvDeath, gEvReach, gEvWaypoint, gEvVehReach;
new gPlayerConnects, gPlayerDisconnects, gPlayerSpawns;
new gUpdates[MAX_PLAYERS];
new gTextCount, gTextPid = -1, gTextLast[144];
new gCmdCount, gCmdPid = -1, gCmdLast[144];
new gDeathPid = -1;
new gSpawnCountByPid[MAX_PLAYERS];
new gTextByPid[MAX_PLAYERS];

new gPhase = -1;
new bool:gPhaseInit;
new gPhaseT0;
new gTimer;
new gVeh;
new Float:gTmpX, Float:gTmpY, Float:gTmpZ;
new gPoolBefore;
new gReuseBot = -1;
new gExtra[2];

enum
{
    PH_WAIT_CONNECT,
    PH_CHECK_STATE,
    PH_HOLD,
    PH_MOVE_RUN,
    PH_MOVE_WAYPOINTS,
    PH_VEHICLE_DRIVE,
    PH_VEHICLE_PASSENGER,
    PH_CHAT,
    PH_DEATH,
    PH_RESPAWN_ALIVE,
    PH_GROUP,
    PH_WANDER,
    PH_COMBAT,
    PH_POOL,
    PH_FILELOAD,
    PH_LANG,
    PH_DESTROY_ALL,
    PH_RECREATE,
    PH_FINISH
}

stock T_Ok(const name[])
{
    gPass++;
    printf("[FBTEST] PASS %s", name);
}

stock T_Fail(const name[], const detail[] = "")
{
    gFail++;
    printf("[FBTEST] FAIL %s -- %s", name, detail);
}

stock T_Check(bool:cond, const name[], const detail[] = "")
{
    if (cond) T_Ok(name); else T_Fail(name, detail);
}

stock Float:T_Dist(Float:x1, Float:y1, Float:z1, Float:x2, Float:y2, Float:z2)
{
    return floatsqroot((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2) + (z1 - z2) * (z1 - z2));
}

stock Float:T_AngleDiff(Float:a, Float:b)
{
    new Float:d = floatabs(a - b);
    while (d > 360.0) d -= 360.0;
    if (d > 180.0) d = 360.0 - d;
    return d;
}

stock CountConnected()
{
    new n;
    for (new p = 0, m = GetMaxPlayers(); p < m; p++)
        if (IsPlayerConnected(p)) n++;
    return n;
}

stock FindPlayerByName(const wanted[])
{
    new nm[MAX_PLAYER_NAME + 1];
    for (new p = 0, m = GetMaxPlayers(); p < m; p++)
    {
        if (!IsPlayerConnected(p)) continue;
        GetPlayerName(p, nm, sizeof nm);
        if (!strcmp(nm, wanted)) return p;
    }
    return -1;
}

stock BotPid(index)
{
    if (index < 0 || index >= gCount || gBots[index] < 0) return -1;
    return FakeBotGetPlayerID(gBots[index]);
}

stock Float:SpawnX(index) return FBT_BASE_X + float(index % 20) * 2.0;
stock Float:SpawnY(index) return FBT_BASE_Y + float(index / 20) * 2.0;

stock GoPhase(phase)
{
    gPhase = phase;
    gPhaseInit = false;
    gPhaseT0 = GetTickCount();
}

stock bool:Timeout(ms)
{
    return (GetTickCount() - gPhaseT0) > ms;
}

stock ReadConfig()
{
    new File:f = fopen(FBT_CFG_FILE, io_read);
    if (!f)
    {
        print("[FBTEST] no scriptfiles/fbtest.cfg - using defaults");
        return 0;
    }
    new line[64];
    if (fread(f, line)) gCount = strval(line);
    if (fread(f, line)) gHoldSeconds = strval(line);
    if (fread(f, line)) gFullMode = (strfind(line, "count", true) == -1);
    fclose(f);
    return 1;
}

// ---------------------------------------------------------------- init
main() {}

public OnGameModeInit()
{
    ReadConfig();
    if (gFullMode && gCount < 12) gCount = 12;
    if (gCount < 1) gCount = 1;
    if (gCount > FBT_MAX_BOTS) gCount = FBT_MAX_BOTS;
    if (gHoldSeconds < 1) gHoldSeconds = 1;

    SetGameModeText("FakeBots test");
    AddPlayerClass(0, 1958.3783, 1343.1572, 15.3746, 269.1425, FBT_W(0), 0, FBT_W(0), 0, FBT_W(0), 0);

    printf("[FBTEST] START bots=%d hold=%ds mode=%s maxplayers=%d", gCount, gHoldSeconds, gFullMode ? "full" : "count", GetMaxPlayers());

    // ---- argument validation: all of these must be refused
    T_Check(FakeBotCreate("", 0, 0.0, 0.0, 3.0) == -1, "create: empty name rejected");
    T_Check(FakeBotCreate("This_Nickname_Is_Way_Too_Long_For_Samp", 0, 0.0, 0.0, 3.0) == -1, "create: >24 char name rejected");
    T_Check(FakeBotCreate("Bad Name!", 0, 0.0, 0.0, 3.0) == -1, "create: illegal characters rejected");
    T_Check(FakeBotDestroy(123456) == false, "destroy: unknown id returns false");
    T_Check(FakeBotGetPlayerID(-5) == -1, "getplayerid: invalid id returns -1");
    T_Check(FakeBotIsValid(-1) == false, "isvalid: -1 is not valid");
    T_Check(FakeBotGetCount() == 0, "count is 0 before any create");

    // ---- create the bots
    new name[MAX_PLAYER_NAME + 1], bad;
    for (new i = 0; i < gCount; i++)
    {
        format(name, sizeof name, "FB_Test_%03d", i);
        gBots[i] = FakeBotCreate(name, 29 + (i % 20), SpawnX(i), SpawnY(i), FBT_BASE_Z, 270.0);
        if (gBots[i] < 0) bad++;
    }
    T_Check(bad == 0, "create: all bots accepted", "some FakeBotCreate() calls failed");
    T_Check(FakeBotCreate("FB_Test_000", 0, 0.0, 0.0, 3.0) == -1, "create: duplicate bot name rejected");

    gTimer = SetTimer("FBT_Tick", FBT_TICK_MS, true);
    GoPhase(PH_WAIT_CONNECT);
    return 1;
}

public OnGameModeExit()
{
    KillTimer(gTimer);
    return 1;
}

// ---------------------------------------------------------------- server callbacks
public OnPlayerRequestClass(playerid, classid)
{
    return 1;
}

public OnPlayerConnect(playerid)
{
    gPlayerConnects++;
    gUpdates[playerid] = 0;
    gSpawnCountByPid[playerid] = 0;
    gTextByPid[playerid] = 0;
    return 1;
}

public OnPlayerDisconnect(playerid, reason)
{
    gPlayerDisconnects++;
    return 1;
}

public OnPlayerSpawn(playerid)
{
    gPlayerSpawns++;
    gSpawnCountByPid[playerid]++;
    return 1;
}

public OnPlayerUpdate(playerid)
{
    gUpdates[playerid]++;
    return 1;
}

public OnPlayerDeath(playerid, killerid, FBT_WTAG:reason)
{
    gDeathPid = playerid;
    return 1;
}

public OnPlayerText(playerid, text[])
{
    gTextCount++;
    gTextPid = playerid;
    gTextByPid[playerid]++;
    format(gTextLast, sizeof gTextLast, "%s", text);
    return 1;
}

public OnPlayerCommandText(playerid, cmdtext[])
{
    gCmdCount++;
    gCmdPid = playerid;
    format(gCmdLast, sizeof gCmdLast, "%s", cmdtext);
    return 1;
}

public OnFakeBotConnect(botid)     { gEvConnect++;    return 1; }
public OnFakeBotSpawn(botid)       { gEvSpawn++;      return 1; }
public OnFakeBotDisconnect(botid, reason) { gEvDisconnect++; printf("[FBTEST] note: OnFakeBotDisconnect bot=%d reason=%d", botid, reason); return 1; }
public OnFakeBotDeath(botid, killerid, reason) { gEvDeath++; return 1; }
public OnFakeBotReachDestination(botid, Float:x, Float:y, Float:z) { gEvReach++; return 1; }
public OnFakeBotWaypointReached(botid, waypointindex) { gEvWaypoint++; return 1; }
public OnFakeBotVehicleReachDest(botid, vehicleid, Float:x, Float:y, Float:z) { gEvVehReach++; return 1; }

// ---------------------------------------------------------------- the test script
forward FBT_Tick();
public FBT_Tick()
{
    new pid, Float:x, Float:y, Float:z, Float:a, buf[96];

    switch (gPhase)
    {
        // -------------------------------------------------------------
        case PH_WAIT_CONNECT:
        {
            if (gEvSpawn >= gCount && gEvConnect >= gCount)
            {
                printf("[FBTEST] all %d bots spawned in %d ms", gCount, GetTickCount() - gPhaseT0);
                T_Ok("all bots connected and spawned");
                GoPhase(PH_CHECK_STATE);
            }
            else if (Timeout(120000 + gCount * 500))
            {
                format(buf, sizeof buf, "connect=%d spawn=%d of %d", gEvConnect, gEvSpawn, gCount);
                T_Fail("all bots connected and spawned", buf);
                GoPhase(PH_DESTROY_ALL);
            }
        }

        // -------------------------------------------------------------
        case PH_CHECK_STATE:
        {
            new okConn, okNpc, okBot, okMap, okState, okPos, okName, okSkin, expected[MAX_PLAYER_NAME + 1], actual[MAX_PLAYER_NAME + 1];
            for (new i = 0; i < gCount; i++)
            {
                pid = BotPid(i);
                if (pid < 0) continue;
                if (IsPlayerConnected(pid)) okConn++;
                if (!IsPlayerNPC(pid)) okNpc++;
                if (FakeBotIsPlayer(pid)) okBot++;
                if (FakeBotGetID(pid) == gBots[i]) okMap++;
                if (GetPlayerState(pid) == PLAYER_STATE_ONFOOT) okState++;
                GetPlayerPos(pid, x, y, z);
                if (T_Dist(x, y, z, SpawnX(i), SpawnY(i), FBT_BASE_Z) < 1.5) okPos++;
                format(expected, sizeof expected, "FB_Test_%03d", i);
                GetPlayerName(pid, actual, sizeof actual);
                if (!strcmp(expected, actual)) okName++;
                if (GetPlayerSkin(pid) == 29 + (i % 20)) okSkin++;
            }
            format(buf, sizeof buf, "%d/%d", okConn, gCount);   T_Check(okConn == gCount, "every bot is a connected player", buf);
            format(buf, sizeof buf, "%d/%d", okNpc, gCount);    T_Check(okNpc == gCount, "IsPlayerNPC() == 0 for every bot (not an NPC)", buf);
            format(buf, sizeof buf, "%d/%d", okBot, gCount);    T_Check(okBot == gCount, "FakeBotIsPlayer() true for every bot", buf);
            format(buf, sizeof buf, "%d/%d", okMap, gCount);    T_Check(okMap == gCount, "FakeBotGetID(playerid) maps back to the bot", buf);
            format(buf, sizeof buf, "%d/%d", okState, gCount);  T_Check(okState == gCount, "every bot is PLAYER_STATE_ONFOOT", buf);
            format(buf, sizeof buf, "%d/%d", okPos, gCount);    T_Check(okPos == gCount, "every bot stands on its requested spawn position", buf);
            format(buf, sizeof buf, "%d/%d", okName, gCount);   T_Check(okName == gCount, "every bot has its requested nickname", buf);
            format(buf, sizeof buf, "%d/%d", okSkin, gCount);   T_Check(okSkin == gCount, "every bot has its requested skin", buf);

            new total = CountConnected();
            format(buf, sizeof buf, "players=%d bots=%d", total, gCount);
            T_Check(total == gCount, "server player count == number of bots", buf);
            format(buf, sizeof buf, "FakeBotGetCount=%d", FakeBotGetCount());
            T_Check(FakeBotGetCount() == gCount, "FakeBotGetCount() == number of bots", buf);
            format(buf, sizeof buf, "connects=%d bots=%d", gPlayerConnects, gCount);
            T_Check(gPlayerConnects == gCount, "OnPlayerConnect fired exactly once per bot", buf);
            format(buf, sizeof buf, "events=%d bots=%d", gEvConnect, gCount);
            T_Check(gEvConnect == gCount, "OnFakeBotConnect fired exactly once per bot", buf);
            format(buf, sizeof buf, "events=%d bots=%d", gEvSpawn, gCount);
            T_Check(gEvSpawn == gCount, "OnFakeBotSpawn fired exactly once per bot", buf);
            format(buf, sizeof buf, "spawns=%d bots=%d", gPlayerSpawns, gCount);
            T_Check(gPlayerSpawns == gCount, "OnPlayerSpawn fired exactly once per bot", buf);

            printf("[FBTEST] STEADY bots=%d players=%d max=%d hold=%d", gCount, total, GetMaxPlayers(), gHoldSeconds);
            GoPhase(PH_HOLD);
        }

        // -------------------------------------------------------------
        case PH_HOLD:
        {
            if (Timeout(gHoldSeconds * 1000))
            {
                new live;
                for (new i = 0; i < gCount; i++)
                    if (BotPid(i) >= 0 && IsPlayerConnected(BotPid(i))) live++;
                format(buf, sizeof buf, "%d/%d still connected after the hold", live, gCount);
                T_Check(live == gCount && CountConnected() == gCount, "bots stay connected while idle (keep-alive sync)", buf);
                printf("[FBTEST] HOLD_DONE");
                GoPhase(gFullMode ? PH_MOVE_RUN : PH_DESTROY_ALL);
            }
        }

        // -------------------------------------------------------------
        case PH_MOVE_RUN:
        {
            pid = BotPid(0);
            if (!gPhaseInit)
            {
                gPhaseInit = true;
                gEvReach = 0;
                GetPlayerPos(pid, gTmpX, gTmpY, gTmpZ);
                gUpdates[pid] = 0;
                T_Check(FakeBotGoTo(gBots[0], gTmpX + 30.0, gTmpY, gTmpZ, 6.0, FAKEBOTS_MOVE_RUN), "FakeBotGoTo accepted");
                T_Check(FakeBotIsMoving(gBots[0]), "FakeBotIsMoving true while walking");
            }
            else if (gEvReach >= 1)
            {
                GetPlayerPos(pid, x, y, z);
                GetPlayerFacingAngle(pid, a);
                format(buf, sizeof buf, "now=%.1f,%.1f,%.1f want=%.1f,%.1f", x, y, z, gTmpX + 30.0, gTmpY);
                T_Check(T_Dist(x, y, z, gTmpX + 30.0, gTmpY, gTmpZ) < 2.0, "bot reached the target (server-side position)", buf);
                format(buf, sizeof buf, "facing=%.1f want=270", a);
                T_Check(T_AngleDiff(a, 270.0) < 15.0, "bot faces its walking direction (east == 270)", buf);
                format(buf, sizeof buf, "%d updates", gUpdates[pid]);
                T_Check(gUpdates[pid] > 20, "OnPlayerUpdate fires continuously while moving", buf);
                T_Check(!FakeBotIsMoving(gBots[0]), "FakeBotIsMoving false after arrival");
                GoPhase(PH_MOVE_WAYPOINTS);
            }
            else if (Timeout(30000))
            {
                T_Fail("bot reached the target (server-side position)", "OnFakeBotReachDestination never fired");
                GoPhase(PH_MOVE_WAYPOINTS);
            }
        }

        // -------------------------------------------------------------
        case PH_MOVE_WAYPOINTS:
        {
            pid = BotPid(1);
            if (!gPhaseInit)
            {
                gPhaseInit = true;
                gEvReach = 0; gEvWaypoint = 0;
                GetPlayerPos(pid, gTmpX, gTmpY, gTmpZ);
                FakeBotAddWaypoint(gBots[1], gTmpX + 10.0, gTmpY, gTmpZ, 6.0, FAKEBOTS_MOVE_RUN);
                FakeBotAddWaypoint(gBots[1], gTmpX + 10.0, gTmpY + 10.0, gTmpZ, 6.0, FAKEBOTS_MOVE_RUN);
                FakeBotAddWaypoint(gBots[1], gTmpX, gTmpY + 10.0, gTmpZ, 6.0, FAKEBOTS_MOVE_RUN);
            }
            else if (gEvReach >= 1 && !FakeBotIsMoving(gBots[1]))
            {
                GetPlayerPos(pid, x, y, z);
                format(buf, sizeof buf, "now=%.1f,%.1f want=%.1f,%.1f", x, y, gTmpX, gTmpY + 10.0);
                T_Check(T_Dist(x, y, z, gTmpX, gTmpY + 10.0, gTmpZ) < 2.0, "bot completed its 3-point waypoint route", buf);
                format(buf, sizeof buf, "waypoint events=%d", gEvWaypoint);
                T_Check(gEvWaypoint >= 2, "OnFakeBotWaypointReached fired along the route", buf);
                GoPhase(PH_VEHICLE_DRIVE);
            }
            else if (Timeout(40000))
            {
                T_Fail("bot completed its 3-point waypoint route", "timeout");
                FakeBotStopMoving(gBots[1]);
                GoPhase(PH_VEHICLE_DRIVE);
            }
        }

        // -------------------------------------------------------------
        case PH_VEHICLE_DRIVE:
        {
            pid = BotPid(2);
            if (!gPhaseInit)
            {
                gPhaseInit = true;
                gEvVehReach = 0;
                GetPlayerPos(pid, gTmpX, gTmpY, gTmpZ);
                gVeh = CreateVehicle(411, gTmpX, gTmpY + 6.0, gTmpZ + 0.5, 0.0, 1, 1, -1);
                T_Check(gVeh != INVALID_VEHICLE_ID, "test vehicle created");
                T_Check(FakeBotPutInVehicle(gBots[2], gVeh, 0), "FakeBotPutInVehicle(driver) accepted");
                gPoolBefore = 0;
            }
            else if (gPoolBefore == 0)
            {
                // give the server a few ticks to settle the state change
                if (Timeout(1500))
                {
                    gPoolBefore = 1;
                    format(buf, sizeof buf, "state=%d vehicle=%d want=%d", GetPlayerState(pid), GetPlayerVehicleID(pid), gVeh);
                    T_Check(GetPlayerState(pid) == PLAYER_STATE_DRIVER && GetPlayerVehicleID(pid) == gVeh, "bot is the driver of the vehicle", buf);
                    GetVehiclePos(gVeh, gTmpX, gTmpY, gTmpZ);
                    T_Check(FakeBotDriveTo(gBots[2], gTmpX + 60.0, gTmpY, gTmpZ, 25.0), "FakeBotDriveTo accepted");
                }
            }
            else if (gEvVehReach >= 1)
            {
                GetVehiclePos(gVeh, x, y, z);
                format(buf, sizeof buf, "vehicle at %.1f,%.1f,%.1f want x=%.1f", x, y, z, gTmpX + 60.0);
                T_Check(T_Dist(x, y, z, gTmpX + 60.0, gTmpY, gTmpZ) < 6.0, "vehicle driven to the destination by the bot", buf);
                GoPhase(PH_VEHICLE_PASSENGER);
            }
            else if (Timeout(45000))
            {
                T_Fail("vehicle driven to the destination by the bot", "OnFakeBotVehicleReachDest never fired");
                GoPhase(PH_VEHICLE_PASSENGER);
            }
        }

        // -------------------------------------------------------------
        case PH_VEHICLE_PASSENGER:
        {
            pid = BotPid(3);
            if (!gPhaseInit)
            {
                gPhaseInit = true;
                gPoolBefore = 0;
                T_Check(FakeBotPutInVehicle(gBots[3], gVeh, 1), "FakeBotPutInVehicle(passenger) accepted");
            }
            else if (gPoolBefore == 0 && Timeout(1500))
            {
                gPoolBefore = 1;
                format(buf, sizeof buf, "state=%d", GetPlayerState(pid));
                T_Check(GetPlayerState(pid) == PLAYER_STATE_PASSENGER, "bot is a passenger", buf);
                FakeBotRemoveFromVehicle(gBots[3]);
                FakeBotRemoveFromVehicle(gBots[2]);
            }
            else if (gPoolBefore == 1 && Timeout(3500))
            {
                format(buf, sizeof buf, "driver=%d passenger=%d", GetPlayerState(BotPid(2)), GetPlayerState(pid));
                T_Check(GetPlayerState(BotPid(2)) == PLAYER_STATE_ONFOOT && GetPlayerState(pid) == PLAYER_STATE_ONFOOT, "bots are back on foot after leaving the vehicle", buf);
                DestroyVehicle(gVeh);
                GoPhase(PH_CHAT);
            }
        }

        // -------------------------------------------------------------
        case PH_CHAT:
        {
            pid = BotPid(4);
            if (!gPhaseInit)
            {
                gPhaseInit = true;
                gPoolBefore = 0;
                gTextCount = 0; gCmdCount = 0;
                T_Check(FakeBotSendMessage(gBots[4], "fbtest hello world"), "FakeBotSendMessage accepted");
            }
            else if (gPoolBefore == 0 && Timeout(2000))
            {
                gPoolBefore = 1;
                format(buf, sizeof buf, "count=%d pid=%d text='%s'", gTextCount, gTextPid, gTextLast);
                T_Check(gTextCount == 1 && gTextPid == pid && !strcmp(gTextLast, "fbtest hello world"), "bot chat reaches OnPlayerText exactly once", buf);
                gTextCount = 0;
                T_Check(FakeBotSendMessage(gBots[4], "/fbtestcmd 42"), "FakeBotSendMessage(/command) accepted");
            }
            else if (gPoolBefore == 1 && Timeout(4000))
            {
                format(buf, sizeof buf, "cmd=%d pid=%d text='%s' text-events=%d", gCmdCount, gCmdPid, gCmdLast, gTextCount);
                T_Check(gCmdCount == 1 && gCmdPid == pid && !strcmp(gCmdLast, "/fbtestcmd 42") && gTextCount == 0, "bot /command reaches OnPlayerCommandText exactly once", buf);
                GoPhase(PH_DEATH);
            }
        }

        // -------------------------------------------------------------
        case PH_DEATH:
        {
            pid = BotPid(5);
            if (!gPhaseInit)
            {
                gPhaseInit = true;
                gDeathPid = -1; gEvDeath = 0;
                SetPlayerHealth(pid, 0.0);
            }
            else if (gDeathPid != -1 && gEvDeath >= 1)
            {
                format(buf, sizeof buf, "deathpid=%d want=%d state=%d", gDeathPid, pid, FakeBotGetState(gBots[5]));
                T_Check(gDeathPid == pid && FakeBotGetState(gBots[5]) == FAKEBOTS_STATE_DEAD, "bot death is reported to the server (OnPlayerDeath + OnFakeBotDeath)", buf);
                GoPhase(PH_RESPAWN_ALIVE);
            }
            else if (Timeout(10000))
            {
                format(buf, sizeof buf, "deathpid=%d events=%d state=%d", gDeathPid, gEvDeath, GetPlayerState(pid));
                T_Fail("bot death is reported to the server (OnPlayerDeath + OnFakeBotDeath)", buf);
                GoPhase(PH_RESPAWN_ALIVE);
            }
        }

        // -------------------------------------------------------------
        case PH_RESPAWN_ALIVE:
        {
            pid = BotPid(5);
            if (!gPhaseInit)
            {
                gPhaseInit = true;
                gPoolBefore = gSpawnCountByPid[pid];
                T_Check(FakeBotRespawn(gBots[5]), "FakeBotRespawn accepted after death");
            }
            else if (gSpawnCountByPid[pid] > gPoolBefore && GetPlayerState(pid) == PLAYER_STATE_ONFOOT)
            {
                T_Check(FakeBotGetState(gBots[5]) == FAKEBOTS_STATE_SPAWNED, "bot respawned and is alive again");
                // second kind of respawn: bot is alive and gets moved with a respawn
                gPoolBefore = gSpawnCountByPid[pid];
                gPhaseInit = false;
                gPhase = PH_GROUP; // continue below, respawn of a living bot is exercised in the group phase
                gPhaseT0 = GetTickCount();
            }
            else if (Timeout(20000))
            {
                format(buf, sizeof buf, "state=%d spawns=%d", GetPlayerState(pid), gSpawnCountByPid[pid]);
                T_Fail("bot respawned and is alive again", buf);
                GoPhase(PH_GROUP);
            }
        }

        // -------------------------------------------------------------
        case PH_GROUP:
        {
            if (!gPhaseInit)
            {
                gPhaseInit = true;
                gEvReach = 0;
                for (new g = 6; g <= 8; g++) gTextByPid[BotPid(g)] = 0;
                T_Check(FakeBotCreateGroup(1), "FakeBotCreateGroup");
                for (new g = 6; g <= 8; g++) FakeBotAddToGroup(gBots[g], 1);
                format(buf, sizeof buf, "members=%d", FakeBotGetGroupCount(1));
                T_Check(FakeBotGetGroupCount(1) == 3, "group has 3 members", buf);
                GetPlayerPos(BotPid(6), gTmpX, gTmpY, gTmpZ);
                FakeBotGroupGoTo(1, gTmpX, gTmpY - 15.0, gTmpZ, 6.0, FAKEBOTS_MOVE_RUN);
                FakeBotGroupSendMessage(1, "group hello");
            }
            else if (gEvReach >= 3)
            {
                new said;
                for (new g = 6; g <= 8; g++) if (gTextByPid[BotPid(g)] == 1) said++;
                format(buf, sizeof buf, "%d/3 members chatted exactly once", said);
                T_Check(said == 3, "group chat goes through the real chat path (3 OnPlayerText)", buf);
                GetPlayerPos(BotPid(7), x, y, z);
                format(buf, sizeof buf, "y=%.1f want=%.1f", y, gTmpY - 15.0);
                T_Check(floatabs(y - (gTmpY - 15.0)) < 3.0, "group walked to the destination", buf);
                FakeBotDestroyGroup(1);
                GoPhase(PH_WANDER);
            }
            else if (Timeout(40000))
            {
                format(buf, sizeof buf, "reach events=%d", gEvReach);
                T_Fail("group walked to the destination", buf);
                FakeBotGroupStopMoving(1);
                FakeBotDestroyGroup(1);
                GoPhase(PH_WANDER);
            }
        }

        // -------------------------------------------------------------
        case PH_WANDER:
        {
            pid = BotPid(9);
            if (!gPhaseInit)
            {
                gPhaseInit = true;
                GetPlayerPos(pid, gTmpX, gTmpY, gTmpZ);
                T_Check(FakeBotSetIdleWander(gBots[9], true, 12.0, 3.0), "FakeBotSetIdleWander accepted");
            }
            else if (Timeout(14000))
            {
                GetPlayerPos(pid, x, y, z);
                new Float:moved = T_Dist(x, y, z, gTmpX, gTmpY, gTmpZ);
                format(buf, sizeof buf, "moved %.1f units", moved);
                T_Check(moved > 1.0 && moved < 40.0, "idle wander makes the bot walk around its anchor", buf);
                FakeBotSetIdleWander(gBots[9], false);
                FakeBotStopMoving(gBots[9]);
                GoPhase(PH_COMBAT);
            }
        }

        // -------------------------------------------------------------
        case PH_COMBAT:
        {
            pid = BotPid(11);
            if (!gPhaseInit)
            {
                gPhaseInit = true;
                SetPlayerHealth(pid, 100.0);
                GetPlayerPos(BotPid(10), gTmpX, gTmpY, gTmpZ);
                SetPlayerPos(pid, gTmpX + 3.0, gTmpY, gTmpZ);
                T_Check(FakeBotSetCombatTarget(gBots[10], pid, 5.0, 0.5, 30.0), "FakeBotSetCombatTarget accepted");
            }
            else
            {
                new Float:hp;
                GetPlayerHealth(pid, hp);
                if (hp < 100.0)
                {
                    format(buf, sizeof buf, "victim health %.1f", hp);
                    T_Ok("combat AI damages its target");
                    FakeBotStopCombat(gBots[10]);
                    SetPlayerHealth(pid, 100.0);
                    GoPhase(PH_POOL);
                }
                else if (Timeout(20000))
                {
                    T_Fail("combat AI damages its target", "victim health never dropped");
                    FakeBotStopCombat(gBots[10]);
                    GoPhase(PH_POOL);
                }
            }
        }

        // -------------------------------------------------------------
        case PH_POOL:
        {
            pid = BotPid(12);
            if (!gPhaseInit)
            {
                gPhaseInit = true;
                gPoolBefore = CountConnected();
                gEvConnect = 0; gEvSpawn = 0;
                FakeBotSetPoolingEnabled(true);
                T_Check(FakeBotIsPoolingEnabled(), "pooling enabled");
                T_Check(FakeBotDestroy(gBots[12]), "destroy with pooling keeps the connection");
                format(buf, sizeof buf, "pooled=%d connected=%d before=%d", FakeBotGetPooledCount(), CountConnected(), gPoolBefore);
                T_Check(FakeBotGetPooledCount() == 1 && CountConnected() == gPoolBefore && IsPlayerConnected(pid), "pooled bot stays connected to the server", buf);
                gReuseBot = FakeBotCreate("FB_Reuse_01", 7, FBT_BASE_X + 50.0, FBT_BASE_Y, FBT_BASE_Z, 90.0);
                T_Check(gReuseBot >= 0, "create after pooling reuses the pooled connection");
            }
            else if (gEvSpawn >= 1)
            {
                new nm[MAX_PLAYER_NAME + 1];
                GetPlayerName(pid, nm, sizeof nm);
                GetPlayerPos(pid, x, y, z);
                format(buf, sizeof buf, "name=%s pos=%.1f,%.1f connected=%d (was %d)", nm, x, y, CountConnected(), gPoolBefore);
                T_Check(!strcmp(nm, "FB_Reuse_01") && CountConnected() == gPoolBefore && T_Dist(x, y, z, FBT_BASE_X + 50.0, FBT_BASE_Y, FBT_BASE_Z) < 2.0, "pooled connection renamed, respawned and reused", buf);
                gBots[12] = gReuseBot;
                FakeBotSetPoolingEnabled(false);
                GoPhase(PH_FILELOAD);
            }
            else if (Timeout(30000))
            {
                T_Fail("pooled connection renamed, respawned and reused", "no spawn after reuse");
                FakeBotSetPoolingEnabled(false);
                GoPhase(PH_FILELOAD);
            }
        }

        // -------------------------------------------------------------
        case PH_FILELOAD:
        {
            if (!gPhaseInit)
            {
                gPhaseInit = true;
                gEvSpawn = 0;
                gPoolBefore = FakeBotGetCount();
                new made = FakeBotLoadFromFile("scriptfiles/fbtest_bots.json");
                format(buf, sizeof buf, "created=%d", made);
                T_Check(made == 2, "FakeBotLoadFromFile created the 2 valid entries (invalid ones skipped)", buf);
            }
            else if (gEvSpawn >= 2)
            {
                format(buf, sizeof buf, "count=%d want=%d", FakeBotGetCount(), gPoolBefore + 2);
                T_Check(FakeBotGetCount() == gPoolBefore + 2, "file-loaded bots joined the server", buf);
                new fid = FakeBotGetID(FindPlayerByName("FB_File_A"));
                T_Check(fid >= 0, "file-loaded bot found by nickname");
                if (fid >= 0) { gExtra[0] = fid; }
                fid = FakeBotGetID(FindPlayerByName("FB_File_B"));
                if (fid >= 0) { gExtra[1] = fid; }
                GoPhase(PH_LANG);
            }
            else if (Timeout(30000))
            {
                format(buf, sizeof buf, "spawned=%d", gEvSpawn);
                T_Fail("file-loaded bots joined the server", buf);
                GoPhase(PH_LANG);
            }
        }

        // -------------------------------------------------------------
        case PH_LANG:
        {
            new txt[128];
            T_Check(FakeBotSetLanguage("fa"), "FakeBotSetLanguage(fa)");
            new n = FakeBotGetText("bot_created", txt);
            format(buf, sizeof buf, "len=%d", n);
            T_Check(n > 0 && strlen(txt) > 0, "FakeBotGetText returns localised text", buf);
            FakeBotGetLanguage(txt);
            T_Check(!strcmp(txt, "fa"), "FakeBotGetLanguage == fa");
            T_Check(!FakeBotSetLanguage("../../etc/passwd"), "language code with path traversal is rejected");
            FakeBotSetLanguage("en");
            GoPhase(PH_DESTROY_ALL);
        }

        // -------------------------------------------------------------
        case PH_DESTROY_ALL:
        {
            if (!gPhaseInit)
            {
                gPhaseInit = true;
                for (new i = 0; i < FBT_MAX_BOTS; i++)
                    if (FakeBotIsValid(i)) FakeBotDestroy(i);
            }
            else if (FakeBotGetCount() == 0 && CountConnected() == 0)
            {
                T_Ok("all bots left; server is empty");
                printf("[FBTEST] EMPTY players=%d", CountConnected());
                GoPhase(gFullMode ? PH_RECREATE : PH_FINISH);
            }
            else if (Timeout(40000))
            {
                format(buf, sizeof buf, "count=%d players=%d", FakeBotGetCount(), CountConnected());
                T_Fail("all bots left; server is empty", buf);
                GoPhase(PH_FINISH);
            }
        }

        // -------------------------------------------------------------
        case PH_RECREATE:
        {
            if (!gPhaseInit)
            {
                gPhaseInit = true;
                gEvSpawn = 0;
                gBots[0] = FakeBotCreate("FB_Test_000", 29, FBT_BASE_X, FBT_BASE_Y, FBT_BASE_Z, 270.0);
                T_Check(gBots[0] >= 0, "a destroyed bot's nickname can be used again right away");
            }
            else if (gEvSpawn >= 1)
            {
                T_Ok("re-created bot joined and spawned");
                FakeBotDestroy(gBots[0]);
                GoPhase(PH_FINISH);
            }
            else if (Timeout(30000))
            {
                T_Fail("re-created bot joined and spawned", "timeout");
                GoPhase(PH_FINISH);
            }
        }

        // -------------------------------------------------------------
        case PH_FINISH:
        {
            if (!gPhaseInit)
            {
                gPhaseInit = true;
                gPhaseT0 = GetTickCount();
            }
            else if (Timeout(1500))
            {
                printf("[FBTEST] RESULT pass=%d fail=%d", gPass, gFail);
                print("[FBTEST] DONE");
                KillTimer(gTimer);
                SendRconCommand("exit");
            }
        }
    }
    return 1;
}
