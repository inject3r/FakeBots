// ============================================================================
// FakeBots :: SampBotClient.h
//
// A headless SA-MP 0.3.7 / open.mp client that joins the *local* server through
// the normal network path (UDP + RakNet), exactly like a game client does:
//
//     open connection (cookie)  ->  connection request (+password)
//     ->  ID_AUTH_KEY challenge/response  ->  connection accepted
//     ->  ClientJoin RPC  ->  InitGame  ->  RequestClass  ->  RequestSpawn
//     ->  Spawn  ->  on-foot / in-car / passenger sync
//
// The server therefore sees an ordinary player.  No NPC slot, NPC mode or
// ConnectNPC path is involved anywhere.
//
// All packet layouts in this file were derived from the server's own netcode
// definitions (open.mp  Shared/NetCode) and verified against a live server.
// ============================================================================
#pragma once

#include "../core/Common.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

// RakNet is an implementation detail of this class; its headers (and the
// platform headers they drag in) are only included by SampBotClient.cpp.
namespace RakNet
{
    class BitStream;
    class RakClientInterface;
    struct RPCParameters;
}

namespace FakeBots
{
    struct SampVector3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct SampBotLocalState
    {
        SampVector3 position{};
        SampVector3 velocity{};
        float facingAngle = 0.0f;
        float health = 100.0f;
        float armour = 0.0f;
        float vehicleHealth = 1000.0f;
        uint8_t weapon = 0;
        uint8_t specialAction = 0;
        uint16_t keys = 0;
        uint16_t leftRight = 0; // pad analog, two's complement (0xFF80 == -128)
        uint16_t upDown = 0;
        uint16_t vehicleId = 0;
        uint8_t vehicleSeat = 0;
        bool inVehicle = false;
        bool passenger = false;
        bool controllable = true;
        int interior = 0;
    };

    // Failure / disconnect reasons reported through the transport callback.
    //   > 0   a RakNet message id (29 attempt failed, 31 server full, 32 closed
    //         by the server / kicked, 33 connection lost, 36 banned, 37 bad
    //         password)
    //   <= -1000 && > -2000   a stage timed out (-1000 - stage)
    //   <= -2000              the server rejected the join (-2000 - reason):
    //                         1 version, 2 nickname, 3 modification, 4 no slot
    namespace NetFail
    {
        constexpr int kAttemptFailed = 29;
        constexpr int kNoFreeSlot = 31;
        constexpr int kClosedByServer = 32;
        constexpr int kConnectionLost = 33;
        constexpr int kBanned = 36;
        constexpr int kInvalidPassword = 37;
        constexpr int kStageTimeoutBase = -1000;
        constexpr int kJoinRejectedBase = -2000;
    }

    class SampBotClient
    {
    public:
        enum class State : uint8_t
        {
            Disconnected,
            Connecting,
            AwaitingInit,
            AwaitingClassResponse,
            AwaitingSpawnResponse,
            Spawned,
            Idle, // connected, class selected, spawn was not granted (yet)
            Failed,
        };

        struct Config
        {
            std::string host = "127.0.0.1"; // always the local server
            uint16_t port = 0;
            std::string password;

            std::string clientVersion = "0.3.7-R2";
            uint32_t netgameVersion = 4057;

            // Serial ("gpci") sent in ClientJoin. If empty a unique, valid
            // value is generated (hex number divisible by 1001, as the server
            // requires).
            std::string authKey;

            // Every bot owns a RakNet network thread that wakes up this often (ms).
            // 20 ms keeps hundreds of bots cheap and is far below the 50 ms sync period.
            int threadSleepMs = 20;
            uint32_t connectTimeoutMs = 20000;
            uint32_t initTimeoutMs = 15000;
            uint32_t spawnTimeoutMs = 15000;

            uint32_t defaultOnFootRateMs = 50;
            uint32_t defaultInCarRateMs = 50;
            uint32_t idleHeartbeatMs = 250;
        };

