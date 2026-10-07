// ============================================================================
// FakeBots :: BotManager.cpp
// ============================================================================
#include "BotManager.h"
#include "BotMovement.h"
#include "BotAI.h"
#include "BotGroupManager.h"
#include "../callbacks/CallbackDispatcher.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cctype>
#include <unordered_map>

namespace FakeBots
{
    namespace
    {
        constexpr size_t kMinNicknameLength = 3; // MIN_PLAYER_NAME on the server

        bool IsPlausibleNickname(const std::string &name)
        {
            if (name.size() < kMinNicknameLength || name.size() > static_cast<size_t>(MAX_PLAYER_NAME))
                return false;
            for (const unsigned char c : name)
            {
                if (c <= 0x20 || c >= 0x7F)
                    return false;
            }
            return true;
        }

        // The bots always join the server they run inside of: the loopback address,
        // or the address the server is bound to when it is bound to a specific one.
        // The port cannot change while the server runs, so it is read once (reading the
        // legacy "port" variable makes open.mp print a deprecation warning every time).
        void ResolveLocalEndpoint(std::string &host, uint16_t &port)
        {
            static uint16_t cachedPort = 0;
            static std::string cachedHost;
            static bool resolved = false;
            if (!resolved)
            {
                // open.mp name first (silent on SA-MP: unknown variable -> 0), then the SA-MP name.
                int value = sampgdk::GetServerVarAsInt("network.port");
                if (value <= 0)
                    value = sampgdk::GetServerVarAsInt("port");
                cachedPort = static_cast<uint16_t>(std::clamp(value, 0, 65535));

                char bind[64] = {0};
                if (sampgdk::GetServerVarAsString("bind", bind, sizeof(bind)) &&
                    bind[0] != '\0' && std::strcmp(bind, "0.0.0.0") != 0 && std::strcmp(bind, "0") != 0)
                    cachedHost = bind;
                else
                    cachedHost = "127.0.0.1";
                resolved = cachedPort != 0; // retry later if the server has not published its port yet
            }
            host = cachedHost.empty() ? "127.0.0.1" : cachedHost;
            port = cachedPort;
        }

        // Human readable form of the codes SampBotClient reports.
        std::string DescribeNetFailure(int code)
        {
            switch (code)
            {
                case NetFail::kAttemptFailed:   return "the server did not answer the connection attempt";
                case NetFail::kNoFreeSlot:      return "the server has no free player slot";
                case NetFail::kClosedByServer:  return "the server closed the connection";
                case NetFail::kConnectionLost:  return "the connection was lost (timeout)";
                case NetFail::kBanned:          return "the server banned this address";
                case NetFail::kInvalidPassword: return "the server rejected the password";
                case NetFail::kJoinRejectedBase - 1: return "the server rejected the client version";
                case NetFail::kJoinRejectedBase - 2: return "the server rejected the nickname (taken or characters not allowed)";
                case NetFail::kJoinRejectedBase - 3: return "the server rejected the client modification flag";
                case NetFail::kJoinRejectedBase - 4: return "the server has no free player slot";
                default: break;
            }
            if (code <= NetFail::kStageTimeoutBase && code > NetFail::kStageTimeoutBase - 100)
                return "the join handshake timed out";
            return "unexpected network event";
        }
    }

    namespace
    {
        uint32_t NowMs()
        {
            using namespace std::chrono;
            return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
        }

        constexpr float kSpatialCellSize = 64.0f;

        int SpatialCell(float value)
        {
            return static_cast<int>(std::floor(value / kSpatialCellSize));
        }

        int64_t SpatialKey(int x, int y)
        {
            return (static_cast<int64_t>(x) << 32) ^ static_cast<uint32_t>(y);
        }

        std::string NormalizePlayerName(const std::string &name)
        {
            std::string out = name;
            for (char &ch : out)
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            return out;
        }

