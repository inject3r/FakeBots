// ============================================================================
//  fbscn.pwn  -  FakeBots scenario gamemode (robustness / scale tests)
//
//  Works on a REAL open.mp server and a REAL SA-MP 0.3.7 server. Every bot is
//  an ordinary network client.
//
//  scriptfiles/fbscn.cfg
//      line 1 : scenario   join | overfill | badname | churn | facing
//      line 2 : number of bots (join / overfill / churn)
//      line 3 : join     -> seconds to hold the full house
//               overfill -> number of bots that are expected to get in (= free slots)
//               churn    -> number of create/destroy rounds
//
//  Output: [FBSCN] PASS|FAIL <name>, finally  [FBSCN] RESULT pass=<n> fail=<n>
// ============================================================================
#include <a_samp>
#include <FakeBots>

#if defined _INC_open_mp
    #define FBS_W(%0)       (WEAPON:(%0))
#else
    #define FBS_W(%0)       (%0)
#endif

#define FBS_MAX             (400)
#define FBS_CFG             "fbscn.cfg"

new gScn[24] = "join";
new gN = 10;
new gExtra = 5;

new gPass, gFail;
new gBots[FBS_MAX];
new gEvConnect, gEvSpawn, gEvDisc;
new gDiscReason[FBS_MAX];
new gDiscSeen;
new gTimer, gT0, gRound;
new gStage;
new Float:gFx, Float:gFy, Float:gFz;

stock S_Ok(const name[])
{
    gPass++;
    printf("[FBSCN] PASS %s", name);
}

stock S_Fail(const name[], const detail[] = "")
{
    gFail++;
    printf("[FBSCN] FAIL %s -- %s", name, detail);
}

stock S_Check(bool:cond, const name[], const detail[] = "")
{
    if (cond) S_Ok(name); else S_Fail(name, detail);
}

stock S_Connected()
{
    new n;
    for (new p = 0, m = GetMaxPlayers(); p < m; p++)
        if (IsPlayerConnected(p)) n++;
    return n;
}

stock S_NpcCount()
{
    new n;
    for (new p = 0, m = GetMaxPlayers(); p < m; p++)
        if (IsPlayerConnected(p) && IsPlayerNPC(p)) n++;
    return n;
}

stock bool:S_Timeout(ms)
{
    return (GetTickCount() - gT0) > ms;
}

stock S_Stage(stage)
{
    gStage = stage;
    gT0 = GetTickCount();
}

stock S_ResetEvents()
{
    gEvConnect = 0;
    gEvSpawn = 0;
    gEvDisc = 0;
    gDiscSeen = 0;
}

stock S_CreateBots(count, const prefix[] = "FBS")
{
    new name[MAX_PLAYER_NAME + 1], bad;
    for (new i = 0; i < count && i < FBS_MAX; i++)
    {
        format(name, sizeof name, "%s_%03d", prefix, i);
        gBots[i] = FakeBotCreate(name, 29 + (i % 20), 1958.3783 + float(i % 25) * 1.5, 1343.1572 + float(i / 25) * 1.5, 15.3746, 270.0);
        if (gBots[i] < 0) bad++;
    }
    return bad;
}

stock S_DestroyAll()
{
    for (new i = 0; i < FBS_MAX; i++)
        if (FakeBotIsValid(i)) FakeBotDestroy(i);
}

stock S_ReadConfig()
{
    new File:f = fopen(FBS_CFG, io_read);
    if (!f)
    {
        print("[FBSCN] no scriptfiles/fbscn.cfg - using defaults");
        return 0;
    }
    new line[64];
    if (fread(f, line))
    {
        new len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ')) line[--len] = 0;
        format(gScn, sizeof gScn, "%s", line);
    }
    if (fread(f, line)) gN = strval(line);
    if (fread(f, line)) gExtra = strval(line);
    fclose(f);
    return 1;
}

main() {}

