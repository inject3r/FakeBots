// ============================================================================
//  FakeBots :: CallbackDispatcher.cpp
// ============================================================================
#include "CallbackDispatcher.h"
#include <algorithm>

namespace FakeBots
{
    CallbackDispatcher& CallbackDispatcher::Get()
    {
        static CallbackDispatcher instance;
        return instance;
    }

    void CallbackDispatcher::RegisterAmx(AMX *amx)
    {
        // A reloaded script may reuse the same AMX address: never trust old lookups.
        m_publicIndexCache.erase(amx);
        if (std::find(m_scripts.begin(), m_scripts.end(), amx) == m_scripts.end())
            m_scripts.push_back(amx);
    }

    void CallbackDispatcher::UnregisterAmx(AMX *amx)
    {
        m_scripts.erase(std::remove(m_scripts.begin(), m_scripts.end(), amx), m_scripts.end());
        m_publicIndexCache.erase(amx);
    }

    // Looks up (and caches) the public function index for `publicName` on
    // a given script. A cached value of -1 means "confirmed absent" so we
    // don't re-run amx_FindPublic() for scripts that simply don't
    // implement a given optional callback either.
    static constexpr int kPublicNotFound = -1;

    static int FindPublicCached(AMX *amx, const char *publicName, std::unordered_map<AMX*, std::unordered_map<std::string, int>> &cache)
    {
        auto &perScript = cache[amx];
        auto it = perScript.find(publicName);
        if (it != perScript.end())
            return it->second;

        int index = 0;
        const int result = (amx_FindPublic(amx, publicName, &index) == AMX_ERR_NONE) ? index : kPublicNotFound;
        perScript.emplace(publicName, result);
        return result;
    }

    // Pushes params in reverse order (Pawn calling convention), resolves the
    // public by name per-script (via the cache above) and executes it if
    // present. A script that doesn't implement the callback is silently
    // skipped - identical to how the server itself treats optional
    // callbacks.
    //
    // Only ever called from Flush(), i.e. from ProcessTick, when no AMX is in
    // the middle of executing - so the AMX stack is empty and a plain
    // push/exec is safe.
    void CallbackDispatcher::Forward(const char *publicName, const cell *params, int count)
    {
        // Scripts can load/unload other scripts from inside a callback. Take
        // a stable snapshot so vector reallocation/erase cannot invalidate the
        // iterator used by this dispatch.
        const std::vector<AMX*> scripts = m_scripts;
        for (AMX *amx : scripts)
        {
            // A callback is allowed to unload another AMX. Do not touch a
            // snapshot entry that has been removed from the live registry.
            if (amx == nullptr ||
                std::find(m_scripts.begin(), m_scripts.end(), amx) == m_scripts.end() ||
                amx->base == nullptr)
                continue;

            const int index = FindPublicCached(amx, publicName, m_publicIndexCache);
            if (index == kPublicNotFound)
                continue;

            const cell savedStk = amx->stk;
            const int savedParamCount = amx->paramcount;

            bool pushOk = true;
            for (int i = count - 1; i >= 0; --i)
            {
                if (amx_Push(amx, params[i]) != AMX_ERR_NONE)
                {
                    pushOk = false;
                    break;
                }
            }
            if (!pushOk)
            {
                // Undo a partial push so the script's stack stays balanced.
                amx->stk = savedStk;
                amx->paramcount = savedParamCount;
                continue;
            }

            cell retval = 0;
            const int error = amx_Exec(amx, &retval, index);
            if (error != AMX_ERR_NONE && error != AMX_ERR_SLEEP)
            {
                sampgdk::logprintf("[FakeBots] Warning: %s failed with AMX error %d.", publicName, error);
            }
        }
    }

    void CallbackDispatcher::Post(const char *publicName, std::initializer_list<cell> params)
    {
        if (m_queue.size() >= kMaxQueued)
        {
            // Nobody is draining the queue (ProcessTick stalled?). Drop the
            // oldest event instead of growing without bound.
            m_queue.pop_front();
            if (!m_overflowReported)
            {
                m_overflowReported = true;
                sampgdk::logprintf("[FakeBots] Warning: event queue overflow, dropping the oldest events.");
            }
        }

        PendingEvent event;
        event.name = publicName;
        for (cell value : params)
        {
            if (event.count < kMaxParams)
                event.params[event.count++] = value;
        }
        m_queue.push_back(event);
    }