        bool IsPlayerNameOccupied(const std::string &name, int ignorePlayerId = kInvalidId)
        {
            const std::string wanted = NormalizePlayerName(name);
            const int maxPlayers = sampgdk::GetMaxPlayers();
            char currentName[MAX_PLAYER_NAME + 1] = {};
            for (int playerId = 0; playerId < maxPlayers; ++playerId)
            {
                if (playerId == ignorePlayerId || !sampgdk::IsPlayerConnected(playerId))
                    continue;
                currentName[0] = '\0';
                sampgdk::GetPlayerName(playerId, currentName, sizeof(currentName));
                if (NormalizePlayerName(currentName) == wanted)
                    return true;
            }
            return false;
        }
    }

    BotManager& BotManager::Get()
    {
        static BotManager instance;
        return instance;
    }

    void BotManager::Reset()
    {
        for (auto &slot : m_bots)
        {
            if (slot)
                slot->Client().Stop(0);
            slot.reset();
        }

        m_playerIdToBotId.clear();
        m_activeBotIds.clear();
        m_pool.clear();
        m_expectedBotDisconnects.clear();
        m_transportFailures.clear();
        m_startQueue.clear();
        m_cachedConnected = 0;
        m_cachedConnectedAtMs = 0;
        m_realPlayers.clear();
        m_realPlayerPositions.clear();
        m_realPlayerGrid.clear();
        m_activeCount = 0;
        m_movingCount = 0;
        m_drivingCount = 0;
        m_lastTickMs = 0;
        m_lastTickTimeMs = 0.0;
        BotGroupManager::Get().Reset();
    }

    void BotManager::SetPoolingEnabled(bool enabled)
    {
        if (m_poolingEnabled == enabled)
            return;

        if (!enabled)
        {
            for (int botId : m_pool)
            {
                Bot *bot = GetByBotId(botId);
                if (bot == nullptr)
                    continue;
                const int playerId = bot->GetPlayerId();
                if (playerId != kInvalidId)
                {
                    m_expectedBotDisconnects.insert(playerId);
                    m_playerIdToBotId.erase(playerId);
                    bot->Client().Stop(0);
                }
                BotGroupManager::Get().PurgeBot(botId);
                m_bots[botId].reset();
            }
            m_pool.clear();
        }

        m_poolingEnabled = enabled;
    }

    int BotManager::AllocateBotId()
    {
        for (int i = 0; i < kMaxBots; ++i)
        {
            if (m_bots[i] == nullptr)
                return i;
        }
        return kInvalidId;
    }

    void BotManager::AddActive(int botId)
    {
        if (std::find(m_activeBotIds.begin(), m_activeBotIds.end(), botId) == m_activeBotIds.end())
            m_activeBotIds.push_back(botId);
    }

    void BotManager::RemoveActive(int botId)
    {
        m_activeBotIds.erase(std::remove(m_activeBotIds.begin(), m_activeBotIds.end(), botId), m_activeBotIds.end());
    }

    void BotManager::InstallClientCallbacks(Bot &bot)
    {
        const int botId = bot.GetBotId();
        const uint64_t generation = bot.GetGeneration();
        bot.Client().SetPlayerIdCallback([this, botId, generation](int playerId)
        {
            Bot *current = GetByBotId(botId);
            if (current == nullptr || current->GetGeneration() != generation)
                return;
            BindNetworkPlayer(botId, playerId);
        });
        bot.Client().SetTransportCallback([this, botId, generation](int reason)
        {
            Bot *current = GetByBotId(botId);
            if (current == nullptr || current->GetGeneration() != generation)
                return;
            HandleTransportFailure(botId, reason);
        });
    }