public OnGameModeInit()
{
    S_ReadConfig();
    if (gN < 1) gN = 1;
    if (gN > FBS_MAX) gN = FBS_MAX;

    SetGameModeText("FakeBots scenarios");
    AddPlayerClass(0, 1958.3783, 1343.1572, 15.3746, 269.1425, FBS_W(0), 0, FBS_W(0), 0, FBS_W(0), 0);
    printf("[FBSCN] START scenario=%s bots=%d extra=%d maxplayers=%d", gScn, gN, gExtra, GetMaxPlayers());

    gTimer = SetTimer("FBS_Tick", 200, true);
    S_ResetEvents();

    if (!strcmp(gScn, "badname"))
    {
        // 1) printable, no blank: the plugin lets it through, the SERVER decides.
        gBots[0] = FakeBotCreate("Bad!Name", 0, 1958.0, 1343.0, 15.37, 0.0);
        S_Check(gBots[0] >= 0, "badname: plugin forwards a printable nickname to the server");
        // 2) nothing a server can ever accept: refused up front.
        S_Check(FakeBotCreate("ab", 0, 1958.0, 1343.0, 15.37, 0.0) == -1, "badname: 2 character nickname refused");
        S_Check(FakeBotCreate("with space", 0, 1958.0, 1343.0, 15.37, 0.0) == -1, "badname: blank in nickname refused");
        // 3) a good one next to it
        gBots[1] = FakeBotCreate("Valid_Name", 0, 1960.0, 1343.0, 15.37, 0.0);
        S_Check(gBots[1] >= 0, "badname: valid nickname accepted");
        S_Stage(0);
    }
    else if (!strcmp(gScn, "churn"))
    {
        gRound = 0;
        S_Stage(0);
    }
    else
    {
        new bad = S_CreateBots(gN);
        if (!strcmp(gScn, "overfill"))
        {
            // The plugin refuses what the server could not take anyway (it keeps a slot free).
            new want = gN - (gExtra - 1), detail[48];
            format(detail, sizeof detail, "refused=%d expected=%d", bad, want);
            S_Check(bad == want, "surplus FakeBotCreate calls are refused up front", detail);
        }
        else S_Check(bad == 0, "all FakeBotCreate calls accepted", "some creations failed");
        S_Stage(0);
    }
    return 1;
}

public OnGameModeExit()
{
    KillTimer(gTimer);
    return 1;
}

public OnPlayerRequestClass(playerid, classid)
{
    return 1;
}

public OnFakeBotConnect(botid)
{
    gEvConnect++;
    return 1;
}

public OnFakeBotSpawn(botid)
{
    gEvSpawn++;
    return 1;
}

new gEvReach;
public OnFakeBotReachDestination(botid)
{
    gEvReach++;
    return 1;
}

public OnFakeBotDisconnect(botid, reason)
{
    if (gDiscSeen < FBS_MAX) gDiscReason[gDiscSeen] = reason;
    gDiscSeen++;
    gEvDisc++;
    printf("[FBSCN] note: OnFakeBotDisconnect bot=%d reason=%d", botid, reason);
    return 1;
}

