// ============================================================================
//  FakeBots :: ServerHooks.cpp
//
//  Implements the standard sampgdk callback-export contract. sampgdk installs
//  a public-call filter (through the documented PLUGIN_DATA_AMX_EXPORTS /
//  amx_Exec export-table mechanism - part of the stable, official plugin
//  ABI, NOT raw process memory patching) and, for every Pawn public that
//  fires, looks up a plugin-exported C function of the same name and calls
//  it first. This is the exact mechanism sampgdk itself documents and is
//  fully version-independent: it works unmodified across every SA-MP server
//  build and on open.mp's legacy-plugin compatibility layer.
//
//  We use it purely as an *observer* - every handler returns true so the
//  gamemode's own callback always still runs normally afterwards.
// ============================================================================
#include "../core/Common.h"
#include "../bots/BotManager.h"
#include "CallbackDispatcher.h"

namespace
{
    using namespace FakeBots;

    // OnPlayerTakeDamage / OnPlayerGiveDamage are not part of vanilla SA-MP
    // but are provided by open.mp; when running on plain SA-MP the public
    // simply never fires here, so this stays a safe no-op.
    bool HandleDamagePublic(bool bIsTake, AMX *amx, const char *name, cell *params, cell *retval)
    {
        (void)amx; (void)retval;
        if (params == nullptr || params[0] < static_cast<cell>(5 * sizeof(cell)))
            return true;

        const int playerId = static_cast<int>(params[1]);
        const int otherId  = static_cast<int>(params[2]);

        float amount;
        std::memcpy(&amount, &params[3], sizeof(float));
        const int weaponId  = static_cast<int>(params[4]);
        const int bodyPart  = static_cast<int>(params[5]);

        Bot *bot = BotManager::Get().GetByPlayerId(playerId);
        if (bot == nullptr)
            return true;

        if (bIsTake)
            CallbackDispatcher::Get().OnBotTakeDamage(bot->GetBotId(), otherId, amount, weaponId, bodyPart);
        else
            CallbackDispatcher::Get().OnBotGiveDamage(bot->GetBotId(), otherId, amount, weaponId, bodyPart);

        (void)name;
        return true;
    }
}

extern "C"
{
    // ------------------------------------------------------------------
    // Generic public-call observer - used only for callbacks that don't
    // have a dedicated typed export in this sampgdk build (damage events).
    // ------------------------------------------------------------------
    PLUGIN_EXPORT bool PLUGIN_CALL OnPublicCall(AMX *amx, const char *name, cell *params, cell *retval)
    {
        if (name == nullptr)
            return true;

        if (std::strcmp(name, "OnPlayerTakeDamage") == 0)
            return HandleDamagePublic(true, amx, name, params, retval);

        if (std::strcmp(name, "OnPlayerGiveDamage") == 0)
            return HandleDamagePublic(false, amx, name, params, retval);

        return true;
    }

    // ------------------------------------------------------------------
    // Connection handshake: FakeBots are ordinary network players, not
    // server-side NPCs. The mapping is established by the real RakNet
    // connection and resolved here as soon as the server emits OnPlayerConnect.
    // ------------------------------------------------------------------
    PLUGIN_EXPORT bool PLUGIN_CALL OnPlayerConnect(int playerid)
    {
        char name[MAX_PLAYER_NAME + 1] = {0};
        sampgdk::GetPlayerName(playerid, name, sizeof(name));
        BotManager::Get().HandlePlayerConnect(playerid, name);
        return true;
    }

    PLUGIN_EXPORT bool PLUGIN_CALL OnPlayerDisconnect(int playerid, int reason)
    {
        BotManager::Get().HandlePlayerDisconnect(playerid, reason);
        return true;
    }

    PLUGIN_EXPORT bool PLUGIN_CALL OnPlayerRequestClass(int playerid, int classid)
    {
        BotManager::Get().HandlePlayerRequestClass(playerid, classid);
        return true;
    }

    PLUGIN_EXPORT bool PLUGIN_CALL OnPlayerSpawn(int playerid)
    {
        Bot *bot = BotManager::Get().GetByPlayerId(playerid);
        if (bot != nullptr)
        {
            bot->Client().ConfirmServerSpawn();
            bot->SetState(BotLifeState::Spawned);
            CallbackDispatcher::Get().OnBotSpawn(bot->GetBotId());
        }
        return true;
    }

    PLUGIN_EXPORT bool PLUGIN_CALL OnPlayerDeath(int playerid, int killerid, int reason)
    {
        Bot *bot = BotManager::Get().GetByPlayerId(playerid);
        if (bot != nullptr)
        {
            bot->SetState(BotLifeState::Dead);
            CallbackDispatcher::Get().OnBotDeath(bot->GetBotId(), killerid, reason);
        }
        return true;
    }

    PLUGIN_EXPORT bool PLUGIN_CALL OnPlayerStateChange(int playerid, int newstate, int oldstate)
    {
        Bot *bot = BotManager::Get().GetByPlayerId(playerid);
        if (bot != nullptr)
            CallbackDispatcher::Get().OnBotStateChange(bot->GetBotId(), newstate, oldstate);
        return true;
    }

    PLUGIN_EXPORT bool PLUGIN_CALL OnPlayerEnterVehicle(int playerid, int vehicleid, bool ispassenger)
    {
        Bot *bot = BotManager::Get().GetByPlayerId(playerid);
        if (bot != nullptr)
            CallbackDispatcher::Get().OnBotEnterVehicle(bot->GetBotId(), vehicleid, ispassenger);
        return true;
    }

    PLUGIN_EXPORT bool PLUGIN_CALL OnPlayerExitVehicle(int playerid, int vehicleid)
    {
        Bot *bot = BotManager::Get().GetByPlayerId(playerid);
        if (bot != nullptr)
            CallbackDispatcher::Get().OnBotExitVehicle(bot->GetBotId(), vehicleid);
        return true;
    }
}