    void BotManager::ApplySpawnAndActivate(Bot &bot)
    {
        const PendingSpawn &spawn = bot.GetPendingSpawn();
        const int playerId = bot.GetPlayerId();
        if (playerId == kInvalidId)
            return;

        const int maxPlayers = sampgdk::GetMaxPlayers();
        if (playerId < 0 || playerId >= maxPlayers)
            return;

        sampgdk::SetSpawnInfo(playerId, 0, spawn.skin,
            spawn.x, spawn.y, spawn.z, spawn.angle,
            spawn.weapon, spawn.ammo, 0, 0, 0, 0);
        sampgdk::SetPlayerVirtualWorld(playerId, spawn.virtualWorld);
        sampgdk::SetPlayerInterior(playerId, spawn.interior);

        bot.Client().ResetForSpawn(spawn.x, spawn.y, spawn.z, spawn.angle);
        bot.SetCachedPosition(spawn.x, spawn.y, spawn.z);
        bot.SetCachedFacing(spawn.angle);
        bot.SetCachedVelocity(0.0f, 0.0f, 0.0f);
        bot.SetCachedHealth(100.0f);
        bot.SetCachedArmour(0.0f);
        bot.SetCachedWeapon(static_cast<uint8_t>(std::clamp(spawn.weapon, 0, 255)));
        bot.SetState(BotLifeState::Idle);

        // IMPORTANT: do not call SpawnPlayer here. The embedded client must
        // complete the real SA-MP class/spawn handshake: InitGame ->
        // RequestClass -> RequestSpawn -> Spawn. SetSpawnInfo only provides
        // the server-side spawn data used by that handshake.
    }

    int BotManager::RequestCreate(const std::string &name, const PendingSpawn &spawn)
    {
        // Only reject what no SA-MP / open.mp server can ever accept (length,
        // blanks, control and non-ASCII characters). The finer nickname rules
        // are the server's business (they can be customised there) and its
        // verdict is reported through OnFakeBotDisconnect.
        if (!IsPlausibleNickname(name))
        {
            sampgdk::logprintf("[FakeBots] Cannot create '%s': a nickname needs 3-%d printable characters and no spaces.",
                name.c_str(), MAX_PLAYER_NAME);
            return kInvalidId;
        }

        // Never let the nickname resolver ambiguously bind a real player's
        // OnPlayerConnect callback to a bot session. SA-MP nicknames are
        // effectively case-insensitive for uniqueness, so compare folded ASCII.
        int pooledMatchBotId = kInvalidId;
        if (m_poolingEnabled)
        {
            const std::string wanted = NormalizePlayerName(name);
            for (auto it = m_pool.rbegin(); it != m_pool.rend(); ++it)
            {
                Bot *pooled = GetByBotId(*it);
                if (pooled != nullptr && NormalizePlayerName(pooled->GetName()) == wanted)
                {
                    pooledMatchBotId = pooled->GetBotId();
                    break;
                }
            }
        }

        int ignoredPlayerId = kInvalidId;
        if (pooledMatchBotId != kInvalidId)
        {
            if (Bot *pooled = GetByBotId(pooledMatchBotId))
                ignoredPlayerId = pooled->GetPlayerId();
        }

        if (IsPlayerNameOccupied(name, ignoredPlayerId))
        {
            sampgdk::logprintf("[FakeBots] Cannot create '%s': nickname is already in use.", name.c_str());
            return kInvalidId;
        }

        for (int activeBotId : m_activeBotIds)
        {
            Bot *activeBot = GetByBotId(activeBotId);
            if (activeBot != nullptr &&
                NormalizePlayerName(activeBot->GetName()) == NormalizePlayerName(name))
            {
                sampgdk::logprintf("[FakeBots] Cannot create '%s': another bot is already using that nickname.", name.c_str());
                return kInvalidId;
            }
        }

        if (m_poolingEnabled && !m_pool.empty())
        {
            int poolIndex = static_cast<int>(m_pool.size()) - 1;
            if (pooledMatchBotId != kInvalidId)
            {
                for (int i = 0; i < static_cast<int>(m_pool.size()); ++i)
                {
                    if (m_pool[i] == pooledMatchBotId)
                    {
                        poolIndex = i;
                        break;
                    }
                }
            }
            const int botId = m_pool[poolIndex];
            m_pool.erase(m_pool.begin() + poolIndex);
            Bot *bot = GetByBotId(botId);
            if (bot != nullptr && bot->GetPlayerId() != kInvalidId && bot->Client().IsConnected())
            {
                bot->SetName(name);
                bot->Client().SetName(name);
                bot->ResetConnectForwarded();
                bot->GetPendingSpawn() = spawn;
                bot->Client().ResetForSpawn(spawn.x, spawn.y, spawn.z, spawn.angle, 100.0f, 0.0f,
                                            static_cast<uint8_t>(std::clamp(spawn.weapon, 0, 255)));
                sampgdk::SetPlayerName(bot->GetPlayerId(), name.c_str());
                sampgdk::TogglePlayerControllable(bot->GetPlayerId(), true);
                ApplySpawnAndActivate(*bot);
                // The connection is alive and spawned already. The server accepts a
                // RequestClass only from players that are not alive, so the recycled
                // bot is spawned with the script API instead: the server answers with a
                // forced RequestSpawn and the client completes it with the Spawn RPC.
                bot->Client().BeginForcedSpawn();
                sampgdk::SpawnPlayer(bot->GetPlayerId()); // fakebots-audit-allow: SpawnPlayer (pooled reuse; the client still completes the Spawn RPC)
                AddActive(botId);
                ++m_activeCount;
                if (!bot->HasConnectForwarded())
                {
                    bot->MarkConnectForwarded();
                    CallbackDispatcher::Get().OnBotConnect(botId);
                }
                return botId;
            }

            if (bot != nullptr)
                bot->Client().Stop(0);
            m_bots[botId].reset();
        }

        // Never open a connection the server would have to refuse: a full server answers
        // with "no free slot", and the connection that takes the last free slot is treated
        // as an attack (see SetReservedSlots).
        const int maxPlayers = sampgdk::GetMaxPlayers();
        const int occupied = CountOccupiedSlots();
        if (occupied + 1 > maxPlayers - m_reservedSlots)
        {
            sampgdk::logprintf("[FakeBots] Cannot create '%s': %d of %d player slots are in use and %d must stay free for real players.",
                name.c_str(), occupied, maxPlayers, m_reservedSlots);
            return kInvalidId;
        }

        const int botId = AllocateBotId();
        if (botId == kInvalidId)
            return kInvalidId;

        auto bot = std::make_unique<Bot>(botId, name);
        bot->GetPendingSpawn() = spawn;
        InstallClientCallbacks(*bot);

        // The connection itself is opened by ProcessStartQueue() a few at a time.
        const uint64_t generation = bot->GetGeneration();
        m_bots[botId] = std::move(bot);
        AddActive(botId);
        ++m_activeCount;
        m_startQueue.push_back({botId, generation});
        return botId;
    }

