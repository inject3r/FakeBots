// ============================================================================
//  FakeBots :: Natives.cpp
// ============================================================================
#include "Natives.h"
#include "../bots/BotManager.h"
#include "../bots/BotMovement.h"
#include "../bots/BotGroupManager.h"
#include "../bots/BotFileLoader.h"
#include "../callbacks/CallbackDispatcher.h"
#include "../i18n/LanguageManager.h"
#include <random>

using namespace FakeBots;

namespace
{
    inline Bot* RequireBot(cell botIdParam)
    {
        return BotManager::Get().GetByBotId(static_cast<int>(botIdParam));
    }

    inline bool HasNativeParams(const cell *params, std::size_t argumentCount)
    {
        return params != nullptr && static_cast<std::size_t>(params[0]) >= argumentCount * sizeof(cell);
    }

    inline bool ResolvePawnCellArray(AMX *amx, cell param, cell **address)
    {
        return amx != nullptr && address != nullptr && amx_GetAddr(amx, param, address) == AMX_ERR_NONE && *address != nullptr;
    }

    inline BotMoveType ToMoveType(cell value)
    {
        switch (value)
        {
            case FAKEBOTS_MOVE_RUN:    return BotMoveType::Run;
            case FAKEBOTS_MOVE_SPRINT: return BotMoveType::Sprint;
            case FAKEBOTS_MOVE_WALK:
            default:                   return BotMoveType::Walk;
        }
    }