        using PlayerIdCallback = std::function<void(int)>;
        using TransportCallback = std::function<void(int)>;

        explicit SampBotClient(std::string name);
        ~SampBotClient();

        SampBotClient(const SampBotClient&) = delete;
        SampBotClient& operator=(const SampBotClient&) = delete;

        bool Start(const Config& config);
        // Disconnects. The slow part (notification + thread join) runs on a
        // background worker so the server thread is never blocked.
        void Stop(unsigned int blockDurationMs = 0);
        void Pump();

        // Blocks until every background disconnect has finished (plugin unload).
        static void FlushBackgroundDisconnects();

        bool BeginSpawnSelection(int classId = 0);
        // Recycling an already spawned connection: wait for the server's forced spawn.
        bool BeginForcedSpawn();
        void ConfirmServerSpawn();
        bool IsConnected() const;
        bool IsSpawned() const { return m_state == State::Spawned; }
        State GetState() const { return m_state; }
        int GetPlayerId() const { return m_playerId; }
        const std::string& GetName() const { return m_name; }
        void SetName(const std::string& name) { m_name = name; }
        int GetPing();

        const SampBotLocalState& GetLocalState() const { return m_local; }
        void SetLocalState(const SampBotLocalState& state) { m_local = state; }

        void ResetForSpawn(float x, float y, float z, float facing,
                           float health = 100.0f, float armour = 0.0f,
                           uint8_t weapon = 0);

        bool SendOnFootSync(uint32_t nowMs);
        // quatWxyz (optional): the vehicle's exact orientation (w, x, y, z). When it
        // is null the orientation is derived from facingAngle (flat ground).
        bool SendInCarSync(int vehicleId, const SampVector3& position,
                           const SampVector3& velocity, float vehicleHealth,
                           float facingAngle, uint8_t weapon, uint16_t keys,
                           bool siren = false, bool landingGear = false,
                           const float* quatWxyz = nullptr);
        bool SendPassengerSync(int vehicleId, int seat, const SampVector3& position);
        bool SendChat(const char* message);
        // Reports the bot's own death to the server (what a game client does
        // when the ped dies). killerId may be kInvalidId.
        bool SendDeath(int reason, int killerId);

        void SetPlayerIdCallback(PlayerIdCallback callback) { m_onPlayerId = std::move(callback); }
        void SetTransportCallback(TransportCallback callback) { m_onTransport = std::move(callback); }

    private:
        // RPC ids (server -> client unless noted)
        static constexpr unsigned char kRpcSetPlayerName = 11;
        static constexpr unsigned char kRpcSetPlayerPos = 12;
        static constexpr unsigned char kRpcSetPlayerPosFindZ = 13;
        static constexpr unsigned char kRpcSetPlayerHealth = 14;
        static constexpr unsigned char kRpcTogglePlayerControllable = 15;
        static constexpr unsigned char kRpcSetPlayerFacingAngle = 19;
        static constexpr unsigned char kRpcResetPlayerWeapons = 21;
        static constexpr unsigned char kRpcGivePlayerWeapon = 22;
        static constexpr unsigned char kRpcClientJoin = 25;       // client -> server
        static constexpr unsigned char kRpcServerCommand = 50;    // client -> server
        static constexpr unsigned char kRpcSpawn = 52;            // client -> server
        static constexpr unsigned char kRpcDeath = 53;            // client -> server
        static constexpr unsigned char kRpcSetPlayerArmour = 66;
        static constexpr unsigned char kRpcSetPlayerArmedWeapon = 67;
        static constexpr unsigned char kRpcPutPlayerInVehicle = 70;
        static constexpr unsigned char kRpcRemovePlayerFromVehicle = 71;
        static constexpr unsigned char kRpcApplyAnimation = 86;
        static constexpr unsigned char kRpcSetPlayerSpecialAction = 88;
        static constexpr unsigned char kRpcSetPlayerVelocity = 90;
        static constexpr unsigned char kRpcSetVehicleVelocity = 91;
        static constexpr unsigned char kRpcChat = 101;            // client -> server
        static constexpr unsigned char kRpcClientCheck = 103;     // both ways
        static constexpr unsigned char kRpcRequestClass = 128;    // both ways
        static constexpr unsigned char kRpcRequestSpawn = 129;    // both ways
        static constexpr unsigned char kRpcConnectionRejected = 130;
        static constexpr unsigned char kRpcInitGame = 139;
        static constexpr unsigned char kRpcSetPlayerAmmo = 145;
        static constexpr unsigned char kRpcSetVehicleHealth = 147;
        static constexpr unsigned char kRpcSetPlayerSkin = 153;
        static constexpr unsigned char kRpcSetPlayerInterior = 156;