    // Slots that are taken or about to be taken on the server: every connected
    // player plus the bots that are still on their way in.
    int BotManager::CountOccupiedSlots()
    {
        const uint32_t now = NowMs();
        const int maxPlayers = sampgdk::GetMaxPlayers();
        if (m_cachedConnectedAtMs == 0 || now - m_cachedConnectedAtMs > 100)
        {
            int connected = 0;
            for (int i = 0; i < maxPlayers; ++i)
                if (sampgdk::IsPlayerConnected(i))
                    ++connected;
            m_cachedConnected = connected;
            m_cachedConnectedAtMs = now;
        }

        int arriving = 0;
        for (int botId : m_activeBotIds)
        {
            const Bot *bot = GetByBotId(botId);
            if (bot != nullptr && bot->GetPlayerId() == kInvalidId && bot->GetState() == BotLifeState::Connecting)
                ++arriving;
        }
        return m_cachedConnected + arriving;
    }

    bool BotManager::StartBotConnection(Bot &bot)
    {
        SampBotClient::Config config;
        ResolveLocalEndpoint(config.host, config.port);
        char password[128] = {0};
        if (sampgdk::GetServerVarAsString("password", password, sizeof(password)))
        {
            // SA-MP/open.mp uses the literal value "0" for an unset password.
            // Do not send it as a RakNet connection password.
            if (password[0] != '\0' && std::strcmp(password, "0") != 0)
                config.password = password;
        }

        if (config.port == 0)
        {
            sampgdk::logprintf("[FakeBots] Cannot connect '%s': the server port is unavailable.", bot.GetName().c_str());
            return false;
        }
        if (!bot.Client().Start(config))
        {
            sampgdk::logprintf("[FakeBots] RakNet client start failed for '%s'.", bot.GetName().c_str());
            return false;
        }
        return true;
    }