    // --------------------------------------------------------------------
    // Creation / destruction
    // --------------------------------------------------------------------
    cell AMX_NATIVE_CALL n_FakeBotCreate(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 10))
            return static_cast<cell>(kInvalidId);
        char name[MAX_PLAYER_NAME + 1];
        char *nameBuf = nullptr;
        amx_StrParam(amx, params[1], nameBuf);
        if (nameBuf == nullptr || nameBuf[0] == '\0')
        {
            // amx_StrParam yields nullptr for an empty string: there is no
            // sensible nickname to invent, so refuse instead of guessing.
            sampgdk::logprintf("[FakeBots] FakeBotCreate: the nickname is empty.");
            return static_cast<cell>(kInvalidId);
        }
        const char *rawName = nameBuf;
        const size_t rawNameLength = std::strlen(rawName);
        if (rawNameLength > MAX_PLAYER_NAME)
        {
            sampgdk::logprintf("[FakeBots] FakeBotCreate: the nickname is longer than %d characters.", MAX_PLAYER_NAME);
            return static_cast<cell>(kInvalidId);
        }
        std::memcpy(name, rawName, rawNameLength);
        name[rawNameLength] = '\0';

        PendingSpawn spawn;
        spawn.skin         = static_cast<int>(params[2]);
        spawn.x             = amx_ctof(params[3]);
        spawn.y             = amx_ctof(params[4]);
        spawn.z             = amx_ctof(params[5]);
        spawn.angle         = amx_ctof(params[6]);
        spawn.weapon        = static_cast<int>(params[7]);
        spawn.ammo          = static_cast<int>(params[8]);
        spawn.virtualWorld  = static_cast<int>(params[9]);
        spawn.interior      = static_cast<int>(params[10]);

        return static_cast<cell>(BotManager::Get().RequestCreate(name, spawn));
    }

    cell AMX_NATIVE_CALL n_FakeBotDestroy(AMX *amx, cell *params)
    {
        (void)amx;
        if (!HasNativeParams(params, 1))
            return 0;
        return BotManager::Get().Destroy(static_cast<int>(params[1])) ? 1 : 0;
    }

    cell AMX_NATIVE_CALL n_FakeBotRespawn(AMX *amx, cell *params)
    {
        (void)amx;
        if (!HasNativeParams(params, 6))
            return 0;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr || bot->GetPlayerId() == kInvalidId)
            return 0;

        const int skin = static_cast<int>(params[2]);
        const float x  = amx_ctof(params[3]);
        const float y  = amx_ctof(params[4]);
        const float z  = amx_ctof(params[5]);
        const float a  = amx_ctof(params[6]);

        float px, py, pz;
        bot->GetCachedPosition(px, py, pz);
        // NaN means "keep current value". Zero is a valid GTA coordinate and
        // therefore must never be treated as an implicit sentinel.
        const float spawnX = std::isnan(x) ? px : x;
        const float spawnY = std::isnan(y) ? py : y;
        const float spawnZ = std::isnan(z) ? pz : z;
        const float spawnAngle = std::isnan(a) ? bot->GetCachedFacing() : a;

        if (skin >= 0)
            sampgdk::SetPlayerSkin(bot->GetPlayerId(), skin);

        PendingSpawn &spawn = bot->GetPendingSpawn();
        spawn.x = spawnX;
        spawn.y = spawnY;
        spawn.z = spawnZ;
        spawn.angle = spawnAngle;
        if (skin >= 0)
            spawn.skin = skin;

        bot->ResetRuntimeState();
        bot->Client().ResetForSpawn(spawnX, spawnY, spawnZ, spawnAngle, 100.0f, 0.0f,
                                    static_cast<uint8_t>(std::clamp(spawn.weapon, 0, 255)));
        bot->SetCachedPosition(spawnX, spawnY, spawnZ);
        bot->SetCachedFacing(spawnAngle);
        bot->SetCachedHealth(100.0f);
        bot->SetCachedArmour(0.0f);
        bot->SetState(BotLifeState::Idle);
        sampgdk::SetSpawnInfo(bot->GetPlayerId(), 0, spawn.skin,
            spawnX, spawnY, spawnZ, spawnAngle, spawn.weapon, spawn.ammo, 0, 0, 0, 0);
        sampgdk::SetPlayerVirtualWorld(bot->GetPlayerId(), spawn.virtualWorld);
        sampgdk::SetPlayerInterior(bot->GetPlayerId(), spawn.interior);
        if (sampgdk::GetPlayerVehicleID(bot->GetPlayerId()) != 0)
            sampgdk::RemovePlayerFromVehicle(bot->GetPlayerId());
        sampgdk::ForceClassSelection(bot->GetPlayerId());
        return bot->Client().BeginSpawnSelection(0) ? 1 : 0;
    }

    cell AMX_NATIVE_CALL n_FakeBotIsValid(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        return BotManager::Get().IsValidBotId(static_cast<int>(params[1])) ? 1 : 0;
    }

    cell AMX_NATIVE_CALL n_FakeBotGetCount(AMX *amx, cell *params)
    {
        (void)amx; (void)params;
        return static_cast<cell>(BotManager::Get().Count());
    }

    cell AMX_NATIVE_CALL n_FakeBotGetState(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        return bot ? static_cast<cell>(bot->GetState()) : static_cast<cell>(kInvalidId);
    }

    cell AMX_NATIVE_CALL n_FakeBotGetName(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 3) || params[3] <= 0)
            return 0;
        Bot *bot = RequireBot(params[1]);
        const std::string name = bot ? bot->GetName() : std::string();
        cell *dest = nullptr;
        if (!ResolvePawnCellArray(amx, params[2], &dest))
            return 0;
        amx_SetString(dest, name.c_str(), 0, 0, static_cast<size_t>(params[3]));
        return static_cast<cell>(name.size());
    }

    // --------------------------------------------------------------------
    // Id mapping
    // --------------------------------------------------------------------
    cell AMX_NATIVE_CALL n_FakeBotGetPlayerID(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        return bot ? static_cast<cell>(bot->GetPlayerId()) : static_cast<cell>(kInvalidId);
    }

    cell AMX_NATIVE_CALL n_FakeBotGetID(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        Bot *bot = BotManager::Get().GetByPlayerId(static_cast<int>(params[1]));
        return bot ? static_cast<cell>(bot->GetBotId()) : static_cast<cell>(kInvalidId);
    }

    cell AMX_NATIVE_CALL n_FakeBotIsPlayer(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        return BotManager::Get().IsPlayerBot(static_cast<int>(params[1])) ? 1 : 0;
    }

    // --------------------------------------------------------------------
    // On-foot movement
    // --------------------------------------------------------------------
    cell AMX_NATIVE_CALL n_FakeBotGoTo(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 6)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr)
            return 0;

        bot->ClearWaypoints();
        bot->SetMoveTarget(amx_ctof(params[2]), amx_ctof(params[3]), amx_ctof(params[4]),
                            amx_ctof(params[5]), ToMoveType(params[6]));
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotStopMoving(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr)
            return 0;
        bot->ClearWaypoints();
        bot->StopMoving();
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotIsMoving(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        return (bot && bot->IsMoving()) ? 1 : 0;
    }

    cell AMX_NATIVE_CALL n_FakeBotGetMoveTarget(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 4))
            return 0;
        Bot *bot = RequireBot(params[1]);
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (bot)
            bot->GetMoveTarget(x, y, z);

        cell *addr = nullptr;
        if (!ResolvePawnCellArray(amx, params[2], &addr)) return 0;
        *addr = amx_ftoc(x);
        if (!ResolvePawnCellArray(amx, params[3], &addr)) return 0;
        *addr = amx_ftoc(y);
        if (!ResolvePawnCellArray(amx, params[4], &addr)) return 0;
        *addr = amx_ftoc(z);
        return bot ? 1 : 0;
    }

    cell AMX_NATIVE_CALL n_FakeBotAddWaypoint(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 6)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr)
            return 0;

        const float x = amx_ctof(params[2]);
        const float y = amx_ctof(params[3]);
        const float z = amx_ctof(params[4]);
        const float speed = amx_ctof(params[5]);
        const BotMoveType type = ToMoveType(params[6]);

        bot->PushWaypoint(x, y, z);
        if (!bot->IsMoving())
        {
            const Waypoint &first = bot->Waypoints().front();
            bot->SetMoveTarget(first.x, first.y, first.z, speed, type);
        }
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotClearWaypoints(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr)
            return 0;
        bot->ClearWaypoints();
        bot->StopMoving();
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotSetLoopWaypoints(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 2)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr)
            return 0;
        bot->SetLoopWaypoints(params[2] != 0);
        return 1;
    }

    // --------------------------------------------------------------------
    // Vehicles / driving
    // --------------------------------------------------------------------
    cell AMX_NATIVE_CALL n_FakeBotPutInNewVehicle(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 8)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr || bot->GetPlayerId() == kInvalidId)
            return static_cast<cell>(kInvalidId);

        const int modelId = static_cast<int>(params[2]);
        const float x = amx_ctof(params[3]);
        const float y = amx_ctof(params[4]);
        const float z = amx_ctof(params[5]);
        const float angle = amx_ctof(params[6]);
        const int color1 = static_cast<int>(params[7]);
        const int color2 = static_cast<int>(params[8]);

        const int vehicleId = sampgdk::CreateVehicle(modelId, x, y, z, angle, color1, color2, -1, false);
        if (vehicleId == INVALID_VEHICLE_ID)
            return static_cast<cell>(kInvalidId);

        sampgdk::PutPlayerInVehicle(bot->GetPlayerId(), vehicleId, 0);
        bot->SetVehicleId(vehicleId);
        return static_cast<cell>(vehicleId);
    }

    cell AMX_NATIVE_CALL n_FakeBotPutInVehicle(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 3)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr || bot->GetPlayerId() == kInvalidId)
            return 0;

        const int vehicleId = static_cast<int>(params[2]);
        const int seat = static_cast<int>(params[3]);
        if (!sampgdk::IsValidVehicle(vehicleId) || seat < 0 || seat > 3)
            return 0;

        if (!sampgdk::PutPlayerInVehicle(bot->GetPlayerId(), vehicleId, seat))
            return 0;

        if (seat == 0)
            bot->SetVehicleId(vehicleId);
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotRemoveFromVehicle(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr || bot->GetPlayerId() == kInvalidId)
            return 0;

        bot->StopDriving();
        return sampgdk::RemovePlayerFromVehicle(bot->GetPlayerId()) ? 1 : 0;
    }

    cell AMX_NATIVE_CALL n_FakeBotDriveTo(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 5)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr || bot->GetPlayerId() == kInvalidId)
            return 0;

        const int vehicleId = sampgdk::GetPlayerVehicleID(bot->GetPlayerId());
        if (vehicleId == 0 || sampgdk::GetPlayerState(bot->GetPlayerId()) != PLAYER_STATE_DRIVER)
            return 0; // Bot must actually be the vehicle driver.

        bot->SetDriveTarget(vehicleId, amx_ctof(params[2]), amx_ctof(params[3]), amx_ctof(params[4]), amx_ctof(params[5]));
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotStopDriving(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr)
            return 0;
        bot->StopDriving();
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotIsDriving(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        return (bot && bot->IsDriving()) ? 1 : 0;
    }

    // --------------------------------------------------------------------
    // Chat
    // --------------------------------------------------------------------
    cell AMX_NATIVE_CALL n_FakeBotSendMessage(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 2)) return 0;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr || bot->GetPlayerId() == kInvalidId)
            return 0;

        char *text;
        amx_StrParam(amx, params[2], text);
        if (text == nullptr)
            return 0;

        // Use the actual client->server RPC. The server therefore executes
        // its normal OnPlayerText/chat pipeline instead of a plugin-side
        // manual broadcast which could bypass filters and chat rules.
        return bot->Client().SendChat(text) ? 1 : 0;
    }

    // --------------------------------------------------------------------
    // Groups
    // --------------------------------------------------------------------
    cell AMX_NATIVE_CALL n_FakeBotCreateGroup(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        return BotGroupManager::Get().CreateGroup(static_cast<int>(params[1])) ? 1 : 0;
    }

    cell AMX_NATIVE_CALL n_FakeBotDestroyGroup(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        return BotGroupManager::Get().DestroyGroup(static_cast<int>(params[1])) ? 1 : 0;
    }

    cell AMX_NATIVE_CALL n_FakeBotIsValidGroup(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        return BotGroupManager::Get().IsValidGroup(static_cast<int>(params[1])) ? 1 : 0;
    }

    cell AMX_NATIVE_CALL n_FakeBotAddToGroup(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 2)) return 0;
        (void)amx;
        return BotGroupManager::Get().AddToGroup(static_cast<int>(params[1]), static_cast<int>(params[2])) ? 1 : 0;
    }

    cell AMX_NATIVE_CALL n_FakeBotRemoveFromGroup(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 2)) return 0;
        (void)amx;
        return BotGroupManager::Get().RemoveFromGroup(static_cast<int>(params[1]), static_cast<int>(params[2])) ? 1 : 0;
    }

    cell AMX_NATIVE_CALL n_FakeBotIsInGroup(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 2)) return 0;
        (void)amx;
        return BotGroupManager::Get().IsInGroup(static_cast<int>(params[1]), static_cast<int>(params[2])) ? 1 : 0;
    }

    cell AMX_NATIVE_CALL n_FakeBotGetGroupCount(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        return static_cast<cell>(BotGroupManager::Get().GetGroupCount(static_cast<int>(params[1])));
    }

    cell AMX_NATIVE_CALL n_FakeBotGroupGoTo(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 6)) return 0;
        (void)amx;
        const auto *members = BotGroupManager::Get().GetMembers(static_cast<int>(params[1]));
        if (members == nullptr)
            return 0;

        const float x = amx_ctof(params[2]);
        const float y = amx_ctof(params[3]);
        const float z = amx_ctof(params[4]);
        const float speed = amx_ctof(params[5]);
        const BotMoveType type = ToMoveType(params[6]);

        int affected = 0;
        for (int botId : *members)
        {
            Bot *bot = RequireBot(static_cast<cell>(botId));
            if (bot == nullptr)
                continue;
            bot->ClearWaypoints();
            bot->SetMoveTarget(x, y, z, speed, type);
            affected++;
        }
        return affected;
    }

    cell AMX_NATIVE_CALL n_FakeBotGroupStopMoving(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        const auto *members = BotGroupManager::Get().GetMembers(static_cast<int>(params[1]));
        if (members == nullptr)
            return 0;

        int affected = 0;
        for (int botId : *members)
        {
            Bot *bot = RequireBot(static_cast<cell>(botId));
            if (bot == nullptr)
                continue;
            bot->ClearWaypoints();
            bot->StopMoving();
            affected++;
        }
        return affected;
    }

    cell AMX_NATIVE_CALL n_FakeBotGroupAddWaypoint(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 6)) return 0;
        (void)amx;
        const auto *members = BotGroupManager::Get().GetMembers(static_cast<int>(params[1]));
        if (members == nullptr)
            return 0;

        const float x = amx_ctof(params[2]);
        const float y = amx_ctof(params[3]);
        const float z = amx_ctof(params[4]);
        const float speed = amx_ctof(params[5]);
        const BotMoveType type = ToMoveType(params[6]);

        int affected = 0;
        for (int botId : *members)
        {
            Bot *bot = RequireBot(static_cast<cell>(botId));
            if (bot == nullptr)
                continue;

            bot->PushWaypoint(x, y, z);
            if (!bot->IsMoving())
            {
                const Waypoint &first = bot->Waypoints().front();
                bot->SetMoveTarget(first.x, first.y, first.z, speed, type);
            }
            affected++;
        }
        return affected;
    }

    cell AMX_NATIVE_CALL n_FakeBotGroupClearWaypoints(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        const auto *members = BotGroupManager::Get().GetMembers(static_cast<int>(params[1]));
        if (members == nullptr)
            return 0;

        int affected = 0;
        for (int botId : *members)
        {
            Bot *bot = RequireBot(static_cast<cell>(botId));
            if (bot == nullptr)
                continue;
            bot->ClearWaypoints();
            bot->StopMoving();
            affected++;
        }
        return affected;
    }

    cell AMX_NATIVE_CALL n_FakeBotGroupSetLoopWaypoints(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 2)) return 0;
        (void)amx;
        const auto *members = BotGroupManager::Get().GetMembers(static_cast<int>(params[1]));
        if (members == nullptr)
            return 0;

        const bool loop = params[2] != 0;
        int affected = 0;
        for (int botId : *members)
        {
            Bot *bot = RequireBot(static_cast<cell>(botId));
            if (bot == nullptr)
                continue;
            bot->SetLoopWaypoints(loop);
            affected++;
        }
        return affected;
    }

    cell AMX_NATIVE_CALL n_FakeBotGroupDestroyBots(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        const int groupId = static_cast<int>(params[1]);
        const auto *members = BotGroupManager::Get().GetMembers(groupId);
        if (members == nullptr)
            return 0;

        // Copy first - Destroy() mutates the group via PurgeBot() as it goes.
        std::vector<int> botIds(members->begin(), members->end());
        int affected = 0;
        for (int botId : botIds)
        {
            if (BotManager::Get().Destroy(botId))
                affected++;
        }
        return affected;
    }

    cell AMX_NATIVE_CALL n_FakeBotGroupSendMessage(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 2)) return 0;
        const auto *members = BotGroupManager::Get().GetMembers(static_cast<int>(params[1]));
        if (members == nullptr)
            return 0;

        char *text;
        amx_StrParam(amx, params[2], text);
        if (text == nullptr)
            return 0;

        int affected = 0;
        for (int botId : *members)
        {
            Bot *bot = RequireBot(static_cast<cell>(botId));
            if (bot == nullptr || bot->GetPlayerId() == kInvalidId)
                continue;

            // Real client path: the server itself raises OnPlayerText, applies the
            // gamemode's chat formatting and broadcasts the line.
            if (bot->Client().SendChat(text))
                ++affected;
        }
        return affected;
    }

    // Arranges every bot in the group into a simple staggered-line
    // formation trailing `leaderplayerid`: bot N sits at
    // (offsetX, offsetY - N*spacing) in the leader's local space, and the
    // whole group re-follows every tick as the leader moves (see
    // BotMovement::StepFollow).
    cell AMX_NATIVE_CALL n_FakeBotGroupFollow(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 6)) return 0;
        const auto *members = BotGroupManager::Get().GetMembers(static_cast<int>(params[1]));
        if (members == nullptr)
            return 0;

        const int leaderPlayerId = static_cast<int>(params[2]);
        const float baseOffsetX  = amx_ctof(params[3]);
        const float baseOffsetY  = amx_ctof(params[4]);
        const float speed        = amx_ctof(params[5]);
        const float spacing      = amx_ctof(params[6]);

        (void)amx;
        int index = 0;
        int affected = 0;
        for (int botId : *members)
        {
            Bot *bot = RequireBot(static_cast<cell>(botId));
            if (bot == nullptr || bot->GetPlayerId() == kInvalidId)
                continue;

            bot->SetFollowTarget(leaderPlayerId, baseOffsetX, baseOffsetY - (index * spacing), speed);
            index++;
            affected++;
        }
        return affected;
    }

    // --------------------------------------------------------------------
    // Localization
    // --------------------------------------------------------------------
    cell AMX_NATIVE_CALL n_FakeBotSetReservedSlots(AMX *amx, cell *params)
    {
        (void)amx;
        if (!HasNativeParams(params, 1)) return 0;
        BotManager::Get().SetReservedSlots(static_cast<int>(params[1]));
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotGetReservedSlots(AMX *amx, cell *params)
    {
        (void)amx; (void)params;
        return static_cast<cell>(BotManager::Get().GetReservedSlots());
    }

    cell AMX_NATIVE_CALL n_FakeBotSetLanguage(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        char *lang = nullptr;
        amx_StrParam(amx, params[1], lang);
        return LanguageManager::Get().SetLanguage(lang ? lang : "en") ? 1 : 0;
    }

    cell AMX_NATIVE_CALL n_FakeBotGetLanguage(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 2) || params[2] <= 0) return 0;
        const std::string &lang = LanguageManager::Get().GetLanguage();
        cell *dest = nullptr;
        if (!ResolvePawnCellArray(amx, params[1], &dest)) return 0;
        amx_SetString(dest, lang.c_str(), 0, 0, static_cast<size_t>(params[2]));
        return static_cast<cell>(lang.size());
    }

    cell AMX_NATIVE_CALL n_FakeBotReloadLanguage(AMX *amx, cell *params)
    {
        (void)amx; (void)params;
        return LanguageManager::Get().Reload() ? 1 : 0;
    }

    cell AMX_NATIVE_CALL n_FakeBotSetLanguageDirectory(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        char *path = nullptr;
        amx_StrParam(amx, params[1], path);
        LanguageManager::Get().SetDirectory(path ? path : "plugins/FakeBots/lang");
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotGetText(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 3) || params[3] <= 0) return 0;
        char *key = nullptr;
        amx_StrParam(amx, params[1], key);
        const std::string &text = LanguageManager::Get().GetText(key ? key : "");

        cell *dest = nullptr;
        if (!ResolvePawnCellArray(amx, params[2], &dest)) return 0;
        amx_SetString(dest, text.c_str(), 0, 0, static_cast<size_t>(params[3]));
        return static_cast<cell>(text.size());
    }

    // --------------------------------------------------------------------
    // Follow AI
    // --------------------------------------------------------------------
    cell AMX_NATIVE_CALL n_FakeBotFollow(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 5)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr || bot->GetPlayerId() == kInvalidId)
            return 0;

        bot->SetFollowTarget(static_cast<int>(params[2]), amx_ctof(params[3]), amx_ctof(params[4]), amx_ctof(params[5]));
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotStopFollowing(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr)
            return 0;
        bot->StopFollowing();
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotIsFollowing(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        return (bot && bot->IsFollowing()) ? 1 : 0;
    }

    // --------------------------------------------------------------------
    // Driving waypoint routes
    // --------------------------------------------------------------------
    cell AMX_NATIVE_CALL n_FakeBotAddDriveWaypoint(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 5)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr || bot->GetPlayerId() == kInvalidId)
            return 0;

        const int vehicleId = sampgdk::GetPlayerVehicleID(bot->GetPlayerId());
        if (vehicleId == 0)
            return 0;

        const float x = amx_ctof(params[2]);
        const float y = amx_ctof(params[3]);
        const float z = amx_ctof(params[4]);
        const float speed = amx_ctof(params[5]);

        bot->PushDriveWaypoint(x, y, z, speed);
        if (!bot->IsDriving())
        {
            const Waypoint &first = bot->DriveWaypoints().front();
            bot->SetDriveTarget(vehicleId, first.x, first.y, first.z, speed);
        }
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotClearDriveWaypoints(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr)
            return 0;
        bot->ClearDriveWaypoints();
        bot->StopDriving();
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotSetLoopDriveWaypoints(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 2)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr)
            return 0;
        bot->SetLoopDriveWaypoints(params[2] != 0);
        return 1;
    }

    // --------------------------------------------------------------------
    // Combat AI
    // --------------------------------------------------------------------
    cell AMX_NATIVE_CALL n_FakeBotSetCombatTarget(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 5)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr || bot->GetPlayerId() == kInvalidId)
            return 0;

        bot->SetCombatTarget(static_cast<int>(params[2]), amx_ctof(params[3]), amx_ctof(params[4]), amx_ctof(params[5]));
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotStopCombat(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr)
            return 0;
        bot->StopCombat();
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotIsInCombat(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        return (bot && bot->IsInCombat()) ? 1 : 0;
    }

    // --------------------------------------------------------------------
    // Idle wander
    // --------------------------------------------------------------------
    cell AMX_NATIVE_CALL n_FakeBotSetIdleWander(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 4)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr || bot->GetPlayerId() == kInvalidId)
            return 0;

        const bool enabled = params[2] != 0;
        const float radius = amx_ctof(params[3]);
        const float speed  = amx_ctof(params[4]);

        float ax, ay, az;
        sampgdk::GetPlayerPos(bot->GetPlayerId(), &ax, &ay, &az);
        bot->SetIdleWander(enabled, ax, ay, az, radius, speed);
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotIsIdleWanderEnabled(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        return (bot && bot->IsIdleWanderEnabled()) ? 1 : 0;
    }

    // --------------------------------------------------------------------
    // Random appearance
    // --------------------------------------------------------------------
    // A conservative built-in pool of universally valid pedestrian/civilian
    // skin ids (SA-MP has a number of special/vehicle/cutscene skin ids
    // that crash or look wrong on a regular player - this list deliberately
    // sticks to well-known safe street-clothes skins). Overridable per
    // server via FakeBotSetAppearancePool().
    static std::vector<int> s_appearancePool = {
        1, 2, 7, 19, 21, 22, 26, 29, 39, 41, 44, 50, 57, 65, 72, 84, 88, 91, 93, 97,
        100, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117,
        150, 151, 152, 153, 154, 158, 159, 161, 163, 165, 168, 169, 190, 191, 192, 195, 198
    };

    cell AMX_NATIVE_CALL n_FakeBotSetAppearancePool(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 2))
            return 0;
        cell *arrAddr = nullptr;
        if (!ResolvePawnCellArray(amx, params[1], &arrAddr))
            return 0;
        const int count = static_cast<int>(params[2]);

        if (count <= 0 || arrAddr == nullptr)
            return 0;

        s_appearancePool.clear();
        s_appearancePool.reserve(count);
        for (int i = 0; i < count; ++i)
            s_appearancePool.push_back(static_cast<int>(arrAddr[i]));

        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotRandomizeAppearance(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        Bot *bot = RequireBot(params[1]);
        if (bot == nullptr || bot->GetPlayerId() == kInvalidId || s_appearancePool.empty())
            return -1;

        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, s_appearancePool.size() - 1);
        const int skin = s_appearancePool[dist(rng)];

        sampgdk::SetPlayerSkin(bot->GetPlayerId(), skin);
        return skin;
    }

    // --------------------------------------------------------------------
    // Bulk loading from JSON
    // --------------------------------------------------------------------
    cell AMX_NATIVE_CALL n_FakeBotLoadFromFile(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        char *filename;
        amx_StrParam(amx, params[1], filename);
        if (filename == nullptr)
            return 0;

        return static_cast<cell>(BotFileLoader::LoadFromFile(filename));
    }

    // --------------------------------------------------------------------
    // Performance / tuning
    // --------------------------------------------------------------------
    cell AMX_NATIVE_CALL n_FakeBotSetTickRate(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        const int ms = static_cast<int>(params[1]);
        if (ms < 10)
            return 0; // refuse anything that would busy-loop the server
        BotManager::Get().SetTickInterval(static_cast<uint32_t>(ms));
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotGetTickRate(AMX *amx, cell *params)
    {
        (void)amx; (void)params;
        return static_cast<cell>(BotManager::Get().GetTickInterval());
    }

    cell AMX_NATIVE_CALL n_FakeBotSetPoolingEnabled(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        BotManager::Get().SetPoolingEnabled(params[1] != 0);
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotIsPoolingEnabled(AMX *amx, cell *params)
    {
        (void)amx; (void)params;
        return BotManager::Get().IsPoolingEnabled() ? 1 : 0;
    }

    cell AMX_NATIVE_CALL n_FakeBotGetPooledCount(AMX *amx, cell *params)
    {
        (void)amx; (void)params;
        return static_cast<cell>(BotManager::Get().GetPooledCount());
    }

    cell AMX_NATIVE_CALL n_FakeBotSetNearbyRadius(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        BotManager::Get().SetNearbyRadius(amx_ctof(params[1]));
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotSetFarUpdateDistance(AMX *amx, cell *params)
    {
        if (!HasNativeParams(params, 1)) return 0;
        (void)amx;
        BotManager::Get().SetFarUpdateDistance(amx_ctof(params[1]));
        return 1;
    }

    cell AMX_NATIVE_CALL n_FakeBotGetLastTickTime(AMX *amx, cell *params)
    {
        (void)amx; (void)params;
        float value = static_cast<float>(BotManager::Get().GetLastTickTimeMs());
        return amx_ftoc(value);
    }

    cell AMX_NATIVE_CALL n_FakeBotGetMovingCount(AMX *amx, cell *params)
    {
        (void)amx; (void)params;
        return static_cast<cell>(BotManager::Get().GetMovingCount());
    }

    cell AMX_NATIVE_CALL n_FakeBotGetDrivingCount(AMX *amx, cell *params)
    {
        (void)amx; (void)params;
        return static_cast<cell>(BotManager::Get().GetDrivingCount());
    }
}

namespace FakeBots
{
    namespace Natives
    {
        int RegisterAll(AMX *amx)
        {
            static const AMX_NATIVE_INFO table[] =
            {
                { "FakeBotCreate",               n_FakeBotCreate },
                { "FakeBotDestroy",               n_FakeBotDestroy },
                { "FakeBotRespawn",               n_FakeBotRespawn },
                { "FakeBotIsValid",               n_FakeBotIsValid },
                { "FakeBotGetCount",              n_FakeBotGetCount },
                { "FakeBotGetState",              n_FakeBotGetState },
                { "FakeBotGetName",               n_FakeBotGetName },

                { "FakeBotGetPlayerID",           n_FakeBotGetPlayerID },
                { "FakeBotGetID",              n_FakeBotGetID },
                { "FakeBotIsPlayer",        n_FakeBotIsPlayer },

                { "FakeBotGoTo",                  n_FakeBotGoTo },
                { "FakeBotStopMoving",             n_FakeBotStopMoving },
                { "FakeBotIsMoving",               n_FakeBotIsMoving },
                { "FakeBotGetMoveTarget",          n_FakeBotGetMoveTarget },
                { "FakeBotAddWaypoint",            n_FakeBotAddWaypoint },
                { "FakeBotClearWaypoints",         n_FakeBotClearWaypoints },
                { "FakeBotSetLoopWaypoints",       n_FakeBotSetLoopWaypoints },

                { "FakeBotPutInNewVehicle",        n_FakeBotPutInNewVehicle },
                { "FakeBotPutInVehicle",            n_FakeBotPutInVehicle },
                { "FakeBotRemoveFromVehicle",       n_FakeBotRemoveFromVehicle },
                { "FakeBotDriveTo",                n_FakeBotDriveTo },
                { "FakeBotStopDriving",             n_FakeBotStopDriving },
                { "FakeBotIsDriving",               n_FakeBotIsDriving },

                { "FakeBotSetReservedSlots",       n_FakeBotSetReservedSlots },
                { "FakeBotGetReservedSlots",       n_FakeBotGetReservedSlots },
                { "FakeBotSetLanguage",            n_FakeBotSetLanguage },
                { "FakeBotGetLanguage",            n_FakeBotGetLanguage },
                { "FakeBotReloadLanguage",          n_FakeBotReloadLanguage },
                { "FakeBotSetLanguageDirectory",    n_FakeBotSetLanguageDirectory },
                { "FakeBotGetText",                n_FakeBotGetText },

                { "FakeBotSendMessage",            n_FakeBotSendMessage },

                { "FakeBotCreateGroup",            n_FakeBotCreateGroup },
                { "FakeBotDestroyGroup",            n_FakeBotDestroyGroup },
                { "FakeBotIsValidGroup",            n_FakeBotIsValidGroup },
                { "FakeBotAddToGroup",              n_FakeBotAddToGroup },
                { "FakeBotRemoveFromGroup",          n_FakeBotRemoveFromGroup },
                { "FakeBotIsInGroup",               n_FakeBotIsInGroup },
                { "FakeBotGetGroupCount",           n_FakeBotGetGroupCount },
                { "FakeBotGroupGoTo",               n_FakeBotGroupGoTo },
                { "FakeBotGroupStopMoving",          n_FakeBotGroupStopMoving },
                { "FakeBotGroupAddWaypoint",         n_FakeBotGroupAddWaypoint },
                { "FakeBotGroupClearWaypoints",      n_FakeBotGroupClearWaypoints },
                { "FakeBotGroupSetLoopWaypoints",    n_FakeBotGroupSetLoopWaypoints },
                { "FakeBotGroupDestroyBots",         n_FakeBotGroupDestroyBots },
                { "FakeBotGroupSendMessage",         n_FakeBotGroupSendMessage },
                { "FakeBotGroupFollow",              n_FakeBotGroupFollow },

                { "FakeBotFollow",                 n_FakeBotFollow },
                { "FakeBotStopFollowing",          n_FakeBotStopFollowing },
                { "FakeBotIsFollowing",            n_FakeBotIsFollowing },

                { "FakeBotAddDriveWaypoint",       n_FakeBotAddDriveWaypoint },
                { "FakeBotClearDriveWaypoints",    n_FakeBotClearDriveWaypoints },
                { "FakeBotSetLoopDriveWaypoints",  n_FakeBotSetLoopDriveWaypoints },

                { "FakeBotSetCombatTarget",        n_FakeBotSetCombatTarget },
                { "FakeBotStopCombat",             n_FakeBotStopCombat },
                { "FakeBotIsInCombat",             n_FakeBotIsInCombat },

                { "FakeBotSetIdleWander",          n_FakeBotSetIdleWander },
                { "FakeBotIsIdleWanderEnabled",    n_FakeBotIsIdleWanderEnabled },

                { "FakeBotSetAppearancePool",      n_FakeBotSetAppearancePool },
                { "FakeBotRandomizeAppearance",    n_FakeBotRandomizeAppearance },

                { "FakeBotLoadFromFile",           n_FakeBotLoadFromFile },

                { "FakeBotSetTickRate",            n_FakeBotSetTickRate },
                { "FakeBotGetTickRate",            n_FakeBotGetTickRate },
                { "FakeBotSetPoolingEnabled",      n_FakeBotSetPoolingEnabled },
                { "FakeBotIsPoolingEnabled",       n_FakeBotIsPoolingEnabled },
                { "FakeBotGetPooledCount",         n_FakeBotGetPooledCount },
                { "FakeBotSetNearbyRadius",        n_FakeBotSetNearbyRadius },
                { "FakeBotSetFarUpdateDistance",   n_FakeBotSetFarUpdateDistance },
                { "FakeBotGetLastTickTime",        n_FakeBotGetLastTickTime },
                { "FakeBotGetMovingCount",         n_FakeBotGetMovingCount },
                { "FakeBotGetDrivingCount",        n_FakeBotGetDrivingCount },

                { nullptr, nullptr }
            };

            return amx_Register(amx, table, -1);
        }
    }
}