        // sync packet ids (client -> server)
        static constexpr unsigned char kPacketPlayerInVehicle = 200;
        static constexpr unsigned char kPacketPlayerOnFoot = 207;
        static constexpr unsigned char kPacketPlayerPassenger = 211;

        static constexpr uint32_t kMinSyncIntervalMs = 20;
        static constexpr uint32_t kMaxSyncIntervalMs = 1000;

        // Server -> client RPCs this client reacts to.
        static constexpr unsigned char kHandledRpcs[] = {
            11, 12, 13, 14, 15, 19, 21, 22, 66, 67, 70, 71, 88, 90, 91, 103,
            128, 129, 130, 139, 145, 147, 153, 156};
        static constexpr size_t kHandledRpcCount = sizeof(kHandledRpcs) / sizeof(kHandledRpcs[0]);

        struct RpcBinding
        {
            SampBotClient* client = nullptr;
            unsigned char id = 0;
        };
        std::array<RpcBinding, kHandledRpcCount> m_rpcBindings{};

        void Dispatch(unsigned char rpcId, RakNet::RPCParameters* params);

        void HandleInitGame(RakNet::BitStream& bs);
        void HandleRequestClassResponse(RakNet::BitStream& bs);
        void HandleRequestSpawnResponse(RakNet::BitStream& bs);
        void HandleConnectionRejected(RakNet::BitStream& bs);
        void HandleClientCheck(RakNet::BitStream& bs);
        void HandleSetPlayerHealth(RakNet::BitStream& bs);
        void HandlePutPlayerInVehicle(RakNet::BitStream& bs);

        bool SendClientJoin(unsigned int challenge);
        bool SendRequestClass(int32_t classId);
        bool SendRequestSpawn();
        bool SendSpawnRpc();
        bool SendRpc(unsigned char rpcId, RakNet::BitStream& bs);
        bool SendSync(RakNet::BitStream& bs);

        void RegisterRpcHandlers();
        void UnregisterRpcHandlers();
        void SetFailed(int reason);
        void MarkStageDeadline(uint32_t timeoutMs);
        void EnterState(State state);

        static float ClampAngle(float degrees);
        static void FacingToQuaternion(float facingDegrees, float (&wxyz)[4]);
        static void WriteQuaternion(RakNet::BitStream& bs, const float (&wxyz)[4]);
        static uint32_t NowMs();
        static std::string GenerateSerial();

        std::string m_name;
        Config m_config{};
        RakNet::RakClientInterface* m_client = nullptr;
        State m_state = State::Disconnected;
        int m_playerId = kInvalidId;

        bool m_failureNotified = false;
        bool m_dead = false;
        bool m_playerIdAnnounced = false;

        uint32_t m_stageDeadlineMs = 0;
        bool m_hasDeadline = false;
        uint32_t m_onFootRateMs = 50;
        uint32_t m_inCarRateMs = 50;
        uint32_t m_lastOnFootSyncMs = 0;
        uint32_t m_lastInCarSyncMs = 0;
        uint32_t m_lastAnySyncMs = 0;
        bool m_hasSynced = false;

        SampBotLocalState m_local{};
        PlayerIdCallback m_onPlayerId;
        TransportCallback m_onTransport;
    };
}