    void BotManager::ProcessStartQueue()
    {
        if (m_startQueue.empty())
            return;

        int inFlight = 0;
        for (int botId : m_activeBotIds)
        {
            const Bot *bot = GetByBotId(botId);
            if (bot == nullptr)
                continue;
            const auto st = bot->Client().GetState();
            if (st == SampBotClient::State::Connecting || st == SampBotClient::State::AwaitingInit)
                ++inFlight;
        }

        while (!m_startQueue.empty() && inFlight < kMaxHandshakesInFlight)
        {
            const PendingStart next = m_startQueue.front();
            m_startQueue.pop_front();

            Bot *bot = GetByBotId(next.botId);
            if (bot == nullptr || bot->GetGeneration() != next.generation)
                continue; // destroyed before it got its turn

            if (!StartBotConnection(*bot))
            {
                HandleTransportFailure(next.botId, NetFail::kAttemptFailed);
                continue;
            }
            ++inFlight;
        }
    }

    void BotManager::BindNetworkPlayer(int botId, int playerId)
    {
        Bot *bot = GetByBotId(botId);
        const int maxPlayers = sampgdk::GetMaxPlayers();
        if (bot == nullptr || playerId < 0 || playerId >= maxPlayers)
            return;

        auto existing = m_playerIdToBotId.find(playerId);
        if (existing != m_playerIdToBotId.end() && existing->second != botId)
            return;

        bot->SetPlayerId(playerId);
        m_playerIdToBotId[playerId] = botId;
    }

    void BotManager::ResolveConnection(int playerId, const std::string &playerName)
    {
        // Fast path: transport already told us the player slot.
        if (Bot *mapped = GetByPlayerId(playerId))
        {
            if (mapped->GetState() == BotLifeState::Connecting)
            {
                ApplySpawnAndActivate(*mapped);
                if (!mapped->HasConnectForwarded())
                {
                    mapped->MarkConnectForwarded();
                    CallbackDispatcher::Get().OnBotConnect(mapped->GetBotId());
                }
            }
            return;
        }

        // The server's OnPlayerConnect may run before ProcessTick gets a
        // chance to consume ID_CONNECTION_REQUEST_ACCEPTED. Match the exact
        // unique nickname and bind the real server player id here.
        for (int botId : m_activeBotIds)
        {
            Bot *bot = GetByBotId(botId);
            if (bot == nullptr || bot->GetState() != BotLifeState::Connecting)
                continue;
            if (bot->GetName() != playerName)
                continue;

            BindNetworkPlayer(botId, playerId);
            ApplySpawnAndActivate(*bot);
            if (!bot->HasConnectForwarded())
            {
                bot->MarkConnectForwarded();
                CallbackDispatcher::Get().OnBotConnect(botId);
            }
            return;
        }
    }

    bool BotManager::HandlePlayerConnect(int playerId, const std::string &playerName)
    {
        if (GetByPlayerId(playerId) != nullptr)
            return true;

        for (int botId : m_activeBotIds)
        {
            Bot *bot = GetByBotId(botId);
            if (bot != nullptr && bot->GetState() == BotLifeState::Connecting && bot->GetName() == playerName)
            {
                ResolveConnection(playerId, playerName);
                return true;
            }
        }

        m_realPlayers.insert(playerId);
        return false;
    }

