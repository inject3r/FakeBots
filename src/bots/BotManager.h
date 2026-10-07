// ============================================================================
// FakeBots :: BotManager.h
// Owns bot/client lifecycles and the simulation loop.
// ============================================================================
#pragma once

#include "Bot.h"
#include <array>
#include <cstdint>
#include <deque>

namespace FakeBots
{
    class BotManager
    {
    public:
        static BotManager& Get();
        void Reset();

        int RequestCreate(const std::string &name, const PendingSpawn &spawn);
        void ResolveConnection(int playerId, const std::string &playerName);
        void HandlePlayerRequestClass(int playerId, int classId);
        void BindNetworkPlayer(int botId, int playerId);
        bool HandlePlayerConnect(int playerId, const std::string &playerName);
        bool Destroy(int botId);
        void HandlePlayerDisconnect(int playerId, int reason);

        Bot* GetByBotId(int botId);
        Bot* GetByPlayerId(int playerId);
        bool IsValidBotId(int botId) const;
        bool IsPlayerBot(int playerId) const;
        int Count() const { return m_activeCount; }
        int GetMovingCount() const { return m_movingCount; }
        int GetDrivingCount() const { return m_drivingCount; }

        void Tick();

        void SetTickInterval(uint32_t milliseconds) { m_tickIntervalMs = milliseconds; }
        uint32_t GetTickInterval() const { return m_tickIntervalMs; }
        void SetPoolingEnabled(bool enabled);
        bool IsPoolingEnabled() const { return m_poolingEnabled; }
        int GetPooledCount() const { return static_cast<int>(m_pool.size()); }
        void SetNearbyRadius(float radius) { m_nearbyRadius = radius; }
        float GetNearbyRadius() const { return m_nearbyRadius; }
        void SetFarUpdateDistance(float distance) { m_farUpdateDistance = distance; }
        float GetFarUpdateDistance() const { return m_farUpdateDistance; }
        double GetLastTickTimeMs() const { return m_lastTickTimeMs; }

        // Player slots that bots must leave free for real players (minimum 1).
        // The server treats a burst of connections from one address that takes the
        // LAST free slot as a "server full" attack and bans the address, so the
        // plugin never opens such a connection.
        void SetReservedSlots(int slots) { m_reservedSlots = slots < 1 ? 1 : slots; }
        int GetReservedSlots() const { return m_reservedSlots; }

        bool IsRealPlayer(int playerId) const;
        void GetNearbyRealPlayers(float x, float y, float z, float radius, std::vector<int>& out) const;

    private:
        BotManager() = default;

        std::array<std::unique_ptr<Bot>, kMaxBots> m_bots{};
        std::unordered_map<int, int> m_playerIdToBotId;
        std::vector<int> m_activeBotIds;
        std::vector<int> m_pool;
        std::unordered_set<int> m_realPlayers;
        std::unordered_map<int, SampVector3> m_realPlayerPositions;
        std::unordered_map<int64_t, std::vector<int>> m_realPlayerGrid;
        std::unordered_set<int> m_expectedBotDisconnects;
        struct TransportFailure { int botId; uint64_t generation; int reason; };
        std::vector<TransportFailure> m_transportFailures;

        struct PendingStart { int botId; uint64_t generation; };
        std::deque<PendingStart> m_startQueue;
        int m_reservedSlots = 1;
        int m_cachedConnected = 0;
        uint32_t m_cachedConnectedAtMs = 0;
        // The server bans an address that has more than 30 half-open connections;
        // keep well below that no matter how many bots are requested at once.
        static constexpr int kMaxHandshakesInFlight = 12;
        static constexpr int kMaxConnectRetries = 2;

        static constexpr uint32_t kPumpIntervalMs = 10;
        uint32_t m_lastPumpMs = 0;

        int m_activeCount = 0;
        int m_movingCount = 0;
        int m_drivingCount = 0;
        uint32_t m_lastTickMs = 0;
        uint32_t m_tickIntervalMs = static_cast<uint32_t>(kTickIntervalMs);
        bool m_poolingEnabled = false;
        float m_nearbyRadius = kDefaultNearbyRadius;
        float m_farUpdateDistance = kFarUpdateDistance;
        double m_lastTickTimeMs = 0.0;

        int AllocateBotId();
        void AddActive(int botId);
        void RemoveActive(int botId);
        bool IsAnyRealPlayerWithin(float x, float y, float z, float distance) const;
        void ApplySpawnAndActivate(Bot &bot);
        void InstallClientCallbacks(Bot &bot);
        void HandleTransportFailure(int botId, int reason);
        void SeedRealPlayerCache();
        int CountOccupiedSlots();
        bool StartBotConnection(Bot &bot);
        void ProcessStartQueue();
    };
}
