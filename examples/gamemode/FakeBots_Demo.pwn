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
#define DEMO_RECORD_MAX     (1800)
#define DEMO_RECORD_INTERVAL (250)
#define DEMO_MAX            (1000)

new const gFirst[][] =
{
    "Abolfazl", "Abbas", "Abdollah", "Abed", "Adel", "Adib", "Afshin", "Ahmad",
    "Akbar", "Alireza", "Ali", "Amir", "Amirali", "Amirabbas", "Amirreza", "Amirhossein",
    "Amin", "Anoushirvan", "Arad", "Arash", "Ardeshir", "Aria", "Arman", "Armin",
    "Arsha", "Arsham", "Artin", "Arvin", "Ashkan", "Aslan", "Ata", "Atabak",
    "Babak", "Bahador", "Bahman", "Bahram", "Behbod", "Behdad", "Behnam", "Behrouz",
    "Behrang", "Bijan", "Borna", "Bardia", "Dariush", "Danial", "Danyal", "Davood",
    "Ehsan", "Ebrahim", "Edris", "Erfan", "Eskandar", "Esmail", "Farhad", "Fariborz",
    "Farid", "Farshad", "Farshid", "Farzad", "Fardin", "Faramarz", "Fereydoun", "Firouz",
    "Foad", "Ghasem", "Gholamreza", "Hadi", "Hamed", "Hamid", "Hamidreza", "Hooman",
    "Hormoz", "Hossein", "Houshang", "Iman", "Iraj", "Javad", "Jahan", "Kamal",
    "Kamran", "Kaveh", "Kian", "Kianoush", "Kourosh", "Kasra", "Keyvan", "Khashayar",
    "Khosrow", "Kianmehr", "Kouhyar", "Majid", "Mahmood", "Mani", "Mansour", "Mahan",
    "Mehran", "Mehrdad", "Meysam", "Milad", "Mirza", "Mohammad", "Mohammadali", "Mohammadreza",
    "Mohsen", "Morteza", "Mostafa", "Nader", "Nima", "Omid", "Parham", "Parsa",
    "Pedram", "Pejman", "Pirooz", "Pouya", "Pouria", "Ramin", "Reza", "Roozbeh",
    "Saeed", "Saman", "Samyar", "Salar", "Saleh", "Sasan", "Shahab", "Shahin",
    "Shayan", "Shervin", "Siavash", "Soheil", "Soroush", "Taher", "Taha", "Vahid",
    "Vafa", "Yashar", "Yasin", "Younes", "Yousef", "Zartosht", "Ziya", "Alvand",
    "Arvinmehr", "Ashkanmehr", "Behnoud", "Behzad", "Dariushmehr", "Farshadmehr", "Farzan", "Fardinmehr",
    "Hoomanmehr", "Kouroshmehr", "Mehrshad", "Mehrpouya", "Navid", "Nikan", "Nikanmehr", "Pendar",
    "Parsaad", "Parviz", "Payam", "Payman", "Pooria", "Radman", "Radan", "Raminmehr",
    "Ramtin", "Rayan", "Rojan", "Sam", "Samanmehr", "Sepand", "Shahram", "Shahrokh",
    "Shapour", "Sina", "Sirous", "Sohrab", "Tirdad", "Touraj", "Vahidreza", "Viyan",
    "Yavar", "Yeganeh", "Zubin", "Aida", "Afsaneh", "Akram", "Arezoo", "Arghavan",
    "Atieh", "Atoosa", "Azadeh", "Azar", "Bahar", "Bahareh", "Banafsheh", "Baran",
    "Behnaz", "Behnoush", "Bita", "Donya", "Dorsa", "Elaheh", "Elham", "Elmira",
    "Elnaz", "Farnaz", "Fariba", "Farideh", "Farzaneh", "Fatemeh", "Fereshteh", "Forough",
    "Ghazal", "Ghazaleh", "Golnar", "Golnaz", "Golsa", "Hanieh", "Hana", "Hasti",
    "Hediyeh", "Homa", "Hoori", "Ida", "Jaleh", "Kiana", "Kimia", "Katayoun",
    "Ladan", "Leila", "Laleh", "Mahsa", "Mahnaz", "Mahtab", "Mahin", "Mandana",
    "Mania", "Maral", "Maryam", "Mastaneh", "Melika", "Mina", "Minoo", "Mojgan",
    "Mona", "Monireh", "Narges", "Nasim", "Nazanin", "Nazli", "Negar", "Negin",
    "Neda", "Niloufar", "Niloofar", "Noushin", "Pegah", "Parisa", "Parastoo", "Pardis",
    "Pariya", "Pantea", "Parnian", "Pooneh", "Rana", "Rojin", "Roya", "Roxana",
    "Sahar", "Saba", "Samin", "Samira", "Sanaz", "Sepideh", "Setareh", "Shabnam",
    "Shadi", "Shaghayegh", "Shahrzad", "Shirin", "Shiva", "Shohreh", "Somayeh", "Soraya",
    "Tara", "Taraneh", "Tannaz", "Tina", "Yasaman", "Yasmin", "Yekta", "Zahra",
    "Ziba", "Zohreh", "Atousa", "Yas"
};