    bool BotManager::Destroy(int botId)
    {
        Bot *bot = GetByBotId(botId);
        if (bot == nullptr || bot->GetState() == BotLifeState::Pooled)
            return false;

        const int playerId = bot->GetPlayerId();
        bot->SetState(BotLifeState::Removing);

        if (m_poolingEnabled && playerId != kInvalidId && bot->Client().IsConnected())
        {
            // Keep the real connection alive. No native NPC state is involved.
            if (sampgdk::GetPlayerVehicleID(playerId) != 0)
                sampgdk::RemovePlayerFromVehicle(playerId);
            sampgdk::TogglePlayerControllable(playerId, false);
            sampgdk::SetPlayerVirtualWorld(playerId, 0x7FFFFFFF);
            bot->SetState(BotLifeState::Pooled);
            bot->ResetRuntimeState();

            RemoveActive(botId);
            m_pool.push_back(botId);
            --m_activeCount;
            BotGroupManager::Get().PurgeBot(botId);
            return true;
        }

        if (playerId != kInvalidId)
        {
            m_expectedBotDisconnects.insert(playerId);
            m_playerIdToBotId.erase(playerId);
            bot->Client().Stop(0);
            CallbackDispatcher::Get().OnBotDisconnect(botId, 0);
        }

        RemoveActive(botId);
        if (m_activeCount > 0)
            --m_activeCount;
        BotGroupManager::Get().PurgeBot(botId);
        m_bots[botId].reset();
        return true;
    }

    void BotManager::HandleTransportFailure(int botId, int reason)
    {
        Bot *bot = GetByBotId(botId);
        if (bot == nullptr)
            return;
        const uint64_t generation = bot->GetGeneration();
        for (const auto &existing : m_transportFailures)
        {
            if (existing.botId == botId && existing.generation == generation)
                return;
        }
        m_transportFailures.push_back({botId, generation, reason});
    }

    void BotManager::HandlePlayerRequestClass(int playerId, int classId)
    {
        Bot *bot = GetByPlayerId(playerId);
        if (bot == nullptr || bot->GetState() == BotLifeState::Removing)
            return;
        if (classId < 0 || classId > 299)
            return;

        const PendingSpawn &spawn = bot->GetPendingSpawn();
        sampgdk::SetSpawnInfo(playerId, 0, spawn.skin,
            spawn.x, spawn.y, spawn.z, spawn.angle,
            spawn.weapon, spawn.ammo, 0, 0, 0, 0);
        sampgdk::SetPlayerVirtualWorld(playerId, spawn.virtualWorld);
        sampgdk::SetPlayerInterior(playerId, spawn.interior);
    }

    void BotManager::HandlePlayerDisconnect(int playerId, int reason)
    {
        if (m_expectedBotDisconnects.erase(playerId) != 0)
        {
            m_realPlayers.erase(playerId);
            return;
        }

        auto mapIt = m_playerIdToBotId.find(playerId);
        if (mapIt == m_playerIdToBotId.end())
        {
            m_realPlayers.erase(playerId);
            return;
        }

        const int botId = mapIt->second;
        m_playerIdToBotId.erase(mapIt);
        m_pool.erase(std::remove(m_pool.begin(), m_pool.end(), botId), m_pool.end());

        if (m_bots[botId] != nullptr)
        {
            Bot *bot = m_bots[botId].get();
            const bool wasActive = bot->GetState() != BotLifeState::Pooled;
            if (bot->HasConnectForwarded())
                CallbackDispatcher::Get().OnBotDisconnect(botId, reason);
            bot->Client().Stop(0);
            m_bots[botId].reset();
            RemoveActive(botId);
            if (wasActive && m_activeCount > 0)
                --m_activeCount;
        }
        BotGroupManager::Get().PurgeBot(botId);
    }

    Bot* BotManager::GetByBotId(int botId)
    {
        if (botId < 0 || botId >= kMaxBots)
            return nullptr;
        return m_bots[botId].get();
    }

    Bot* BotManager::GetByPlayerId(int playerId)
    {
        auto it = m_playerIdToBotId.find(playerId);
        return it == m_playerIdToBotId.end() ? nullptr : GetByBotId(it->second);
    }

    bool BotManager::IsValidBotId(int botId) const
    {
        return botId >= 0 && botId < kMaxBots && m_bots[botId] != nullptr;
    }

    bool BotManager::IsPlayerBot(int playerId) const
    {
        return m_playerIdToBotId.find(playerId) != m_playerIdToBotId.end();
    }

    bool BotManager::IsRealPlayer(int playerId) const
    {
        return m_realPlayers.find(playerId) != m_realPlayers.end();
    }