    void CallbackDispatcher::Flush()
    {
        if (m_flushing)
            return; // a script callback must never re-enter the dispatcher

        m_flushing = true;
        int budget = kMaxEventsPerFlush;
        while (!m_queue.empty() && budget-- > 0)
        {
            const PendingEvent event = m_queue.front();
            m_queue.pop_front();
            Forward(event.name, event.params, event.count);
        }
        m_flushing = false;
    }

    void CallbackDispatcher::ClearPending()
    {
        m_queue.clear();
        m_overflowReported = false;
    }

    static inline cell FloatToCell(float value)
    {
        cell result;
        std::memcpy(&result, &value, sizeof(cell));
        return result;
    }

    void CallbackDispatcher::OnBotConnect(int botId)
    {
        Post("OnFakeBotConnect", { static_cast<cell>(botId) });
    }

    void CallbackDispatcher::OnBotSpawn(int botId)
    {
        Post("OnFakeBotSpawn", { static_cast<cell>(botId) });
    }

    void CallbackDispatcher::OnBotDeath(int botId, int killerId, int reason)
    {
        Post("OnFakeBotDeath", { static_cast<cell>(botId), static_cast<cell>(killerId), static_cast<cell>(reason) });
    }

    void CallbackDispatcher::OnBotDisconnect(int botId, int reason)
    {
        Post("OnFakeBotDisconnect", { static_cast<cell>(botId), static_cast<cell>(reason) });
    }

    void CallbackDispatcher::OnBotReachDestination(int botId, float x, float y, float z)
    {
        Post("OnFakeBotReachDestination", { static_cast<cell>(botId), FloatToCell(x), FloatToCell(y), FloatToCell(z) });
    }

    void CallbackDispatcher::OnBotWaypointReached(int botId, int waypointIndex)
    {
        Post("OnFakeBotWaypointReached", { static_cast<cell>(botId), static_cast<cell>(waypointIndex) });
    }

    void CallbackDispatcher::OnBotEnterVehicle(int botId, int vehicleId, bool isPassenger)
    {
        Post("OnFakeBotEnterVehicle", { static_cast<cell>(botId), static_cast<cell>(vehicleId), static_cast<cell>(isPassenger ? 1 : 0) });
    }

    void CallbackDispatcher::OnBotExitVehicle(int botId, int vehicleId)
    {
        Post("OnFakeBotExitVehicle", { static_cast<cell>(botId), static_cast<cell>(vehicleId) });
    }

    void CallbackDispatcher::OnBotVehicleReachDestination(int botId, int vehicleId, float x, float y, float z)
    {
        Post("OnFakeBotVehicleReachDest", { static_cast<cell>(botId), static_cast<cell>(vehicleId), FloatToCell(x), FloatToCell(y), FloatToCell(z) });
    }

    void CallbackDispatcher::OnBotStateChange(int botId, int newState, int oldState)
    {
        Post("OnFakeBotStateChange", { static_cast<cell>(botId), static_cast<cell>(newState), static_cast<cell>(oldState) });
    }

    void CallbackDispatcher::OnBotTakeDamage(int botId, int issuerId, float amount, int weaponId, int bodyPart)
    {
        Post("OnFakeBotTakeDamage", { static_cast<cell>(botId), static_cast<cell>(issuerId), FloatToCell(amount), static_cast<cell>(weaponId), static_cast<cell>(bodyPart) });
    }

    void CallbackDispatcher::OnBotGiveDamage(int botId, int damagedId, float amount, int weaponId, int bodyPart)
    {
        Post("OnFakeBotGiveDamage", { static_cast<cell>(botId), static_cast<cell>(damagedId), FloatToCell(amount), static_cast<cell>(weaponId), static_cast<cell>(bodyPart) });
    }

    void CallbackDispatcher::OnBotPlayerNearby(int botId, int playerId, float distance)
    {
        Post("OnFakeBotPlayerNearby", { static_cast<cell>(botId), static_cast<cell>(playerId), FloatToCell(distance) });
    }
}