new const gLast[][] =
{
    "Abbasi", "Abdollahi", "Abedini", "Afshar", "Aghaei", "Ahmadi", "Akbari", "Alavi",
    "Alizadeh", "Amiri", "Ansari", "Arabi", "Asadi", "Asgari", "Aslani", "Azadi",
    "Azimi", "Bagheri", "Bahadori", "Bahrami", "Bakhtiari", "Barati", "Bayat", "Behnam",
    "Behzadi", "Beigi", "Borhani", "Bostani", "Chegini", "Dadgar", "Daryaei", "Dehghan",
    "Ebrahimi", "Emadi", "Eskandari", "Fallahi", "Fathi", "Farahani", "Farhadi", "Farrokhi",
    "Farzan", "Fazeli", "Firoozi", "Ghaedi", "Ghasemi", "Gharib", "Ghanbari", "Gharibi",
    "Gholami", "Ghorbani", "Golchin", "Golestani", "Goodarzi", "Habibi", "Haghgoo", "Hajizadeh",
    "Hakimi", "Hamedi", "Hashemi", "Hassani", "Heidari", "Hemmati", "Heshmati", "Heydari",
    "Hosseini", "Jafari", "Jalali", "Javanmard", "Javid", "Jazini", "Kalantari", "Kamali",
    "Karimi", "Kazemi", "Keshavarz", "Khalili", "Kiani", "Khosravi", "Khorsandi", "Khorrami",
    "Kord", "Kousha", "Lajevardi", "Lahiji", "Mahdavi", "Mahmoodi", "Majidi", "Maleki",
    "Mansouri", "Marashi", "Matin", "Mazaheri", "Mehrabi", "Mirzaei", "Mohammadi", "Mohseni",
    "Moradi", "Mousavi", "Movahed", "Najafi", "Naderi", "Najjar", "Naseri", "Nazari",
    "Nikbakht", "Niknam", "Nouri", "Omidi", "Pakdel", "Pakzad", "Parsa", "Parvizi",
    "Paydar", "Pezeshki", "Pourmand", "Pourreza", "Rahimi", "Rahmani", "Rahbar", "Rasouli",
    "Rastegar", "Razi", "Rezaei", "Riahi", "Rigi", "Rouhani", "Rostami", "Saberi",
    "Safari", "Safavi", "Sadeghi", "Sadri", "Saeedi", "Safaei", "Salehi", "Salimi",
    "Samadi", "Sanei", "Sarabi", "Sarraf", "Sayadi", "Sepahvand", "Shahbazi", "Shahidi",
    "Shakiba", "Sharifi", "Shariati", "Shams", "Shamsi", "Sheikhi", "Shiri", "Shokri",
    "Shokuhi", "Shojaei", "Soltani", "Soroush", "Taheri", "Taimouri", "Teymouri", "Torabi",
    "Vafaei", "Vahidi", "Vakili", "Vaziri", "Yaghoubi", "Yahyavi", "Yazdani", "Yeganeh",
    "Yousefi", "Zahedi", "Zakeri", "Zand", "Zarei", "Zare", "Zarrin", "Zolfaghari",
    "Moini", "Motamedi", "Mokhtari", "Mohebi", "Moslemi", "Namazi", "Namin", "Pashaei",
    "Qaderi", "Roshan", "Saadat", "Shabani", "Shafiei", "Shahriari", "Shahrokhi", "Soleimani",
    "Taghavi", "Vatanparast", "Azar", "Arjomand", "Ashtiani", "Asefi", "Bahmani", "BaniAsadi",
    "Bakhshi", "Bakhshandeh", "Balochi", "Barzegar", "Bashiri", "Bastani", "Bavar", "Beheshti",
    "Bina", "Bonyadi", "Bozorgi", "Bukhari", "Chavoshi", "Davani", "Diba", "Didehvar",
    "Dindarfar", "Fadaei", "Fakhari", "Farzanegan", "Fattahi", "Fouladi", "Ghaderi", "Ghassemi",
    "Gharagozlou", "Ghorashi", "Goudarzi", "Hajian", "Hamzehloo", "Hanifi", "Hasanzadeh", "Hedayati",
    "Honarmand", "Honari", "Jabbari", "Jahangiri", "Jahani", "Jalaeian", "Jamali", "Jangi",
    "Jannati", "Jelodar", "Kachuei", "Kalhor", "Kardan", "Kargar", "Karkhaneh", "Karmand",
    "Kaveh", "Kazerouni", "Khadem", "Khademi", "Khalatbari", "Khani", "Khatibi", "Khavari",
    "Khazaei", "Kheradmand", "Khodadadi", "Khodarahmi", "Khorram", "Khorshidi", "Khoshdel", "Khoshkholgh",
    "Kianfar", "Kianmehr", "Kiyani", "Kobraei", "Koushki", "Kowsari", "Labafi", "Lorestani",
    "Madani", "Mahdizadeh", "Mahjour", "Majlesi", "Makaremi", "Malekian", "MansouriRad", "Marandi",
    "Marjani", "Marvasti", "Masoumi", "MehrabiRad", "Mehrjui", "Mehrvarz", "Meskini", "Moghaddam",
    "Moghimi", "Mohajer", "MohammadiRad", "Mohammadzadeh", "Mohammadian", "Mohammadiyan", "Mohsenian", "MokhtariRad",
    "Monfared", "MoradiRad", "Moradiyan", "MoradiZadeh"
};