    void BotManager::SeedRealPlayerCache()
    {
        m_realPlayers.clear();
        m_realPlayerPositions.clear();
        m_realPlayerGrid.clear();

        const int maxPlayers = sampgdk::GetMaxPlayers();
        for (int playerId = 0; playerId < maxPlayers; ++playerId)
        {
            if (!sampgdk::IsPlayerConnected(playerId) || IsPlayerBot(playerId))
                continue;

            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (!sampgdk::GetPlayerPos(playerId, &x, &y, &z))
                continue;

            m_realPlayers.insert(playerId);
            m_realPlayerPositions[playerId] = {x, y, z};
            m_realPlayerGrid[SpatialKey(SpatialCell(x), SpatialCell(y))].push_back(playerId);
        }
    }

    bool BotManager::IsAnyRealPlayerWithin(float x, float y, float z, float distance) const
    {
        const float distSq = distance * distance;
        const int minCellX = SpatialCell(x - distance);
        const int maxCellX = SpatialCell(x + distance);
        const int minCellY = SpatialCell(y - distance);
        const int maxCellY = SpatialCell(y + distance);

        for (int cx = minCellX; cx <= maxCellX; ++cx)
        {
            for (int cy = minCellY; cy <= maxCellY; ++cy)
            {
                auto it = m_realPlayerGrid.find(SpatialKey(cx, cy));
                if (it == m_realPlayerGrid.end())
                    continue;
                for (int playerId : it->second)
                {
                    auto pit = m_realPlayerPositions.find(playerId);
                    if (pit == m_realPlayerPositions.end())
                        continue;
                    const SampVector3 &p = pit->second;
                    if (DistanceSquared(x, y, z, p.x, p.y, p.z) <= distSq)
                        return true;
                }
            }
        }
        return false;
    }

    void BotManager::GetNearbyRealPlayers(float x, float y, float z, float radius, std::vector<int>& out) const
    {
        out.clear();
        const float radiusSq = radius * radius;
        const int minCellX = SpatialCell(x - radius);
        const int maxCellX = SpatialCell(x + radius);
        const int minCellY = SpatialCell(y - radius);
        const int maxCellY = SpatialCell(y + radius);

        for (int cx = minCellX; cx <= maxCellX; ++cx)
        {
            for (int cy = minCellY; cy <= maxCellY; ++cy)
            {
                auto it = m_realPlayerGrid.find(SpatialKey(cx, cy));
                if (it == m_realPlayerGrid.end())
                    continue;
                for (int playerId : it->second)
                {
                    auto pit = m_realPlayerPositions.find(playerId);
                    if (pit == m_realPlayerPositions.end())
                        continue;
                    const SampVector3 &p = pit->second;
                    if (DistanceSquared(x, y, z, p.x, p.y, p.z) <= radiusSq)
                        out.push_back(playerId);
                }
            }
        }
    }