stock S_CheckHeading(pid, Float:want, const dir[], const leg[])
{
    new Float:a = 0.0, buf[96];
    GetPlayerFacingAngle(pid, a);
    new Float:d = a - want;
    while (d > 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    if (d < 0.0) d = -d;
    format(buf, sizeof buf, "walking %s (%s): server facing=%.1f expected=%.1f", dir, leg, a, want);
    S_Check(d < 12.0, buf, buf);
}

stock S_Finish()
{
    printf("[FBSCN] RESULT pass=%d fail=%d", gPass, gFail);
    print("[FBSCN] DONE");
    KillTimer(gTimer);
    SendRconCommand("exit");
}

forward FBS_Tick();
public FBS_Tick()
{
    new buf[128];

    // ======================================================== join
    if (!strcmp(gScn, "join"))
    {
        switch (gStage)
        {
            case 0:
            {
                if (gEvSpawn >= gN)
                {
                    printf("[FBSCN] all %d bots spawned in %d ms", gN, GetTickCount() - gT0);
                    format(buf, sizeof buf, "players=%d bots=%d", S_Connected(), gN);
                    S_Check(S_Connected() == gN, "server player count == number of bots", buf);
                    format(buf, sizeof buf, "npc=%d", S_NpcCount());
                    S_Check(S_NpcCount() == 0, "no player is an NPC", buf);
                    format(buf, sizeof buf, "FakeBotGetCount=%d", FakeBotGetCount());
                    S_Check(FakeBotGetCount() == gN, "FakeBotGetCount() == number of bots", buf);
                    format(buf, sizeof buf, "connect=%d spawn=%d", gEvConnect, gEvSpawn);
                    S_Check(gEvConnect == gN && gEvSpawn == gN, "one connect + one spawn event per bot", buf);
                    printf("[FBSCN] STEADY bots=%d players=%d max=%d hold=%d", gN, S_Connected(), GetMaxPlayers(), gExtra);
                    S_Stage(1);
                }
                else if (S_Timeout(120000 + gN * 800))
                {
                    format(buf, sizeof buf, "connect=%d spawn=%d disc=%d of %d", gEvConnect, gEvSpawn, gEvDisc, gN);
                    S_Fail("all bots joined", buf);
                    S_Finish();
                }
            }
            case 1:
            {
                if (S_Timeout(gExtra * 1000))
                {
                    format(buf, sizeof buf, "players=%d disconnect-events=%d", S_Connected(), gEvDisc);
                    S_Check(S_Connected() == gN && gEvDisc == 0, "everyone is still connected after the hold", buf);
                    S_DestroyAll();
                    S_Stage(2);
                }
            }
            case 2:
            {
                if (S_Connected() == 0 && FakeBotGetCount() == 0)
                {
                    S_Ok("all bots left; server is empty");
                    printf("[FBSCN] EMPTY players=%d", S_Connected());
                    S_Finish();
                }
                else if (S_Timeout(60000))
                {
                    format(buf, sizeof buf, "players=%d count=%d", S_Connected(), FakeBotGetCount());
                    S_Fail("all bots left; server is empty", buf);
                    S_Finish();
                }
            }
        }
        return 1;
    }

    // ======================================================== overfill
    if (!strcmp(gScn, "overfill"))
    {
        switch (gStage)
        {
            case 0:
            {
                if (gEvSpawn >= gExtra || S_Timeout(90000))
                {
                    // gExtra = player slots of the server. The plugin keeps one of them free.
                    format(buf, sizeof buf, "spawned=%d expected=%d", gEvSpawn, gExtra - 1);
                    S_Check(gEvSpawn == gExtra - 1, "bots fill all slots except the reserved one", buf);
                    format(buf, sizeof buf, "disconnect events=%d", gEvDisc);
                    S_Check(gEvDisc == 0, "no bot was refused or banned by the server", buf);
                    format(buf, sizeof buf, "players=%d", S_Connected());
                    S_Check(S_Connected() == gExtra - 1, "one slot is still free for a real player", buf);
                    format(buf, sizeof buf, "FakeBotGetCount=%d", FakeBotGetCount());
                    S_Check(FakeBotGetCount() == gExtra - 1, "FakeBotGetCount() == live bots", buf);
                    S_Check(S_NpcCount() == 0, "no player is an NPC");
                    S_Check(FakeBotGetReservedSlots() == 1, "reserved slots default to 1");
                    // the surplus creations were refused by the plugin itself
                    S_Check(FakeBotCreate("Surplus_Bot", 0, 1958.0, 1343.0, 15.37, 0.0) == -1, "one more bot is refused up front");
                    // opening it up by one slot lets exactly one more bot in
                    FakeBotSetReservedSlots(2);
                    S_Check(FakeBotGetReservedSlots() == 2, "reserved slots can be raised");
                    gT0 = GetTickCount();
                    printf("[FBSCN] FULLHOUSE players=%d", S_Connected());
                    S_Stage(1);
                }
            }
            case 1:
            {
                if (S_Timeout(gN > 0 ? 6000 : 6000))
                {
                    S_DestroyAll();
                    S_Stage(2);
                }
            }
            case 2:
            {
                if (S_Connected() == 0 && FakeBotGetCount() == 0)
                {
                    S_Ok("all bots left; server is empty");
                    S_Finish();
                }
                else if (S_Timeout(60000))
                {
                    S_Fail("all bots left; server is empty");
                    S_Finish();
                }
            }
        }
        return 1;
    }

    // ======================================================== badname
    if (!strcmp(gScn, "badname"))
    {
        switch (gStage)
        {
            case 0:
            {
                if ((gEvSpawn >= 1 && gEvDisc >= 1) || S_Timeout(45000))
                {
                    format(buf, sizeof buf, "spawn=%d disc=%d", gEvSpawn, gEvDisc);
                    S_Check(gEvSpawn == 1 && gEvDisc == 1, "one bot joined, one was rejected by the server", buf);
                    format(buf, sizeof buf, "reason=%d", gDiscReason[0]);
                    S_Check(gDiscSeen >= 1 && gDiscReason[0] == -2002, "server nickname rejection is reported as -2002", buf);
                    format(buf, sizeof buf, "players=%d", S_Connected());
                    S_Check(S_Connected() == 1, "only the valid bot is on the server", buf);
                    S_DestroyAll();
                    S_Stage(1);
                }
            }
            case 1:
            {
                if (S_Connected() == 0 && FakeBotGetCount() == 0)
                {
                    S_Ok("all bots left; server is empty");
                    S_Finish();
                }
                else if (S_Timeout(30000))
                {
                    S_Fail("all bots left; server is empty");
                    S_Finish();
                }
            }
        }
        return 1;
    }

    // ======================================================== facing
    // A bot walks in 8 compass directions; the SERVER must report the matching heading.
    // This proves that the rotation the bot puts into its sync packets is decoded correctly.
    if (!strcmp(gScn, "facing"))
    {
        static const Float:dirX[8] = { 0.0,  1.0, 1.0,  1.0,  0.0, -1.0, -1.0, -1.0};
        static const Float:dirY[8] = { 1.0,  1.0, 0.0, -1.0, -1.0, -1.0,  0.0,  1.0};
        static const dirName[8][3] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        // SA-MP headings: 0 = north (+y), 90 = west (-x), 180 = south, 270 = east
        static const Float:dirWant[8] = {0.0, 315.0, 270.0, 225.0, 180.0, 135.0, 90.0, 45.0};
        new pid = FakeBotGetPlayerID(gBots[0]);
        switch (gStage)
        {
            case 0:
            {
                if (gEvSpawn >= 1 && pid != INVALID_PLAYER_ID)
                {
                    new Float:x, Float:y, Float:z;
                    GetPlayerPos(pid, x, y, z);
                    gFx = x; gFy = y; gFz = z;
                    gRound = 0;
                    S_Stage(1);
                }
                else if (S_Timeout(30000)) { S_Fail("bot spawned"); S_Finish(); }
            }
            case 1: // walk out from the centre
            {
                gEvReach = 0;
                FakeBotGoTo(gBots[0], gFx + dirX[gRound] * 12.0, gFy + dirY[gRound] * 12.0, gFz, 6.0, FAKEBOTS_MOVE_RUN);
                S_Stage(2);
            }
            case 2: // outbound: the server must report the heading of the walk
            {
                if (gEvReach >= 1)
                {
                    S_CheckHeading(pid, dirWant[gRound], dirName[gRound], "out");
                    gEvReach = 0;
                    FakeBotGoTo(gBots[0], gFx, gFy, gFz, 6.0, FAKEBOTS_MOVE_RUN);
                    S_Stage(4);
                }
                else if (S_Timeout(25000)) { S_Fail("walk finished", "no OnFakeBotReachDestination"); S_Finish(); }
            }
            case 4: // back to the centre: the opposite heading
            {
                if (gEvReach >= 1)
                {
                    new Float:back = dirWant[gRound] + 180.0;
                    if (back >= 360.0) back -= 360.0;
                    S_CheckHeading(pid, back, dirName[gRound], "back");
                    gRound++;
                    if (gRound >= 8) { S_DestroyAll(); S_Stage(3); }
                    else S_Stage(1);
                }
                else if (S_Timeout(25000)) { S_Fail("walk back finished", "no OnFakeBotReachDestination"); S_Finish(); }
            }
            case 3:
            {
                if (S_Connected() == 0 && FakeBotGetCount() == 0) { S_Ok("all bots left; server is empty"); S_Finish(); }
                else if (S_Timeout(30000)) { S_Fail("all bots left; server is empty"); S_Finish(); }
            }
        }
        return 1;
    }

    // ======================================================== churn
    if (!strcmp(gScn, "churn"))
    {
        switch (gStage)
        {
            case 0: // start a round
            {
                S_ResetEvents();
                new bad = S_CreateBots(gN, "CH");
                format(buf, sizeof buf, "round %d: all creations accepted", gRound + 1);
                S_Check(bad == 0, buf);
                S_Stage(1);
            }
            case 1: // wait for the full house
            {
                if (gEvSpawn >= gN)
                {
                    format(buf, sizeof buf, "players=%d bots=%d", S_Connected(), gN);
                    new name[48];
                    format(name, sizeof name, "round %d: player count == bots", gRound + 1);
                    S_Check(S_Connected() == gN, name, buf);
                    S_DestroyAll();
                    S_Stage(2);
                }
                else if (S_Timeout(120000))
                {
                    format(buf, sizeof buf, "round %d: connect=%d spawn=%d disc=%d", gRound + 1, gEvConnect, gEvSpawn, gEvDisc);
                    S_Fail("churn round joined", buf);
                    S_Finish();
                }
            }
            case 2: // wait until the server is empty again
            {
                if (S_Connected() == 0 && FakeBotGetCount() == 0)
                {
                    gRound++;
                    printf("[FBSCN] ROUND %d/%d done (server empty again)", gRound, gExtra);
                    if (gRound >= gExtra)
                    {
                        S_Ok("all churn rounds finished");
                        S_Finish();
                    }
                    else S_Stage(0);
                }
                else if (S_Timeout(60000))
                {
                    format(buf, sizeof buf, "players=%d count=%d", S_Connected(), FakeBotGetCount());
                    S_Fail("churn round emptied the server", buf);
                    S_Finish();
                }
            }
        }
        return 1;
    }

    S_Fail("unknown scenario", gScn);
    S_Finish();
    return 1;
}
