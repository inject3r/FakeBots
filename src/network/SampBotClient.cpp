// ============================================================================
// FakeBots :: SampBotClient.cpp
// ============================================================================
#include "SampBotClient.h"

#include <SAMPRakNet.hpp>

#include <BitStream.h>
#include <NetworkTypes.h>
#include <PacketEnumerations.h>
#include <RakClientInterface.h>
#include <RakNetworkFactory.h>
#include <RakSleep.h>
#include <GetTime.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <random>
#include <thread>

namespace FakeBots
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        float Clamp100(float value)
        {
            if (!std::isfinite(value))
                return 0.0f;
            return std::max(0.0f, std::min(100.0f, value));
        }

        float Clamp1000(float value)
        {
            if (!std::isfinite(value))
                return 0.0f;
            return std::max(0.0f, std::min(1000.0f, value));
        }

        // The server drops sync packets whose position is outside these ranges.
        float ClampPos(float v, float lo, float hi)
        {
            if (!std::isfinite(v))
                return 0.0f;
            return std::max(lo, std::min(hi, v));
        }

        // ...and velocity vectors longer than 100 units.
        SampVector3 SanitizeVelocity(SampVector3 v)
        {
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
                return {};
            const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (len > 99.0f)
            {
                const float k = 99.0f / len;
                v.x *= k;
                v.y *= k;
                v.z *= k;
            }
            return v;
        }

        bool ReadVec3(RakNet::BitStream& bs, SampVector3& v)
        {
            return bs.Read(v.x) && bs.Read(v.y) && bs.Read(v.z);
        }

        bool IsValidPlayerId(int id)
        {
            return id >= 0 && id <= 1000;
        }

        size_t Utf8PrefixLength(const char* text, size_t maxBytes)
        {
            if (text == nullptr)
                return 0;
            const size_t full = std::strlen(text);
            if (full <= maxBytes)
                return full;
            size_t end = maxBytes;
            while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U)
                --end;
            return end;
        }

        // RakNet keeps a few process-wide, reference counted singletons (string compressor,
        // string table, RNG seed) that every RakPeer touches in its constructor/destructor
        // WITHOUT locking. Bots are created on the server thread and destroyed on the
        // disconnect worker, so those two moments are serialised here.
        std::recursive_mutex& RakLifecycleMutex()
        {
            static std::recursive_mutex m;
            return m;
        }

        // ---------------------------------------------------------------
        // Background disconnect worker.
        //
        // RakPeer::Disconnect() waits for the notification to be flushed and
        // the destructor joins the network thread (>= 15 ms each).  Doing that
        // for dozens of bots on the server thread would stall the whole
        // server, so the finished clients are handed to this worker instead.
        // ---------------------------------------------------------------
        class DisconnectWorker
        {
        public:
            static DisconnectWorker& Get()
            {
                static DisconnectWorker w;
                return w;
            }

            void Submit(RakNet::RakClientInterface* client, unsigned int blockMs)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_thread.joinable())
                    m_thread = std::thread(&DisconnectWorker::Run, this);
                m_queue.push_back({client, blockMs});
                m_pending.fetch_add(1);
                m_cv.notify_one();
            }

            void Flush()
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_idleCv.wait(lock, [this] { return m_pending.load() == 0; });
                m_stop = true;
                m_cv.notify_all();
                lock.unlock();
                if (m_thread.joinable())
                    m_thread.join();
                lock.lock();
                m_stop = false;
                m_thread = std::thread();
            }

            ~DisconnectWorker()
            {
                if (m_thread.joinable())
                {
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_stop = true;
                    }
                    m_cv.notify_all();
                    m_thread.join();
                }
            }

        private:
            struct Item
            {
                RakNet::RakClientInterface* client;
                unsigned int blockMs;
            };

            void Run()
            {
                for (;;)
                {
                    Item item{};
                    {
                        std::unique_lock<std::mutex> lock(m_mutex);
                        m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
                        if (m_queue.empty())
                            return; // m_stop
                        item = m_queue.front();
                        m_queue.pop_front();
                    }
                    item.client->Disconnect(item.blockMs);
                    {
                        std::lock_guard<std::recursive_mutex> lifecycle(RakLifecycleMutex());
                        RakNet::RakNetworkFactory::DestroyRakClientInterface(item.client);
                    }
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_pending.fetch_sub(1);
                    }
                    m_idleCv.notify_all();
                }
            }

            std::mutex m_mutex;
            std::condition_variable m_cv;
            std::condition_variable m_idleCv;
            std::deque<Item> m_queue;
            std::atomic<int> m_pending{0};
            std::thread m_thread;
            bool m_stop = false;
        };
    }

    // ------------------------------------------------------------------
    //  Helpers
    // ------------------------------------------------------------------
    uint32_t SampBotClient::NowMs()
    {
        using namespace std::chrono;
        return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }

    float SampBotClient::ClampAngle(float degrees)
    {
        if (!std::isfinite(degrees))
            return 0.0f;
        degrees = std::fmod(degrees, 360.0f);
        if (degrees < 0.0f)
            degrees += 360.0f;
        return degrees;
    }

    // Rotation about the Z axis in GTA's convention (identical to the server's
    // GTAQuat(0, 0, angle)): w = cos(a/2), z = -sin(a/2).
    void SampBotClient::FacingToQuaternion(float facingDegrees, float (&wxyz)[4])
    {
        const float half = ClampAngle(facingDegrees) * kPi / 180.0f * -0.5f;
        wxyz[0] = std::cos(half);
        wxyz[1] = 0.0f;
        wxyz[2] = 0.0f;
        wxyz[3] = std::sin(half);
    }

    // Wire order: w, x, y, z (the open.mp SDK builds with GLM_FORCE_QUAT_DATA_WXYZ, which
    // is the layout of the original SA-MP structures).
    void SampBotClient::WriteQuaternion(RakNet::BitStream& bs, const float (&wxyz)[4])
    {
        bs.Write(wxyz[0]); // w
        bs.Write(wxyz[1]); // x
        bs.Write(wxyz[2]); // y
        bs.Write(wxyz[3]); // z
    }

    // A "gpci" value: the server only accepts hexadecimal serials that are
    // divisible by 1001 (that is how genuine clients generate them).
    std::string SampBotClient::GenerateSerial()
    {
        static std::mutex rngMutex;
        static std::mt19937_64 rng{[]
        {
            std::random_device rd;
            return (static_cast<uint64_t>(rd()) << 32U) ^ rd() ^
                   static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        }()};

        // 37 random hex digits, first one non-zero
        int digits[48] = {};
        const int n = 37;
        {
            std::lock_guard<std::mutex> lock(rngMutex);
            for (int i = 0; i < n; ++i)
                digits[i] = static_cast<int>(rng() & 0xF);
        }
        if (digits[0] == 0)
            digits[0] = 1;

        // multiply the big hex number by 1001 (little-endian accumulate)
        std::string out;
        unsigned carry = 0;
        for (int i = n - 1; i >= 0; --i)
        {
            const unsigned v = static_cast<unsigned>(digits[i]) * 1001u + carry;
            out.push_back("0123456789ABCDEF"[v & 0xF]);
            carry = v >> 4;
        }
        while (carry)
        {
            out.push_back("0123456789ABCDEF"[carry & 0xF]);
            carry >>= 4;
        }
        std::reverse(out.begin(), out.end());
        return out;
    }

    // ------------------------------------------------------------------
    //  Lifecycle
    // ------------------------------------------------------------------
    SampBotClient::SampBotClient(std::string name)
        : m_name(std::move(name))
    {
    }

    SampBotClient::~SampBotClient()
    {
        Stop(0);
    }

    void SampBotClient::FlushBackgroundDisconnects()
    {
        DisconnectWorker::Get().Flush();
    }

    void SampBotClient::EnterState(State state)
    {
        m_state = state;
    }

    bool SampBotClient::Start(const Config& config)
    {
        Stop(0);

        if (config.host.empty() || config.port == 0 || m_name.empty())
        {
            m_state = State::Failed;
            return false;
        }

        m_config = config;
        if (m_config.authKey.empty())
            m_config.authKey = GenerateSerial();

        m_playerId = kInvalidId;
        m_failureNotified = false;
        m_dead = false;
        m_playerIdAnnounced = false;
        m_hasSynced = false;
        m_onFootRateMs = std::clamp(m_config.defaultOnFootRateMs, kMinSyncIntervalMs, kMaxSyncIntervalMs);
        m_inCarRateMs = std::clamp(m_config.defaultInCarRateMs, kMinSyncIntervalMs, kMaxSyncIntervalMs);
        m_lastOnFootSyncMs = 0;
        m_lastInCarSyncMs = 0;
        m_lastAnySyncMs = 0;
        m_local = SampBotLocalState{};

        std::lock_guard<std::recursive_mutex> lifecycle(RakLifecycleMutex());
        RakNet::GetTime(); // initialise RakNet's clock once, on this thread

        m_client = RakNet::RakNetworkFactory::GetRakClientInterface();
        if (m_client == nullptr)
        {
            m_state = State::Failed;
            return false;
        }

        m_client->SetPassword(m_config.password.empty() ? nullptr : m_config.password.c_str());
        RegisterRpcHandlers();
        m_client->StopOccasionalPing();

        // Connection attempts: one request per second until the limit below.
        const uint32_t seconds = std::max<uint32_t>(3, m_config.connectTimeoutMs / 1000);
        SAMPRakNet::SetMaxConnectionAttempts(seconds);

        if (!m_client->Connect(m_config.host.c_str(), m_config.port, 0, 0, std::max(1, m_config.threadSleepMs)))
        {
            UnregisterRpcHandlers();
            RakNet::RakNetworkFactory::DestroyRakClientInterface(m_client);
            m_client = nullptr;
            m_state = State::Failed;
            return false;
        }

        EnterState(State::Connecting);
        MarkStageDeadline(m_config.connectTimeoutMs + 2000);
        return true;
    }

    void SampBotClient::Stop(unsigned int blockDurationMs)
    {
        if (m_client != nullptr)
        {
            UnregisterRpcHandlers();
            RakNet::RakClientInterface* client = m_client;
            m_client = nullptr;
            // Slow work (notification flush, thread join) happens off-thread.
            DisconnectWorker::Get().Submit(client, blockDurationMs > 0 ? blockDurationMs : 150);
        }

        m_state = State::Disconnected;
        m_playerId = kInvalidId;
        m_failureNotified = false;
        m_dead = false;
        m_playerIdAnnounced = false;
        m_hasDeadline = false;
        m_hasSynced = false;
    }

    bool SampBotClient::IsConnected() const
    {
        return m_client != nullptr && m_client->IsConnected();
    }

    int SampBotClient::GetPing()
    {
        return m_client != nullptr ? m_client->GetLastPing() : -1;
    }

    void SampBotClient::MarkStageDeadline(uint32_t timeoutMs)
    {
        m_stageDeadlineMs = NowMs() + std::max<uint32_t>(1000, timeoutMs);
        m_hasDeadline = true;
    }

    void SampBotClient::SetFailed(int reason)
    {
        if (m_state == State::Failed || m_state == State::Disconnected)
            return;
        m_state = State::Failed;
        m_hasDeadline = false;
        if (!m_failureNotified)
        {
            m_failureNotified = true;
            if (m_onTransport)
                m_onTransport(reason);
        }
    }

    void SampBotClient::ConfirmServerSpawn()
    {
        if (m_state == State::Disconnected || m_state == State::Failed)
            return;
        m_state = State::Spawned;
        m_dead = false;
        m_hasDeadline = false;
    }

    void SampBotClient::ResetForSpawn(float x, float y, float z, float facing,
                                      float health, float armour, uint8_t weapon)
    {
        m_local.position = {x, y, z};
        m_local.velocity = {};
        m_local.facingAngle = ClampAngle(facing);
        m_local.health = Clamp100(health);
        m_local.armour = Clamp100(armour);
        m_local.weapon = weapon;
        m_local.keys = 0;
        m_local.leftRight = 0;
        m_local.upDown = 0;
        m_local.specialAction = 0;
        m_local.inVehicle = false;
        m_local.passenger = false;
        m_local.vehicleId = 0;
        m_local.vehicleSeat = 0;
        m_local.controllable = true;
        m_dead = false;
    }

    // ------------------------------------------------------------------
    //  Main-thread pump
    // ------------------------------------------------------------------
    void SampBotClient::Pump()
    {
        if (m_client == nullptr)
            return;

        const uint32_t now = NowMs();
        if (m_hasDeadline &&
            (m_state == State::Connecting || m_state == State::AwaitingInit ||
             m_state == State::AwaitingClassResponse || m_state == State::AwaitingSpawnResponse) &&
            static_cast<int32_t>(now - m_stageDeadlineMs) > 0)
        {
            SetFailed(NetFail::kStageTimeoutBase - static_cast<int>(m_state));
            return;
        }

        for (int i = 0; i < 256; ++i)
        {
            RakNet::Packet* packet = m_client->Receive(); // also runs the RPC handlers
            if (packet == nullptr)
                break;

            if (packet->data == nullptr || packet->length == 0)
            {
                m_client->DeallocatePacket(packet);
                continue;
            }

            const unsigned char packetId = packet->data[0];
            switch (packetId)
            {
                case RakNet::ID_CONNECTION_REQUEST_ACCEPTED:
                {
                    // [id][u32 ip][u16 port][u16 playerIndex][u32 challenge]
                    if (packet->length < 13 || m_state != State::Connecting)
                        break;
                    RakNet::BitStream accepted(packet->data, packet->length, false);
                    accepted.IgnoreBits(8);
                    accepted.IgnoreBits(8 * (sizeof(unsigned int) + sizeof(unsigned short)));
                    unsigned short slotIndex = 0;
                    unsigned int challenge = 0;
                    if (!accepted.Read(slotIndex) || !accepted.Read(challenge))
                    {
                        SetFailed(static_cast<int>(RakNet::ID_CONNECTION_REQUEST_ACCEPTED));
                        break;
                    }

                    EnterState(State::AwaitingInit);
                    MarkStageDeadline(m_config.initTimeoutMs);
                    if (!SendClientJoin(challenge))
                        SetFailed(-1001);
                    break;
                }

                case RakNet::ID_CONNECTION_ATTEMPT_FAILED:
                case RakNet::ID_NO_FREE_INCOMING_CONNECTIONS:
                case RakNet::ID_INVALID_PASSWORD:
                case RakNet::ID_CONNECTION_BANNED:
                case RakNet::ID_DISCONNECTION_NOTIFICATION:
                case RakNet::ID_CONNECTION_LOST:
                    SetFailed(static_cast<int>(packetId));
                    break;

                default:
                    break;
            }

            m_client->DeallocatePacket(packet);
            if (m_client == nullptr || m_state == State::Failed)
                return;
        }

        // Keep-alive: an idle player still reports its state a few times per
        // second (a stopped sync stream looks like a frozen/AFK client).
        if (m_state == State::Spawned && !m_dead && m_config.idleHeartbeatMs != 0 &&
            (!m_hasSynced || now - m_lastAnySyncMs >= m_config.idleHeartbeatMs))
        {
            if (m_local.inVehicle)
            {
                if (m_local.passenger)
                    SendPassengerSync(m_local.vehicleId, m_local.vehicleSeat, m_local.position);
            }
            else
            {
                SendOnFootSync(now);
            }
        }
    }

    // Used when a living, already spawned connection is recycled: the script-side
    // SpawnPlayer() makes the server answer with a forced RequestSpawn (allow = 2),
    // which HandleRequestSpawnResponse() turns into the Spawn RPC.
    bool SampBotClient::BeginForcedSpawn()
    {
        if (m_client == nullptr || !IsConnected())
            return false;
        if (m_state == State::Connecting || m_state == State::AwaitingInit || m_state == State::Failed)
            return false;

        m_dead = false;
        m_hasSynced = false;
        EnterState(State::AwaitingSpawnResponse);
        MarkStageDeadline(m_config.spawnTimeoutMs);
        return true;
    }

    bool SampBotClient::BeginSpawnSelection(int classId)
    {
        if (m_client == nullptr || !IsConnected())
            return false;
        if (m_state == State::Connecting || m_state == State::AwaitingInit || m_state == State::Failed)
            return false;

        m_dead = false;
        EnterState(State::AwaitingClassResponse);
        MarkStageDeadline(m_config.spawnTimeoutMs);
        return SendRequestClass(classId);
    }

    // ------------------------------------------------------------------
    //  Client -> server
    // ------------------------------------------------------------------
    bool SampBotClient::SendRpc(unsigned char rpcId, RakNet::BitStream& bs)
    {
        if (m_client == nullptr || !IsConnected())
            return false;
        return m_client->RPC(static_cast<RakNet::RPCID>(rpcId), &bs, RakNet::HIGH_PRIORITY,
                             RakNet::RELIABLE_ORDERED, 0, false, RakNet::UNASSIGNED_NETWORK_ID, nullptr);
    }

    bool SampBotClient::SendSync(RakNet::BitStream& bs)
    {
        if (m_client == nullptr || !IsConnected())
            return false;
        return m_client->Send(&bs, RakNet::HIGH_PRIORITY, RakNet::UNRELIABLE_SEQUENCED, 0);
    }

    bool SampBotClient::SendClientJoin(unsigned int challenge)
    {
        if (m_client == nullptr)
            return false;

        RakNet::BitStream bs;
        const uint32_t version = m_config.netgameVersion;
        const uint8_t modded = 1;
        const uint8_t nameLen = static_cast<uint8_t>(std::min<size_t>(m_name.size(), MAX_PLAYER_NAME));
        const uint32_t challengeResponse = challenge ^ version;
        const uint8_t authLen = static_cast<uint8_t>(std::min<size_t>(m_config.authKey.size(), 255));
        const uint8_t verLen = static_cast<uint8_t>(std::min<size_t>(m_config.clientVersion.size(), 24));

        bs.Write(version);
        bs.Write(modded);
        bs.Write(nameLen);
        bs.Write(m_name.data(), nameLen);
        bs.Write(challengeResponse);
        bs.Write(authLen);
        bs.Write(m_config.authKey.data(), authLen);
        bs.Write(verLen);
        bs.Write(m_config.clientVersion.data(), verLen);
        return SendRpc(kRpcClientJoin, bs);
    }

    bool SampBotClient::SendRequestClass(int32_t classId)
    {
        RakNet::BitStream bs;
        bs.Write(classId); // genuine clients send a 32 bit class id
        return SendRpc(kRpcRequestClass, bs);
    }

    bool SampBotClient::SendRequestSpawn()
    {
        RakNet::BitStream bs;
        return SendRpc(kRpcRequestSpawn, bs);
    }

    bool SampBotClient::SendSpawnRpc()
    {
        RakNet::BitStream bs;
        return SendRpc(kRpcSpawn, bs);
    }

    bool SampBotClient::SendDeath(int reason, int killerId)
    {
        if (m_client == nullptr || !IsConnected() || m_dead)
            return false;
        RakNet::BitStream bs;
        const uint8_t r = static_cast<uint8_t>(std::clamp(reason, 0, 255));
        const uint16_t k = (killerId >= 0 && killerId <= 1000) ? static_cast<uint16_t>(killerId) : static_cast<uint16_t>(0xFFFF);
        bs.Write(r);
        bs.Write(k);
        m_dead = true;
        m_local.health = 0.0f;
        return SendRpc(kRpcDeath, bs);
    }

    bool SampBotClient::SendChat(const char* message)
    {
        if (message == nullptr || message[0] == '\0' || m_client == nullptr || !IsConnected())
            return false;
        if (m_state == State::Connecting || m_state == State::AwaitingInit)
            return false;

        if (message[0] == '/')
        {
            // Slash commands travel in their own RPC: [u32 length][text]
            const size_t len = Utf8PrefixLength(message, 255);
            if (len < 2)
                return false;
            RakNet::BitStream bs;
            bs.Write(static_cast<uint32_t>(len));
            bs.Write(message, static_cast<int>(len));
            return SendRpc(kRpcServerCommand, bs);
        }

        const size_t len = Utf8PrefixLength(message, 127);
        if (len == 0)
            return false;
        RakNet::BitStream bs;
        bs.Write(static_cast<uint8_t>(len));
        bs.Write(message, static_cast<int>(len));
        return SendRpc(kRpcChat, bs);
    }

    // PLAYER_SYNC (207), client -> server:
    //   u16 leftRight, u16 upDown, u16 keys, f32 pos[3], quaternion[4],
    //   u8 health, u8 armour, u8 weapon(6 bits)|additionalKey(2 bits),
    //   u8 specialAction, f32 velocity[3], f32 surfOffset[3], u16 surfId,
    //   u16 animationId, u16 animationFlags
    bool SampBotClient::SendOnFootSync(uint32_t nowMs)
    {
        if (m_client == nullptr || m_state != State::Spawned || m_dead || m_local.inVehicle)
            return false;
        if (m_hasSynced && static_cast<uint32_t>(nowMs - m_lastOnFootSyncMs) < m_onFootRateMs)
            return false;

        const SampVector3 pos{ClampPos(m_local.position.x, -19990.0f, 19990.0f),
                              ClampPos(m_local.position.y, -19990.0f, 19990.0f),
                              ClampPos(m_local.position.z, -900.0f, 190000.0f)};
        const SampVector3 vel = SanitizeVelocity(m_local.velocity);
        float quat[4];
        FacingToQuaternion(m_local.facingAngle, quat);

        RakNet::BitStream bs;
        bs.Write(static_cast<uint8_t>(kPacketPlayerOnFoot));
        bs.Write(m_local.leftRight);
        bs.Write(m_local.upDown);
        bs.Write(m_local.keys);
        bs.Write(pos.x);
        bs.Write(pos.y);
        bs.Write(pos.z);
        WriteQuaternion(bs, quat);
        bs.Write(static_cast<uint8_t>(std::lround(Clamp100(m_local.health))));
        bs.Write(static_cast<uint8_t>(std::lround(Clamp100(m_local.armour))));
        bs.Write(static_cast<uint8_t>(m_local.weapon & 0x3F));
        bs.Write(m_local.specialAction);
        bs.Write(vel.x);
        bs.Write(vel.y);
        bs.Write(vel.z);
        bs.Write(0.0f); // surfing offset
        bs.Write(0.0f);
        bs.Write(0.0f);
        bs.Write(static_cast<uint16_t>(0)); // surfing vehicle/object id
        bs.Write(static_cast<uint16_t>(0)); // animation id
        bs.Write(static_cast<uint16_t>(0)); // animation flags

        if (!SendSync(bs))
            return false;
        m_lastOnFootSyncMs = nowMs;
        m_lastAnySyncMs = nowMs;
        m_hasSynced = true;
        return true;
    }

    // VEHICLE_SYNC (200), client -> server:
    //   u16 vehicleId, u16 leftRight, u16 upDown, u16 keys, quaternion[4],
    //   f32 pos[3], f32 velocity[3], f32 vehicleHealth, u8 playerHealth,
    //   u8 playerArmour, u8 weapon|additionalKey, u8 siren, u8 landingGear,
    //   u16 trailerId, u32 hydraThrustAngle/trainSpeed
    bool SampBotClient::SendInCarSync(int vehicleId, const SampVector3& position,
                                      const SampVector3& velocity, float vehicleHealth,
                                      float facingAngle, uint8_t weapon, uint16_t keys,
                                      bool siren, bool landingGear, const float* quatWxyz)
    {
        if (m_client == nullptr || m_state != State::Spawned || m_dead || vehicleId <= 0 || vehicleId > 2000)
            return false;
        const uint32_t nowMs = NowMs();
        if (m_hasSynced && static_cast<uint32_t>(nowMs - m_lastInCarSyncMs) < m_inCarRateMs)
            return false;

        const SampVector3 pos{ClampPos(position.x, -19990.0f, 19990.0f),
                              ClampPos(position.y, -19990.0f, 19990.0f),
                              ClampPos(position.z, -900.0f, 190000.0f)};
        const SampVector3 vel = SanitizeVelocity(velocity);
        float quat[4];
        if (quatWxyz != nullptr && std::isfinite(quatWxyz[0]) && std::isfinite(quatWxyz[1]) &&
            std::isfinite(quatWxyz[2]) && std::isfinite(quatWxyz[3]))
        {
            std::memcpy(quat, quatWxyz, sizeof(quat));
        }
        else
        {
            FacingToQuaternion(facingAngle, quat);
        }

        RakNet::BitStream bs;
        bs.Write(static_cast<uint8_t>(kPacketPlayerInVehicle));
        bs.Write(static_cast<uint16_t>(vehicleId));
        bs.Write(m_local.leftRight);
        bs.Write(m_local.upDown);
        bs.Write(keys);
        WriteQuaternion(bs, quat);
        bs.Write(pos.x);
        bs.Write(pos.y);
        bs.Write(pos.z);
        bs.Write(vel.x);
        bs.Write(vel.y);
        bs.Write(vel.z);
        bs.Write(Clamp1000(vehicleHealth));
        bs.Write(static_cast<uint8_t>(std::lround(Clamp100(m_local.health))));
        bs.Write(static_cast<uint8_t>(std::lround(Clamp100(m_local.armour))));
        bs.Write(static_cast<uint8_t>(weapon & 0x3F));
        bs.Write(static_cast<uint8_t>(siren ? 1 : 0));
        bs.Write(static_cast<uint8_t>(landingGear ? 1 : 0));
        bs.Write(static_cast<uint16_t>(0)); // trailer
        bs.Write(static_cast<uint32_t>(0)); // hydra thrust angle / train speed

        if (!SendSync(bs))
            return false;
        m_lastInCarSyncMs = nowMs;
        m_lastAnySyncMs = nowMs;
        m_hasSynced = true;
        m_local.position = position;
        return true;
    }

    // PASSENGER_SYNC (211), client -> server:
    //   u16 vehicleId, u16 seat(7 bits)|driveBy(1 bit)|weapon(8 bits),
    //   u8 health, u8 armour, u16 leftRight, u16 upDown, u16 keys, f32 pos[3]
    bool SampBotClient::SendPassengerSync(int vehicleId, int seat, const SampVector3& position)
    {
        if (m_client == nullptr || m_state != State::Spawned || m_dead || vehicleId <= 0 || vehicleId > 2000)
            return false;

        const SampVector3 pos{ClampPos(position.x, -19990.0f, 19990.0f),
                              ClampPos(position.y, -19990.0f, 19990.0f),
                              ClampPos(position.z, -900.0f, 190000.0f)};

        RakNet::BitStream bs;
        bs.Write(static_cast<uint8_t>(kPacketPlayerPassenger));
        bs.Write(static_cast<uint16_t>(vehicleId));
        bs.Write(static_cast<uint8_t>(std::clamp(seat, 0, 127)));
        bs.Write(static_cast<uint8_t>(m_local.weapon & 0x3F));
        bs.Write(static_cast<uint8_t>(std::lround(Clamp100(m_local.health))));
        bs.Write(static_cast<uint8_t>(std::lround(Clamp100(m_local.armour))));
        bs.Write(m_local.leftRight);
        bs.Write(m_local.upDown);
        bs.Write(m_local.keys);
        bs.Write(pos.x);
        bs.Write(pos.y);
        bs.Write(pos.z);

        if (!SendSync(bs))
            return false;
        const uint32_t nowMs = NowMs();
        m_lastAnySyncMs = nowMs;
        m_hasSynced = true;
        return true;
    }

    // ------------------------------------------------------------------
    //  Server -> client RPCs
    // ------------------------------------------------------------------
    // RakNet hands our `extra` pointer back untouched; it points into
    // m_rpcBindings, which lives exactly as long as this object. RPC handlers
    // only ever run inside RakClient::Receive(), called from Pump() on the
    // server thread, so a Stop()ed client (pointer cleared) can never call back.
    void SampBotClient::RegisterRpcHandlers()
    {
        if (m_client == nullptr)
            return;
        for (size_t i = 0; i < kHandledRpcCount; ++i)
        {
            m_rpcBindings[i] = RpcBinding{this, kHandledRpcs[i]};
            m_client->RegisterAsRemoteProcedureCall(
                static_cast<RakNet::RPCID>(kHandledRpcs[i]),
                [](RakNet::RPCParameters* params, void* extra)
                {
                    auto* b = static_cast<RpcBinding*>(extra);
                    if (b != nullptr && b->client != nullptr)
                        b->client->Dispatch(b->id, params);
                },
                &m_rpcBindings[i]);
        }
    }

    void SampBotClient::UnregisterRpcHandlers()
    {
        if (m_client == nullptr)
            return;
        for (size_t i = 0; i < kHandledRpcCount; ++i)
        {
            m_client->UnregisterAsRemoteProcedureCall(static_cast<RakNet::RPCID>(kHandledRpcs[i]));
            m_rpcBindings[i] = RpcBinding{};
        }
    }

    void SampBotClient::Dispatch(unsigned char rpcId, RakNet::RPCParameters* params)
    {
        if (params == nullptr)
            return;

        // Some RPCs (RemovePlayerFromVehicle, ResetPlayerWeapons, ...) carry no
        // payload at all; they must still be dispatched.
        RakNet::BitStream empty;
        RakNet::BitStream payload(params->input,
                                  params->input != nullptr ? (params->numberOfBitsOfData + 7U) / 8U : 0U,
                                  false);
        RakNet::BitStream& bs = (params->input != nullptr && params->numberOfBitsOfData != 0) ? payload : empty;

        switch (rpcId)
        {
            case kRpcInitGame:
                HandleInitGame(bs);
                break;
            case kRpcRequestClass:
                HandleRequestClassResponse(bs);
                break;
            case kRpcRequestSpawn:
                HandleRequestSpawnResponse(bs);
                break;
            case kRpcConnectionRejected:
                HandleConnectionRejected(bs);
                break;
            case kRpcClientCheck:
                HandleClientCheck(bs);
                break;
            case kRpcSetPlayerHealth:
                HandleSetPlayerHealth(bs);
                break;
            case kRpcPutPlayerInVehicle:
                HandlePutPlayerInVehicle(bs);
                break;

            case kRpcSetPlayerName:
            {
                uint16_t playerId = 0;
                uint8_t len = 0;
                if (bs.Read(playerId) && bs.Read(len) && playerId == m_playerId && len <= MAX_PLAYER_NAME)
                {
                    char buffer[MAX_PLAYER_NAME + 1] = {};
                    if (len == 0 || bs.Read(buffer, len))
                        m_name.assign(buffer, len);
                }
                break;
            }
            case kRpcSetPlayerPos:
            case kRpcSetPlayerPosFindZ:
            {
                SampVector3 p;
                if (ReadVec3(bs, p) && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z))
                    m_local.position = p;
                break;
            }
            case kRpcSetPlayerArmour:
            {
                float v = 0.0f;
                if (bs.Read(v))
                    m_local.armour = Clamp100(v);
                break;
            }
            case kRpcTogglePlayerControllable:
            {
                uint8_t v = 1;
                if (bs.Read(v))
                    m_local.controllable = v != 0;
                break;
            }
            case kRpcSetPlayerFacingAngle:
            {
                float v = 0.0f;
                if (bs.Read(v))
                    m_local.facingAngle = ClampAngle(v);
                break;
            }
            case kRpcSetPlayerInterior:
            {
                uint8_t v = 0;
                if (bs.Read(v))
                    m_local.interior = v;
                break;
            }
            case kRpcSetPlayerVelocity:
            {
                SampVector3 v;
                if (ReadVec3(bs, v))
                    m_local.velocity = SanitizeVelocity(v);
                break;
            }
            case kRpcSetVehicleVelocity:
            {
                uint8_t type = 0;
                SampVector3 v;
                if (bs.Read(type) && ReadVec3(bs, v))
                    m_local.velocity = SanitizeVelocity(v);
                break;
            }
            case kRpcSetVehicleHealth:
            {
                uint16_t vehicleId = 0;
                float hp = 0.0f;
                if (bs.Read(vehicleId) && bs.Read(hp) && (m_local.vehicleId == 0 || vehicleId == m_local.vehicleId))
                    m_local.vehicleHealth = Clamp1000(hp);
                break;
            }
            case kRpcSetPlayerArmedWeapon:
            {
                uint32_t w = 0;
                if (bs.Read(w) && w <= 63)
                    m_local.weapon = static_cast<uint8_t>(w);
                break;
            }
            case kRpcGivePlayerWeapon:
            {
                uint32_t w = 0, ammo = 0;
                if (bs.Read(w) && bs.Read(ammo) && w <= 63 && ammo > 0 && m_local.weapon == 0)
                    m_local.weapon = static_cast<uint8_t>(w);
                break;
            }
            case kRpcSetPlayerAmmo:
                break;
            case kRpcResetPlayerWeapons:
                m_local.weapon = 0;
                break;
            case kRpcSetPlayerSpecialAction:
            {
                uint8_t a = 0;
                if (bs.Read(a))
                    m_local.specialAction = a;
                break;
            }
            case kRpcRemovePlayerFromVehicle:
                m_local.inVehicle = false;
                m_local.passenger = false;
                m_local.vehicleId = 0;
                m_local.vehicleSeat = 0;
                break;
            case kRpcSetPlayerSkin:
                break;
            default:
                break;
        }
    }

    // PlayerInit: bit zoneNames, bit pedAnims, bit interiorWeapons, bit limitChat,
    // f32 chatRadius, bit stunt, f32 nametagDist, bit noEnterExit, bit nametagLOS,
    // bit manualEngine, u32 spawnCount, u16 playerId, bit showNames, u32 markers,
    // u8 time, u8 weather, f32 gravity, bit lan, u32 deathDrop, bit instagib,
    // u32 onFootRate, u32 inCarRate, u32 weaponRate, u32 multiplier, u32 lagComp,
    // string hostname ...
    void SampBotClient::HandleInitGame(RakNet::BitStream& bs)
    {
        bool b = false;
        float f = 0.0f;
        uint32_t u32 = 0;
        uint16_t playerId = 0;
        uint8_t u8 = 0;
        uint32_t onFootRate = 0, inCarRate = 0;

        bs.Read(b); bs.Read(b); bs.Read(b); bs.Read(b);
        bs.Read(f);
        bs.Read(b);
        bs.Read(f);
        bs.Read(b); bs.Read(b); bs.Read(b);
        bs.Read(u32);
        if (!bs.Read(playerId))
        {
            SetFailed(static_cast<int>(kRpcInitGame));
            return;
        }
        bs.Read(b);
        bs.Read(u32);
        bs.Read(u8); bs.Read(u8);
        bs.Read(f);
        bs.Read(b);
        bs.Read(u32);
        bs.Read(b);
        if (bs.Read(onFootRate) && bs.Read(inCarRate))
        {
            if (onFootRate >= kMinSyncIntervalMs && onFootRate <= kMaxSyncIntervalMs)
                m_onFootRateMs = std::max(onFootRate, m_config.defaultOnFootRateMs);
            if (inCarRate >= kMinSyncIntervalMs && inCarRate <= kMaxSyncIntervalMs)
                m_inCarRateMs = std::max(inCarRate, m_config.defaultInCarRateMs);
        }

        if (!IsValidPlayerId(static_cast<int>(playerId)))
        {
            SetFailed(static_cast<int>(kRpcInitGame));
            return;
        }

        m_playerId = static_cast<int>(playerId);
        if (!m_playerIdAnnounced)
        {
            m_playerIdAnnounced = true;
            if (m_onPlayerId)
                m_onPlayerId(m_playerId);
        }

        // Join the class selection exactly like the game client does.
        EnterState(State::AwaitingClassResponse);
        MarkStageDeadline(m_config.spawnTimeoutMs);
        if (!SendRequestClass(0))
            SetFailed(-1002);
    }

    // RequestClass response: u8 selectable, u8 team, u32 skin, u8 unknown,
    // f32 spawn[3], f32 angle, u32 weapons[3], u32 ammo[3]
    void SampBotClient::HandleRequestClassResponse(RakNet::BitStream& bs)
    {
        uint8_t selectable = 0;
        if (!bs.Read(selectable))
            return;
        if (m_state != State::AwaitingClassResponse)
            return;

        if (selectable == 0)
        {
            // The script refused the class (OnPlayerRequestClass returned 0).
            EnterState(State::Idle);
            m_hasDeadline = false;
            return;
        }

        if (!SendRequestSpawn())
        {
            SetFailed(-1003);
            return;
        }
        EnterState(State::AwaitingSpawnResponse);
        MarkStageDeadline(m_config.spawnTimeoutMs);
    }

    // RequestSpawn response: u32  (0 = refused, 1 = allowed, 2 = forced spawn)
    void SampBotClient::HandleRequestSpawnResponse(RakNet::BitStream& bs)
    {
        // open.mp answers with a 32 bit value, a genuine SA-MP server with a single byte
        // (verified against samp03svr 0.3.7-R2): read whichever the payload holds.
        uint32_t allow = 0;
        if (bs.GetNumberOfBitsUsed() >= 32)
        {
            if (!bs.Read(allow))
                return;
        }
        else
        {
            uint8_t small = 0;
            if (!bs.Read(small))
                return;
            allow = small;
        }

        // 2 == the script called SpawnPlayer(): the server wants us to spawn
        // regardless of what state the handshake was in.
        if (allow == 0)
        {
            if (m_state == State::AwaitingSpawnResponse)
            {
                EnterState(State::Idle);
                m_hasDeadline = false;
            }
            return;
        }
        if (m_state == State::Spawned && allow != 2)
            return;
        if (m_state != State::AwaitingSpawnResponse && allow != 2)
            return;

        if (!SendSpawnRpc())
        {
            SetFailed(-1004);
            return;
        }

        m_dead = false;
        m_local.inVehicle = false;
        m_local.passenger = false;
        m_local.vehicleId = 0;
        EnterState(State::Spawned);
        m_hasDeadline = false;

        // Report the starting position right away (a game client does this on
        // the next frame) so the server does not keep the class position.
        m_hasSynced = false;
        SendOnFootSync(NowMs());
    }

    void SampBotClient::HandleConnectionRejected(RakNet::BitStream& bs)
    {
        uint8_t reason = 0;
        bs.Read(reason);
        SetFailed(NetFail::kJoinRejectedBase - static_cast<int>(reason));
    }

    // ClientCheck request: u8 type, u32 address, u16 offset, u16 count
    // reply:               u8 type, u32 address, u8 result
    void SampBotClient::HandleClientCheck(RakNet::BitStream& bs)
    {
        uint8_t type = 0;
        uint32_t address = 0;
        if (!bs.Read(type) || !bs.Read(address))
            return;
        RakNet::BitStream reply;
        reply.Write(type);
        reply.Write(address);
        reply.Write(static_cast<uint8_t>(0));
        SendRpc(kRpcClientCheck, reply);
    }

    void SampBotClient::HandleSetPlayerHealth(RakNet::BitStream& bs)
    {
        float health = 0.0f;
        if (!bs.Read(health) || !std::isfinite(health))
            return;
        m_local.health = Clamp100(health);
        // A game client reports its own death when the ped's health reaches 0.
        if (m_local.health <= 0.0f && m_state == State::Spawned && !m_dead)
            SendDeath(255, kInvalidId);
    }

    // PutPlayerInVehicle: u16 vehicleId, u8 seat
    void SampBotClient::HandlePutPlayerInVehicle(RakNet::BitStream& bs)
    {
        uint16_t vehicleId = 0;
        uint8_t seat = 0;
        if (!bs.Read(vehicleId) || !bs.Read(seat))
            return;
        m_local.inVehicle = true;
        m_local.vehicleId = vehicleId;
        m_local.vehicleSeat = seat;
        m_local.passenger = seat != 0;
        m_local.velocity = {};
    }
}