new const gSay[][] =
{
    "salam hamegi", "che khabar bacheha", "ki hale race dare?", "berim dor bezanim", "server kheili khoobe", "man alan miam", "koja mirin?", "damet garm",
    "ghabel nadare", "che hal mide", "boro bia", "vay lag", "man raftam", "bezan bere", "hamechi arume", "khob bashi",
    "salam dadash", "salam abji", "chetori?", "emshab che khabar?", "baba berim", "man peyadetam", "boro jolo", "inja khoobe",
    "be server khosh oomadi", "kasi online hast?", "ki game mizane?", "man tayaram", "motor daram", "bia race", "brim LV", "Los Santos khoobe",
    "San Fierro che khabar?", "Las Venturas jaamune", "bacheha bazi?", "az invar boro", "rah ro baz kon", "man dor mizanim", "boro fast", "ye dast bazi?",
    "khastam", "sare karam", "badan miam", "khodahafez", "mersi az hame", "dametoon garm", "che kasi hoste?", "man ino didam",
    "vay chi shod?", "khob shod", "alan online shodam", "man amadeam", "berim", "eyval", "baba chiye?", "koja boodin?",
    "sobh bekheir", "shab bekheir", "rooz bekheir", "khoobi?", "khobam to chetori?", "ghashange", "ajab serveri", "che mapie",
    "inja koja mishe?", "man rah ro baladam", "yalla", "bede berim", "beri ke beresim", "man bar migardam", "bargard", "hamash khosh bash",
    "be salamat", "boro kenar", "hamechi okaye", "ye lahze sabr kon", "komakam mikoni?", "man omadama", "che shod pas?", "baraye man okaye",
    "faghat ye dast", "berim downtown", "emrooz khosh migzare", "dastet dard nakone", "cheshm", "mobarak bashe", "bacheha koja hastin?", "be man ham begid",
    "man haminjaam", "hamin alan", "kheili khoob bood", "dige chi?", "inja bemoon", "dor nazan", "baba arum", "khodeto jam kon",
    "ma berim", "man bazam miam", "hame tayar?", "bazi shoroo shod"
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
new gBotNames[DEMO_MAX][MAX_PLAYER_NAME + 1];

new Float:gRecordX[DEMO_RECORD_MAX];
new Float:gRecordY[DEMO_RECORD_MAX];
new Float:gRecordZ[DEMO_RECORD_MAX];
new Float:gRecordAngle[DEMO_RECORD_MAX];
new gRecordMode[DEMO_RECORD_MAX]; // 0 = on foot, 1 = in a vehicle
new gRecordVehicleModel[DEMO_RECORD_MAX];
new gRecordSkinFrames[DEMO_RECORD_MAX];
new gRecordCount;
new gRecordOwner = INVALID_PLAYER_ID;
new bool:gRecording;
new gRecordTimer;

new gReplayBot = FAKEBOTS_INVALID_ID;
new gReplayVehicle = FAKEBOTS_INVALID_ID;
new gReplayIndex;
new gReplayTimer;
new bool:gReplayWaiting;
new bool:gReplaying;
new gReplaySkin = -1;

forward Demo_RecordTick();
forward Demo_ReplayTick();

stock Demo_ReadConfig()
{
    new File:f = fopen(DEMO_CFG, io_read);
    if (!f) return 0;

    new line[32];
    if (fread(f, line) && strval(line) > 0) gStartBots = strval(line);
    if (fread(f, line) && strval(line) > 0) gReportSeconds = strval(line);
    if (fread(f, line)) gAutoExit = strval(line);
    fclose(f);

    if (gStartBots > DEMO_MAX) gStartBots = DEMO_MAX;
    if (gReportSeconds < 1) gReportSeconds = 15;
    if (gAutoExit < 0) gAutoExit = 0;
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


stock Demo_NameInUse(const name[])
{
    for (new i = 0; i < gBotCount; i++)
        if (!strcmp(gBotNames[i], name, true))
            return 1;

    for (new playerid = 0, maxPlayers = GetMaxPlayers(); playerid < maxPlayers; playerid++)
    {
        if (!IsPlayerConnected(playerid))
            continue;

        new currentName[MAX_PLAYER_NAME + 1];
        GetPlayerName(playerid, currentName, sizeof(currentName));
        if (!strcmp(currentName, name, true))
            return 1;
    }
    return 0;
}

stock Demo_GenerateName(name[], size)
{
    for (new attempt = 0; attempt < 100; attempt++)
    {
        new first[32], last[32];
        format(first, sizeof(first), "%s", gFirst[random(sizeof(gFirst))]);
        format(last, sizeof(last), "%s", gLast[random(sizeof(gLast))]);

        if (strlen(first) + strlen(last) + 1 > MAX_PLAYER_NAME)
            continue;

        format(name, size, "%s_%s", first, last);
        if (Demo_NameInUse(name))
            continue;

        return 1;
    }
    return 0;
}

stock Demo_TrackBot(botid, const name[])
{
    if (botid == FAKEBOTS_INVALID_ID || gBotCount >= DEMO_MAX)
        return FAKEBOTS_INVALID_ID;

    gBots[gBotCount] = botid;
    format(gBotNames[gBotCount], MAX_PLAYER_NAME + 1, "%s", name);
    gBotCount++;
    return botid;
}

stock Demo_AddBot()
{
    if (gBotCount >= DEMO_MAX)
        return FAKEBOTS_INVALID_ID;

    new name[MAX_PLAYER_NAME + 1];
    if (!Demo_GenerateName(name, sizeof(name)))
        return FAKEBOTS_INVALID_ID;

    new id = FakeBotCreate(
        name,
        20 + random(260),
        2030.0 + float(random(60) - 30),
        1000.0 + float(random(60) - 30),
        10.8,
        float(random(360))
    );

    if (id == FAKEBOTS_INVALID_ID)
        return FAKEBOTS_INVALID_ID;

    return Demo_TrackBot(id, name);
}

stock Demo_ForgetBot(botid)
{
    for (new i = 0; i < gBotCount; i++)
    {
        if (gBots[i] != botid)
            continue;

        new last = --gBotCount;
        if (i != last)
        {
            gBots[i] = gBots[last];
            format(gBotNames[i], MAX_PLAYER_NAME + 1, "%s", gBotNames[last]);
        }
        gBotNames[last][0] = '\0';
        return 1;
    }
    return 0;
}

stock Demo_NextToken(const source[], &index, dest[], size)
{
    while (source[index] == ' ' || source[index] == '\t')
        index++;

    new length;
    while (source[index] > ' ' && length < size - 1)
    {
        dest[length++] = source[index++];
    }
    dest[length] = '\0';
    return length;
}

stock Demo_GetSpawnedBotPlayer(botid)
{
    if (!FakeBotIsValid(botid) || FakeBotGetState(botid) != FAKEBOTS_STATE_SPAWNED)
        return FAKEBOTS_INVALID_ID;

    return FakeBotGetPlayerID(botid);
}

stock Demo_StopBot(botid)
{
    if (!FakeBotIsValid(botid))
        return 0;

    FakeBotSetIdleWander(botid, false);
    FakeBotStopFollowing(botid);
    FakeBotStopCombat(botid);
    FakeBotStopMoving(botid);
    FakeBotClearWaypoints(botid);
    FakeBotStopDriving(botid);
    FakeBotClearDriveWaypoints(botid);
    return 1;
}

stock Demo_StoreRecordFrame(playerid)
{
    if (gRecordCount >= DEMO_RECORD_MAX || !IsPlayerConnected(playerid))
        return 0;

    new frame = gRecordCount;
    GetPlayerPos(playerid, gRecordX[frame], gRecordY[frame], gRecordZ[frame]);
    GetPlayerFacingAngle(playerid, gRecordAngle[frame]);
    gRecordMode[frame] = 0;
    gRecordVehicleModel[frame] = 0;

    new playerState = GetPlayerState(playerid);
    if (playerState == PLAYER_STATE_DRIVER || playerState == PLAYER_STATE_PASSENGER)
    {
        new vehicleid = GetPlayerVehicleID(playerid);
        if (vehicleid != INVALID_VEHICLE_ID)
        {
            GetVehiclePos(vehicleid, gRecordX[frame], gRecordY[frame], gRecordZ[frame]);
            GetVehicleZAngle(vehicleid, gRecordAngle[frame]);
            gRecordMode[frame] = 1;
            gRecordVehicleModel[frame] = GetVehicleModel(vehicleid);
        }
    }

    gRecordSkinFrames[frame] = GetPlayerSkin(playerid);
    gRecordCount++;
    return 1;
}

stock Demo_EndRecording(bool:captureLast)
{
    if (!gRecording)
        return 0;

    new owner = gRecordOwner;
    if (captureLast && owner != INVALID_PLAYER_ID && IsPlayerConnected(owner) && gRecordCount < DEMO_RECORD_MAX)
        Demo_StoreRecordFrame(owner);

    gRecording = false;
    if (gRecordTimer != 0)
    {
        KillTimer(gRecordTimer);
        gRecordTimer = 0;
    }

    if (owner != INVALID_PLAYER_ID && IsPlayerConnected(owner))
    {
        new msg[128];
        if (gRecordCount >= DEMO_RECORD_MAX)
            format(msg, sizeof(msg), "Record limit reached. Saved %d movement frames in memory.", gRecordCount);
        else
            format(msg, sizeof(msg), "Recording stopped. Saved %d movement frames in memory.", gRecordCount);
        SendClientMessage(owner, 0x33CC66FF, msg);
    }

    return 1;
}

public Demo_RecordTick()
{
    if (!gRecording)
        return 0;

    if (gRecordOwner == INVALID_PLAYER_ID || !IsPlayerConnected(gRecordOwner) || FakeBotIsPlayer(gRecordOwner))
    {
        Demo_EndRecording(false);
        return 0;
    }

    if (!Demo_StoreRecordFrame(gRecordOwner) || gRecordCount >= DEMO_RECORD_MAX)
    {
        Demo_EndRecording(false);
        return 0;
    }
    return 1;
}

stock Float:Demo_ReplaySpeed(index, bool:driving)
{
    if (index <= 0)
    {
        if (driving) return 20.0;
        return 1.0;
    }

    new Float:dx = gRecordX[index] - gRecordX[index - 1];
    new Float:dy = gRecordY[index] - gRecordY[index - 1];
    new Float:dz = gRecordZ[index] - gRecordZ[index - 1];
    new Float:speed = floatsqroot(dx * dx + dy * dy + dz * dz) * 4.0;

    if (driving)
    {
        if (speed < 8.0) speed = 8.0;
        if (speed > 55.0) speed = 55.0;
    }
    else
    {
        if (speed < 0.6) speed = 0.6;
        if (speed > 7.0) speed = 7.0;
    }
    return speed;
}

stock Demo_DestroyReplayVehicle()
{
    if (gReplayVehicle == FAKEBOTS_INVALID_ID)
        return 0;

    if (FakeBotIsValid(gReplayBot) && FakeBotGetState(gReplayBot) == FAKEBOTS_STATE_SPAWNED)
        FakeBotRemoveFromVehicle(gReplayBot);

    if (IsValidVehicle(gReplayVehicle))
        DestroyVehicle(gReplayVehicle);

    gReplayVehicle = FAKEBOTS_INVALID_ID;
    return 1;
}

stock Demo_StopReplay(bool:destroyBot)
{
    if (gReplayTimer != 0)
    {
        KillTimer(gReplayTimer);
        gReplayTimer = 0;
    }
    gReplaying = false;
    gReplayWaiting = false;

    if (gReplayBot == FAKEBOTS_INVALID_ID || !FakeBotIsValid(gReplayBot))
    {
        Demo_DestroyReplayVehicle();
        if (destroyBot)
            gReplayBot = FAKEBOTS_INVALID_ID;
        return 0;
    }

    Demo_StopBot(gReplayBot);
    Demo_DestroyReplayVehicle();

    if (destroyBot)
    {
        new oldBot = gReplayBot;
        gReplayBot = FAKEBOTS_INVALID_ID;
        gReplaySkin = -1;
        FakeBotDestroy(oldBot);
    }
    return 1;
}

stock Demo_StartReplay()
{
    if (gRecording)
        return 0;
    if (gRecordCount < 2)
        return 0;

    if (gReplayBot != FAKEBOTS_INVALID_ID && FakeBotIsValid(gReplayBot))
        Demo_StopReplay(true);

    if (gBotCount >= DEMO_MAX)
        return 0;

    new name[MAX_PLAYER_NAME + 1];
    if (!Demo_GenerateName(name, sizeof(name)))
        return 0;

    new botid = FakeBotCreate(
        name,
        gRecordSkinFrames[0],
        gRecordX[0],
        gRecordY[0],
        gRecordZ[0],
        gRecordAngle[0]
    );
    if (botid == FAKEBOTS_INVALID_ID)
        return 0;

    if (Demo_TrackBot(botid, name) == FAKEBOTS_INVALID_ID)
    {
        FakeBotDestroy(botid);
        return 0;
    }

    gReplayBot = botid;
    gReplayVehicle = FAKEBOTS_INVALID_ID;
    gReplayIndex = 0;
    gReplaySkin = gRecordSkinFrames[0];
    gReplayWaiting = true;
    gReplaying = false;
    return 1;
}

stock Demo_ApplyReplayFrame(index)
{
    if (!FakeBotIsValid(gReplayBot) || FakeBotGetState(gReplayBot) != FAKEBOTS_STATE_SPAWNED)
        return 0;

    new playerid = FakeBotGetPlayerID(gReplayBot);
    if (playerid == FAKEBOTS_INVALID_ID)
        return 0;

    if (gRecordSkinFrames[index] != gReplaySkin)
    {
        SetPlayerSkin(playerid, gRecordSkinFrames[index]);
        gReplaySkin = gRecordSkinFrames[index];
    }

    if (gRecordMode[index] == 1)
    {
        if (gReplayVehicle == FAKEBOTS_INVALID_ID
            || !IsValidVehicle(gReplayVehicle)
            || GetPlayerState(playerid) != PLAYER_STATE_DRIVER
            || GetVehicleModel(gReplayVehicle) != gRecordVehicleModel[index])
        {
            Demo_DestroyReplayVehicle();
            gReplayVehicle = FakeBotPutInNewVehicle(
                gReplayBot,
                gRecordVehicleModel[index],
                gRecordX[index],
                gRecordY[index],
                gRecordZ[index],
                gRecordAngle[index],
                1,
                1
            );
            if (gReplayVehicle == FAKEBOTS_INVALID_ID)
                return 0;
        }

        FakeBotSetIdleWander(gReplayBot, false);
        FakeBotDriveTo(
            gReplayBot,
            gRecordX[index],
            gRecordY[index],
            gRecordZ[index],
            Demo_ReplaySpeed(index, true)
        );
    }
    else
    {
        if (gReplayVehicle != FAKEBOTS_INVALID_ID)
            Demo_DestroyReplayVehicle();

        new Float:speed = Demo_ReplaySpeed(index, false);
        new movetype = FAKEBOTS_MOVE_WALK;
        if (speed >= 4.0) movetype = FAKEBOTS_MOVE_SPRINT;
        else if (speed >= 2.0) movetype = FAKEBOTS_MOVE_RUN;

        FakeBotSetIdleWander(gReplayBot, false);
        FakeBotGoTo(gReplayBot, gRecordX[index], gRecordY[index], gRecordZ[index], speed, movetype);
    }
    return 1;
}

public Demo_ReplayTick()
{
    if (!gReplaying || gReplayBot == FAKEBOTS_INVALID_ID || !FakeBotIsValid(gReplayBot))
    {
        if (gReplayTimer != 0)
        {
            KillTimer(gReplayTimer);
            gReplayTimer = 0;
        }
        gReplaying = false;
        return 0;
    }

    if (gReplayIndex >= gRecordCount)
    {
        gReplaying = false;
        if (gReplayTimer != 0)
        {
            KillTimer(gReplayTimer);
            gReplayTimer = 0;
        }

        if (FakeBotIsValid(gReplayBot) && FakeBotGetState(gReplayBot) == FAKEBOTS_STATE_SPAWNED)
        {
            FakeBotStopMoving(gReplayBot);
            FakeBotStopDriving(gReplayBot);
        }

        if (gRecordOwner != INVALID_PLAYER_ID && IsPlayerConnected(gRecordOwner))
            SendClientMessage(gRecordOwner, 0x33CC66FF, "Recorded movement playback finished.");
        return 0;
    }

    if (!Demo_ApplyReplayFrame(gReplayIndex))
    {
        Demo_StopReplay(false);
        return 0;
    }

    gReplayIndex++;
    return 1;
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
    if (gTimer != 0) KillTimer(gTimer);
    if (gRecordTimer != 0) KillTimer(gRecordTimer);
    if (gReplayTimer != 0) KillTimer(gReplayTimer);
    gRecording = false;
    gReplaying = false;
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

public OnPlayerDisconnect(playerid, reason)
{
    if (gRecording && gRecordOwner == playerid)
        Demo_EndRecording(false);
    if (gRecordOwner == playerid)
        gRecordOwner = INVALID_PLAYER_ID;
    return 1;
}

// ---------------------------------------------------------------------------
//  Bot events
// ---------------------------------------------------------------------------
public OnFakeBotSpawn(botid)
{
    if (botid == gReplayBot)
    {
        FakeBotSetIdleWander(botid, false);
        if (gReplayWaiting)
        {
            gReplayWaiting = false;
            gReplaying = true;
            gReplayIndex = 0;
            if (gReplayTimer != 0) KillTimer(gReplayTimer);
            gReplayTimer = SetTimer("Demo_ReplayTick", DEMO_RECORD_INTERVAL, true);
            Demo_ReplayTick();
        }
        return 1;
    }

    FakeBotSetIdleWander(botid, true, 25.0, 1.0);
    return 1;
}

public OnFakeBotDisconnect(botid, reason)
{
    if (botid == gReplayBot)
    {
        if (gReplayTimer != 0) KillTimer(gReplayTimer);
        gReplayTimer = 0;
        gReplaying = false;
        gReplayWaiting = false;
        gReplayBot = FAKEBOTS_INVALID_ID;
        if (gReplayVehicle != FAKEBOTS_INVALID_ID && IsValidVehicle(gReplayVehicle))
            DestroyVehicle(gReplayVehicle);
        gReplayVehicle = FAKEBOTS_INVALID_ID;
    }

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
    new cmd[32], token[64], idx;
    Demo_NextToken(cmdtext, idx, cmd, sizeof(cmd));
    if (!cmd[0])
        return 0;

    if (!strcmp(cmd, "/help", true))
    {
        SendClientMessage(playerid, 0xFFCC00FF, "=== FakeBots demo commands ===");
        SendClientMessage(playerid, 0xFFFFFFFF, "/botcount  /botlist [page]  /bots <1-50>  /botsclear  /botsfollow  /botsstop");
        SendClientMessage(playerid, 0xFFFFFFFF, "/botcar  /botsay <text>  /botwalk <id> <x> <y> <z> [speed]");
        SendClientMessage(playerid, 0xFFFFFFFF, "/botdrive <id> <x> <y> <z> [speed]  /botfollow <id>  /botwander <id> [radius]");
        SendClientMessage(playerid, 0xFFFFFFFF, "/botstop <id>  /botremove <id>  /botrespawn <id>  /botpos <id>");
        SendClientMessage(playerid, 0xFFFFFFFF, "/botskin <id> <skin>  /bothealth <id> <hp>  /botweapon <id> <weapon> <ammo>");
        SendClientMessage(playerid, 0xFFFFFFFF, "/botpool <0|1>  /bottick <10-1000>");
        SendClientMessage(playerid, 0x33CC66FF, "/record  /stoprecord  /runrecord  /stopreplay  /recordstatus");
        SendClientMessage(playerid, 0xAAAAAAFF, "Record captures a sampled movement path (on foot + vehicle), stored in memory.");
        return 1;
    }

    if (!strcmp(cmd, "/botcount", true))
    {
        new msg[128];
        format(msg, sizeof(msg), "Players: %d (bots: %d, real players: %d, slots: %d)", Demo_AllPlayers(), FakeBotGetCount(), Demo_RealPlayers(), GetMaxPlayers());
        SendClientMessage(playerid, 0xFFFFFFFF, msg);
        return 1;
    }


    if (!strcmp(cmd, "/botlist", true))
    {
        new pageText[16], page;
        if (Demo_NextToken(cmdtext, idx, pageText, sizeof(pageText)))
            page = strval(pageText);
        if (page < 0)
            return SendClientMessage(playerid, 0xFF6666FF, "Page number cannot be negative."), 1;

        if (gBotCount == 0)
            return SendClientMessage(playerid, 0xFFFFFFFF, "No bots are currently tracked."), 1;

        new pages = (gBotCount + 9) / 10;
        if (page >= pages)
            return SendClientMessage(playerid, 0xFF6666FF, "That bot-list page does not exist."), 1;

        new msg[128];
        format(msg, sizeof(msg), "Bot list page %d/%d (total %d)", page + 1, pages, gBotCount);
        SendClientMessage(playerid, 0xFFCC00FF, msg);

        for (new i = page * 10; i < gBotCount && i < (page + 1) * 10; i++)
        {
            if (!FakeBotIsValid(gBots[i]))
                continue;

            new stateText[16];
            switch (FakeBotGetState(gBots[i]))
            {
                case FAKEBOTS_STATE_CONNECTING: stateText = "connecting";
                case FAKEBOTS_STATE_IDLE:       stateText = "idle";
                case FAKEBOTS_STATE_SPAWNED:    stateText = "spawned";
                case FAKEBOTS_STATE_DEAD:       stateText = "dead";
                default:                        stateText = "removing";
            }

            format(msg, sizeof(msg), "#%d %s | %s", gBots[i], gBotNames[i], stateText);
            SendClientMessage(playerid, 0xFFFFFFFF, msg);
        }
        return 1;
    }

    if (!strcmp(cmd, "/bots", true))
    {
        if (!Demo_NextToken(cmdtext, idx, token, sizeof(token)))
            return SendClientMessage(playerid, 0xFF6666FF, "Usage: /bots <1-50>"), 1;

        new want = strval(token);
        if (want < 1 || want > 50)
            return SendClientMessage(playerid, 0xFF6666FF, "Usage: /bots <1-50>"), 1;

        new made;
        for (new i = 0; i < want; i++)
            if (Demo_AddBot() != FAKEBOTS_INVALID_ID) made++;

        new msg[96];
        format(msg, sizeof(msg), "Created %d of %d requested bots.", made, want);
        SendClientMessage(playerid, 0x33CC66FF, msg);
        return 1;
    }

    if (!strcmp(cmd, "/botsclear", true))
    {
        Demo_StopReplay(false);
        for (new i = gBotCount - 1; i >= 0; i--)
            if (FakeBotIsValid(gBots[i])) FakeBotDestroy(gBots[i]);

        gBotCount = 0;
        gReplayBot = FAKEBOTS_INVALID_ID;
        for (new i = 0; i < DEMO_MAX; i++)
            gBotNames[i][0] = '\0';

        SendClientMessage(playerid, 0x33CC66FF, "All bots are being removed.");
        return 1;
    }

    if (!strcmp(cmd, "/botsfollow", true))
    {
        if (gReplaying || gReplayWaiting) Demo_StopReplay(false);
        for (new i = 0; i < gBotCount; i++)
        {
            FakeBotSetIdleWander(gBots[i], false);
            FakeBotStopMoving(gBots[i]);
            FakeBotFollow(gBots[i], playerid, float(random(7) - 3), float(random(7) - 3), 2.2);
        }
        SendClientMessage(playerid, 0x33CC66FF, "The bots follow you.");
        return 1;
    }

    if (!strcmp(cmd, "/botsstop", true))
    {
        if (gReplaying || gReplayWaiting) Demo_StopReplay(false);
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
        if (botid == FAKEBOTS_INVALID_ID)
            return SendClientMessage(playerid, 0xFF6666FF, "No free slot or no available name."), 1;

        SetTimerEx("Demo_GiveCar", 3000, false, "i", botid);
        SendClientMessage(playerid, 0x33CC66FF, "A new bot will drive around the casino.");
        return 1;
    }

    if (!strcmp(cmd, "/botsay", true))
    {
        while (cmdtext[idx] == ' ') idx++;
        if (!cmdtext[idx] || gBotCount == 0)
            return SendClientMessage(playerid, 0xFF6666FF, "Usage: /botsay <text> (requires a bot)"), 1;

        FakeBotSendMessage(gBots[random(gBotCount)], cmdtext[idx]);
        return 1;
    }

    if (!strcmp(cmd, "/botwalk", true))
    {
        new idText[16], xText[24], yText[24], zText[24], speedText[24];
        if (!Demo_NextToken(cmdtext, idx, idText, sizeof(idText))
            || !Demo_NextToken(cmdtext, idx, xText, sizeof(xText))
            || !Demo_NextToken(cmdtext, idx, yText, sizeof(yText))
            || !Demo_NextToken(cmdtext, idx, zText, sizeof(zText)))
            return SendClientMessage(playerid, 0xFF6666FF, "Usage: /botwalk <id> <x> <y> <z> [speed]"), 1;

        new botid = strval(idText);
        if (Demo_GetSpawnedBotPlayer(botid) == FAKEBOTS_INVALID_ID)
            return SendClientMessage(playerid, 0xFF6666FF, "Bot not found or not spawned."), 1;

        new Float:speed = 1.2;
        if (Demo_NextToken(cmdtext, idx, speedText, sizeof(speedText)))
            speed = floatstr(speedText);
        if (speed <= 0.0 || speed > 10.0)
            return SendClientMessage(playerid, 0xFF6666FF, "Speed must be above 0 and at most 10."), 1;

        if (botid == gReplayBot) Demo_StopReplay(false);
        Demo_StopBot(botid);
        FakeBotGoTo(botid, floatstr(xText), floatstr(yText), floatstr(zText), speed, speed >= 4.0 ? FAKEBOTS_MOVE_SPRINT : (speed >= 2.0 ? FAKEBOTS_MOVE_RUN : FAKEBOTS_MOVE_WALK));
        SendClientMessage(playerid, 0x33CC66FF, "Bot walking route started.");
        return 1;
    }

    if (!strcmp(cmd, "/botdrive", true))
    {
        new idText[16], xText[24], yText[24], zText[24], speedText[24];
        if (!Demo_NextToken(cmdtext, idx, idText, sizeof(idText))
            || !Demo_NextToken(cmdtext, idx, xText, sizeof(xText))
            || !Demo_NextToken(cmdtext, idx, yText, sizeof(yText))
            || !Demo_NextToken(cmdtext, idx, zText, sizeof(zText)))
            return SendClientMessage(playerid, 0xFF6666FF, "Usage: /botdrive <id> <x> <y> <z> [speed]"), 1;

        new botid = strval(idText);
        new botPlayer = Demo_GetSpawnedBotPlayer(botid);
        if (botPlayer == FAKEBOTS_INVALID_ID)
            return SendClientMessage(playerid, 0xFF6666FF, "Bot not found or not spawned."), 1;
        if (GetPlayerState(botPlayer) != PLAYER_STATE_DRIVER)
            return SendClientMessage(playerid, 0xFF6666FF, "The bot must be driving a vehicle. Use /botcar first."), 1;

        new Float:speed = 25.0;
        if (Demo_NextToken(cmdtext, idx, speedText, sizeof(speedText)))
            speed = floatstr(speedText);
        if (speed < 1.0 || speed > 60.0)
            return SendClientMessage(playerid, 0xFF6666FF, "Driving speed must be between 1 and 60."), 1;

        if (botid == gReplayBot) Demo_StopReplay(false);
        Demo_StopBot(botid);
        FakeBotDriveTo(botid, floatstr(xText), floatstr(yText), floatstr(zText), speed);
        SendClientMessage(playerid, 0x33CC66FF, "Bot driving route started.");
        return 1;
    }

    if (!strcmp(cmd, "/botfollow", true))
    {
        if (!Demo_NextToken(cmdtext, idx, token, sizeof(token)))
            return SendClientMessage(playerid, 0xFF6666FF, "Usage: /botfollow <id>"), 1;
        new botid = strval(token);
        if (Demo_GetSpawnedBotPlayer(botid) == FAKEBOTS_INVALID_ID)
            return SendClientMessage(playerid, 0xFF6666FF, "Bot not found or not spawned."), 1;
        if (botid == gReplayBot) Demo_StopReplay(false);
        Demo_StopBot(botid);
        FakeBotFollow(botid, playerid, 1.0, -1.5, 2.2);
        SendClientMessage(playerid, 0x33CC66FF, "Bot is following you.");
        return 1;
    }

    if (!strcmp(cmd, "/botwander", true))
    {
        new idText[16], radiusText[24];
        if (!Demo_NextToken(cmdtext, idx, idText, sizeof(idText)))
            return SendClientMessage(playerid, 0xFF6666FF, "Usage: /botwander <id> [radius]"), 1;
        new botid = strval(idText);
        if (Demo_GetSpawnedBotPlayer(botid) == FAKEBOTS_INVALID_ID)
            return SendClientMessage(playerid, 0xFF6666FF, "Bot not found or not spawned."), 1;
        new Float:radius = 25.0;
        if (Demo_NextToken(cmdtext, idx, radiusText, sizeof(radiusText)))
            radius = floatstr(radiusText);
        if (radius < 1.0 || radius > 250.0)
            return SendClientMessage(playerid, 0xFF6666FF, "Radius must be between 1 and 250."), 1;
        if (botid == gReplayBot) Demo_StopReplay(false);
        Demo_StopBot(botid);
        FakeBotSetIdleWander(botid, true, radius, 1.0);
        SendClientMessage(playerid, 0x33CC66FF, "Bot idle wandering enabled.");
        return 1;
    }

    if (!strcmp(cmd, "/botstop", true))
    {
        if (!Demo_NextToken(cmdtext, idx, token, sizeof(token)))
            return SendClientMessage(playerid, 0xFF6666FF, "Usage: /botstop <id>"), 1;
        new botid = strval(token);
        if (!FakeBotIsValid(botid))
            return SendClientMessage(playerid, 0xFF6666FF, "Bot not found."), 1;
        if (botid == gReplayBot) Demo_StopReplay(false);
        Demo_StopBot(botid);
        SendClientMessage(playerid, 0x33CC66FF, "Bot movement stopped.");
        return 1;
    }

    if (!strcmp(cmd, "/botremove", true))
    {
        if (!Demo_NextToken(cmdtext, idx, token, sizeof(token)))
            return SendClientMessage(playerid, 0xFF6666FF, "Usage: /botremove <id>"), 1;
        new botid = strval(token);
        if (!FakeBotIsValid(botid))
            return SendClientMessage(playerid, 0xFF6666FF, "Bot not found."), 1;
        if (botid == gReplayBot)
        {
            Demo_StopReplay(false);
            gReplayBot = FAKEBOTS_INVALID_ID;
        }
        FakeBotDestroy(botid);
        SendClientMessage(playerid, 0x33CC66FF, "Bot removal requested.");
        return 1;
    }

    if (!strcmp(cmd, "/botrespawn", true))
    {
        if (!Demo_NextToken(cmdtext, idx, token, sizeof(token)))
            return SendClientMessage(playerid, 0xFF6666FF, "Usage: /botrespawn <id>"), 1;
        new botid = strval(token);
        if (!FakeBotIsValid(botid))
            return SendClientMessage(playerid, 0xFF6666FF, "Bot not found."), 1;
        if (botid == gReplayBot) Demo_StopReplay(false);
        FakeBotRespawn(botid);
        SendClientMessage(playerid, 0x33CC66FF, "Bot respawn requested.");
        return 1;
    }

    if (!strcmp(cmd, "/botpos", true))
    {
        if (!Demo_NextToken(cmdtext, idx, token, sizeof(token)))
            return SendClientMessage(playerid, 0xFF6666FF, "Usage: /botpos <id>"), 1;
        new botid = strval(token);
        new botPlayer = Demo_GetSpawnedBotPlayer(botid);
        if (botPlayer == FAKEBOTS_INVALID_ID)
            return SendClientMessage(playerid, 0xFF6666FF, "Bot not found or not spawned."), 1;
        new Float:x, Float:y, Float:z;
        GetPlayerPos(botPlayer, x, y, z);
        new msg[128];
        format(msg, sizeof(msg), "Bot %d position: %.2f, %.2f, %.2f | state=%d", botid, x, y, z, GetPlayerState(botPlayer));
        SendClientMessage(playerid, 0xFFFFFFFF, msg);
        return 1;
    }

    if (!strcmp(cmd, "/botskin", true))
    {
        new idText[16], skinText[16];
        if (!Demo_NextToken(cmdtext, idx, idText, sizeof(idText)) || !Demo_NextToken(cmdtext, idx, skinText, sizeof(skinText)))
            return SendClientMessage(playerid, 0xFF6666FF, "Usage: /botskin <id> <skin 0-311>"), 1;
        new botid = strval(idText), skinid = strval(skinText);
        new botPlayer = Demo_GetSpawnedBotPlayer(botid);
        if (botPlayer == FAKEBOTS_INVALID_ID || skinid < 0 || skinid > 311)
            return SendClientMessage(playerid, 0xFF6666FF, "Invalid bot or skin id."), 1;
        SetPlayerSkin(botPlayer, skinid);
        SendClientMessage(playerid, 0x33CC66FF, "Bot skin changed.");
        return 1;
    }

    if (!strcmp(cmd, "/bothealth", true))
    {
        new idText[16], healthText[24];
        if (!Demo_NextToken(cmdtext, idx, idText, sizeof(idText)) || !Demo_NextToken(cmdtext, idx, healthText, sizeof(healthText)))
            return SendClientMessage(playerid, 0xFF6666FF, "Usage: /bothealth <id> <hp>"), 1;
        new botid = strval(idText);
        new Float:health = floatstr(healthText);
        new botPlayer = Demo_GetSpawnedBotPlayer(botid);
        if (botPlayer == FAKEBOTS_INVALID_ID || health < 0.0 || health > 1000.0)
            return SendClientMessage(playerid, 0xFF6666FF, "Invalid bot or health value."), 1;
        SetPlayerHealth(botPlayer, health);
        SendClientMessage(playerid, 0x33CC66FF, "Bot health updated.");
        return 1;
    }

    if (!strcmp(cmd, "/botweapon", true))
    {
        new idText[16], weaponText[16], ammoText[16];
        if (!Demo_NextToken(cmdtext, idx, idText, sizeof(idText))
            || !Demo_NextToken(cmdtext, idx, weaponText, sizeof(weaponText))
            || !Demo_NextToken(cmdtext, idx, ammoText, sizeof(ammoText)))
            return SendClientMessage(playerid, 0xFF6666FF, "Usage: /botweapon <id> <weapon 0-46> <ammo>"), 1;
        new botid = strval(idText), weaponid = strval(weaponText), ammo = strval(ammoText);
        new botPlayer = Demo_GetSpawnedBotPlayer(botid);
        if (botPlayer == FAKEBOTS_INVALID_ID || weaponid < 0 || weaponid > 46 || ammo < 0 || ammo > 99999)
            return SendClientMessage(playerid, 0xFF6666FF, "Invalid bot, weapon, or ammo."), 1;
        GivePlayerWeapon(botPlayer, weaponid, ammo);
        SendClientMessage(playerid, 0x33CC66FF, "Weapon given to bot.");
        return 1;
    }

    if (!strcmp(cmd, "/botpool", true))
    {
        if (!Demo_NextToken(cmdtext, idx, token, sizeof(token)))
            return SendClientMessage(playerid, 0xFF6666FF, "Usage: /botpool <0|1>"), 1;
        new enabled = strval(token);
        if (enabled != 0 && enabled != 1)
            return SendClientMessage(playerid, 0xFF6666FF, "Usage: /botpool <0|1>"), 1;
        if (enabled == 1)
        {
            FakeBotSetPoolingEnabled(true);
            SendClientMessage(playerid, 0x33CC66FF, "Bot pooling enabled.");
        }
        else
        {
            FakeBotSetPoolingEnabled(false);
            SendClientMessage(playerid, 0x33CC66FF, "Bot pooling disabled.");
        }
        return 1;
    }

    if (!strcmp(cmd, "/bottick", true))
    {
        if (!Demo_NextToken(cmdtext, idx, token, sizeof(token)))
            return SendClientMessage(playerid, 0xFF6666FF, "Usage: /bottick <10-1000>"), 1;
        new tickRate = strval(token);
        if (tickRate < 10 || tickRate > 1000 || !FakeBotSetTickRate(tickRate))
            return SendClientMessage(playerid, 0xFF6666FF, "Tick rate must be between 10 and 1000 ms."), 1;
        new msg[80];
        format(msg, sizeof(msg), "FakeBots tick rate set to %d ms.", FakeBotGetTickRate());
        SendClientMessage(playerid, 0x33CC66FF, msg);
        return 1;
    }

    if (!strcmp(cmd, "/record", true))
    {
        if (FakeBotIsPlayer(playerid))
            return SendClientMessage(playerid, 0xFF6666FF, "Only real players can start a recording."), 1;
        if (gRecording)
            return SendClientMessage(playerid, 0xFF6666FF, "A recording is already running."), 1;
        if (gReplaying || gReplayWaiting)
            Demo_StopReplay(false);

        gRecordCount = 0;
        gRecordOwner = playerid;
        if (!Demo_StoreRecordFrame(playerid))
        {
            gRecordOwner = INVALID_PLAYER_ID;
            return SendClientMessage(playerid, 0xFF6666FF, "Could not start recording."), 1;
        }

        gRecording = true;
        gRecordTimer = SetTimer("Demo_RecordTick", DEMO_RECORD_INTERVAL, true);
        if (gRecordTimer == 0)
        {
            gRecording = false;
            gRecordOwner = INVALID_PLAYER_ID;
            gRecordCount = 0;
            return SendClientMessage(playerid, 0xFF6666FF, "Could not create the recording timer."), 1;
        }
        SendClientMessage(playerid, 0x33CC66FF, "Recording started. Walk, run, or drive; use /stoprecord to save it in memory.");
        return 1;
    }

    if (!strcmp(cmd, "/stoprecord", true))
    {
        if (!gRecording || gRecordOwner != playerid)
            return SendClientMessage(playerid, 0xFF6666FF, "You do not own an active recording."), 1;
        Demo_EndRecording(true);
        return 1;
    }

    if (!strcmp(cmd, "/runrecord", true))
    {
        if (gRecording)
            return SendClientMessage(playerid, 0xFF6666FF, "Use /stoprecord before playing the recording."), 1;
        if (gRecordCount < 2)
            return SendClientMessage(playerid, 0xFF6666FF, "No saved recording. Use /record then /stoprecord first."), 1;
        if (!Demo_StartReplay())
            return SendClientMessage(playerid, 0xFF6666FF, "Could not start playback (no free bot slot or no available name)."), 1;
        SendClientMessage(playerid, 0x33CC66FF, "Replay bot created. Playback starts after it has spawned.");
        return 1;
    }

    if (!strcmp(cmd, "/stopreplay", true))
    {
        if (!gReplaying && !gReplayWaiting)
            return SendClientMessage(playerid, 0xFF6666FF, "No recording playback is active."), 1;
        Demo_StopReplay(false);
        SendClientMessage(playerid, 0x33CC66FF, "Recording playback stopped.");
        return 1;
    }

    if (!strcmp(cmd, "/recordstatus", true))
    {
        new msg[144], recordingText[12], replayText[16];
        if (gRecording) format(recordingText, sizeof(recordingText), "yes");
        else format(recordingText, sizeof(recordingText), "no");
        if (gReplaying) format(replayText, sizeof(replayText), "yes");
        else if (gReplayWaiting) format(replayText, sizeof(replayText), "connecting");
        else format(replayText, sizeof(replayText), "no");
        format(msg, sizeof(msg), "Frames=%d/%d | recording=%s | replay=%s | sample=%dms",
            gRecordCount, DEMO_RECORD_MAX, recordingText, replayText, DEMO_RECORD_INTERVAL);
        SendClientMessage(playerid, 0xFFFFFFFF, msg);
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
