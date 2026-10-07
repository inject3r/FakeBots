// ============================================================================
//  FakeBots :: CallbackDispatcher.h
//  Forwards FakeBots_On* events into every loaded Pawn script. Scripts are
//  free to omit any of these forwards - a missing public is simply skipped,
//  exactly like standard SA-MP callbacks.
// ============================================================================
#pragma once

#include "../core/Common.h"

#include <deque>
#include <initializer_list>

namespace FakeBots
{
    class CallbackDispatcher
    {
    public:
        static CallbackDispatcher& Get();

        void RegisterAmx(AMX *amx);
        void UnregisterAmx(AMX *amx);
        const std::vector<AMX*>& LoadedScripts() const { return m_scripts; }

        // Generic forward helpers. `format` uses the classic amx_Push*
        // convention: 'd'/'i' = cell, 'f' = float(passed as double), so we
        // keep a tiny bespoke dispatcher instead (see .cpp) for clarity and
        // to avoid varargs pitfalls with floats on every ABI we target.

        void OnBotConnect(int botId);
        void OnBotSpawn(int botId);
        void OnBotDeath(int botId, int killerId, int reason);
        void OnBotDisconnect(int botId, int reason);
        void OnBotReachDestination(int botId, float x, float y, float z);
        void OnBotWaypointReached(int botId, int waypointIndex);
        void OnBotEnterVehicle(int botId, int vehicleId, bool isPassenger);
        void OnBotExitVehicle(int botId, int vehicleId);
        void OnBotVehicleReachDestination(int botId, int vehicleId, float x, float y, float z);
        void OnBotStateChange(int botId, int newState, int oldState);
        void OnBotTakeDamage(int botId, int issuerId, float amount, int weaponId, int bodyPart);
        void OnBotGiveDamage(int botId, int damagedId, float amount, int weaponId, int bodyPart);

        // Fired when a real player comes within FakeBotSetNearbyRadius()'s
        // configured distance of a bot (see BotAI::CheckNearbyPlayers).
        void OnBotPlayerNearby(int botId, int playerId, float distance);

        // ------------------------------------------------------------------
        // Event delivery
        //
        // Every OnFakeBot* event is queued by the On* methods above and handed
        // to the scripts from Flush(), which the plugin calls once per
        // ProcessTick - i.e. never from inside another public's amx_Exec hook.
        // That guarantees the natural ordering scripts expect (the server's own
        // OnPlayerConnect/OnPlayerSpawn/... always run BEFORE the matching
        // OnFakeBot* event) and that a script reacting to an event can safely
        // call natives that raise further events.
        // ------------------------------------------------------------------
        void Flush();
        void ClearPending();
        size_t PendingCount() const { return m_queue.size(); }

    private:
        CallbackDispatcher() = default;

        static constexpr int kMaxParams = 6;
        static constexpr size_t kMaxQueued = 16384;
        static constexpr int kMaxEventsPerFlush = 4096;

        struct PendingEvent
        {
            const char *name = nullptr; // string literal, static storage
            cell params[kMaxParams] = {};
            int count = 0;
        };

        void Post(const char *publicName, std::initializer_list<cell> params);

        // Executes `publicName` with the given cell-encoded params (already
        // in left-to-right logical order) on every loaded script. Caches
        // each script's amx_FindPublic() result per name so repeated calls
        // (this runs on every simulation tick for movement/AI callbacks)
        // don't re-search the AMX public table every single time.
        void Forward(const char *publicName, const cell *params, int count);

        std::vector<AMX*> m_scripts;
        std::deque<PendingEvent> m_queue;
        bool m_flushing = false;
        bool m_overflowReported = false;
        std::unordered_map<AMX*, std::unordered_map<std::string, int>> m_publicIndexCache;
    };
}