    void BotManager::Tick()
    {
        ProcessStartQueue();

        // Pump every RakNet client even while pooled/connecting. RakNet does
        // network I/O on its own thread, but RPC dispatch and lifecycle
        // packets are deliberately consumed on the server thread here.
        // The server loop runs every few milliseconds, a bot only needs its network inbox
        // looked at a hundred times per second (join handshake steps, RPCs, kicks).
        const uint32_t pumpNow = NowMs();
        if (pumpNow - m_lastPumpMs >= kPumpIntervalMs)
        {
            m_lastPumpMs = pumpNow;
            for (auto &slot : m_bots)
            {
                if (slot)
                {
                    slot->Client().Pump();
                    slot->SyncClientStateFromNetwork();
                }
            }
        }

        if (!m_transportFailures.empty())
        {
            const auto failures = std::move(m_transportFailures);
            m_transportFailures.clear();
            for (const auto &failure : failures)
            {
                const int botId = failure.botId;
                const int reason = failure.reason;
                Bot *bot = GetByBotId(botId);
                if (bot != nullptr && bot->GetGeneration() != failure.generation)
                    continue;
                if (bot == nullptr)
                    continue;
                const int playerId = bot->GetPlayerId();

                // A handshake that never completed (a lost datagram, or a stale peer of the
                // same ip:port still known to the server) is simply tried again on a brand
                // new socket; the script only hears about it when every attempt failed.
                const bool handshakeFailure = reason == NetFail::kAttemptFailed ||
                                              reason == NetFail::kStageTimeoutBase - static_cast<int>(SampBotClient::State::Connecting);
                if (playerId == kInvalidId && handshakeFailure && bot->GetConnectRetries() < kMaxConnectRetries)
                {
                    bot->BumpConnectRetries();
                    bot->Client().Stop(0);
                    m_startQueue.push_back({botId, bot->GetGeneration()});
                    continue;
                }

                sampgdk::logprintf("[FakeBots] Bot %d ('%s', player %d): connection ended - %s (code %d).",
                    botId, bot->GetName().c_str(), playerId, DescribeNetFailure(reason).c_str(), reason);
                if (playerId != kInvalidId)
                {
                    m_expectedBotDisconnects.insert(playerId);
                    m_playerIdToBotId.erase(playerId);
                }
                CallbackDispatcher::Get().OnBotDisconnect(botId, reason);
                RemoveActive(botId);
                if (m_activeCount > 0)
                    --m_activeCount;
                BotGroupManager::Get().PurgeBot(botId);
                m_bots[botId]->Client().Stop(0);
                m_bots[botId].reset();
            }
        }

        const uint32_t now = NowMs();
        if (now - m_lastTickMs < m_tickIntervalMs)
            return;

        const float deltaSeconds = (m_lastTickMs == 0)
            ? (m_tickIntervalMs / 1000.0f)
            : ((now - m_lastTickMs) / 1000.0f);
        m_lastTickMs = now;

        const auto tickStart = std::chrono::steady_clock::now();
        m_movingCount = 0;
        m_drivingCount = 0;

        SeedRealPlayerCache();

        for (int botId : m_activeBotIds)
        {
            Bot *bot = GetByBotId(botId);
            if (bot == nullptr || bot->GetState() != BotLifeState::Spawned)
                continue;

            float bx, by, bz;
            bot->GetCachedPosition(bx, by, bz);
            const bool isFar = !IsAnyRealPlayerWithin(bx, by, bz, m_farUpdateDistance);
            float effectiveDelta = deltaSeconds;
            if (isFar)
            {
                bot->AddThrottleTime(deltaSeconds);
                const int counter = bot->GetThrottleCounter() + 1;
                if (counter < kFarUpdateSkipTicks)
                {
                    bot->SetThrottleCounter(counter);
                    continue;
                }
                effectiveDelta = bot->ConsumeThrottleTime(0.0f);
                bot->SetThrottleCounter(0);
            }
            else
            {
                effectiveDelta = bot->ConsumeThrottleTime(deltaSeconds);
                bot->SetThrottleCounter(0);
            }

            if (bot->IsDriving())
            {
                BotMovement::StepDriving(*bot, effectiveDelta);
                ++m_drivingCount;
            }
            else if (bot->IsInCombat())
            {
                BotAI::StepCombat(*bot, effectiveDelta);
            }
            else if (bot->IsFollowing())
            {
                BotMovement::StepFollow(*bot, effectiveDelta);
                ++m_movingCount;
            }
            else if (bot->IsMoving())
            {
                BotMovement::StepOnFoot(*bot, effectiveDelta);
                ++m_movingCount;
            }
            else if (bot->IsIdleWanderEnabled())
            {
                BotAI::StepIdleWander(*bot, effectiveDelta);
            }

            if (!bot->IsDriving())
            {
                if (bot->Client().GetLocalState().inVehicle)
                    bot->SendVehicleIdleSync(now);
                else
                    bot->SendOnFootSync(now);
            }
        }

        const auto tickEnd = std::chrono::steady_clock::now();
        m_lastTickTimeMs = std::chrono::duration<double, std::milli>(tickEnd - tickStart).count();
    }
}
