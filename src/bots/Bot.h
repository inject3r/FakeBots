// ============================================================================
//  FakeBots :: Bot.h
//  Represents a single fake player instance and everything the simulation
//  layer needs to steer it (walking paths, driving paths, follow/combat/
//  idle-wander AI, pending spawn data). A Bot always maps 1:1 to a real,
//  connected player id once the embedded RakNet client handshake has
//  completed.
// ============================================================================
#pragma once

#include "../core/Common.h"
#include "../network/SampBotClient.h"
#include <deque>
#include <unordered_set>

namespace FakeBots
{
    enum class BotMoveType : int
    {
        None    = 0,
        Walk    = 1,
        Run     = 2,
        Sprint  = 3,
    };

    enum class BotLifeState : int
    {
        Connecting = 0,   // RakNet connection in progress
        Idle       = 1,   // Connected, not spawned
        Spawned    = 2,   // Alive in the world
        Dead       = 3,   // Wasted, waiting for respawn/removal
        Removing   = 4,   // Destroy() in progress
        Pooled     = 5,   // Connected & idle, held in reserve for reuse (see BotManager pooling)
    };

    struct Waypoint
    {
        float x, y, z;
    };

    // Pending spawn parameters requested via FakeBotCreate(), applied the
    // moment the NPC connection completes and OnPlayerConnect fires for it.
    struct PendingSpawn
    {
        int   skin        = 0;
        float x           = 0.0f;
        float y           = 0.0f;
        float z           = 0.0f;
        float angle       = 0.0f;
        int   weapon      = 0;
        int   ammo        = 0;
        int   virtualWorld = 0;
        int   interior    = 0;
    };

    class Bot
    {
    public:
        Bot(int botId, std::string name);
        ~Bot();

        Bot(const Bot&) = delete;
        Bot& operator=(const Bot&) = delete;

        // Identity
        int                 GetBotId()   const { return m_botId; }
        uint64_t            GetGeneration() const { return m_generation; }
        int                 GetPlayerId() const { return m_playerId; }
        const std::string&  GetName()    const { return m_name; }
        void SetName(const std::string &name) { m_name = name; }

        void SetPlayerId(int playerId) { m_playerId = playerId; }
        int  GetConnectRetries() const { return m_connectRetries; }
        void BumpConnectRetries() { ++m_connectRetries; }
        bool HasConnectForwarded() const { return m_connectForwarded; }
        void MarkConnectForwarded() { m_connectForwarded = true; }
        void ResetConnectForwarded() { m_connectForwarded = false; }
        SampBotClient& Client() { return m_client; }
        const SampBotClient& Client() const { return m_client; }

        // Lifecycle
        BotLifeState GetState() const { return m_state; }
        void         SetState(BotLifeState state);

        PendingSpawn& GetPendingSpawn() { return m_pendingSpawn; }

        // ------------------------------------------------------------------
        // Cached position - updated every time WE move the bot (on-foot
        // step, spawn, respawn). Lets the simulation avoid an extra
        // GetPlayerPos() native round-trip on every single tick for every
        // moving bot; only used as a "last known" value, never as a
        // substitute for a fresh read when precision actually matters.
        // ------------------------------------------------------------------
        void SetCachedPosition(float x, float y, float z) { m_cachedX = x; m_cachedY = y; m_cachedZ = z; }
        void GetCachedPosition(float &x, float &y, float &z) const { x = m_cachedX; y = m_cachedY; z = m_cachedZ; }

        void SetCachedFacing(float angle) { m_facingAngle = angle; }
        float GetCachedFacing() const { return m_facingAngle; }
        void SetCachedVelocity(float x, float y, float z) { m_velocityX = x; m_velocityY = y; m_velocityZ = z; }
        void GetCachedVelocity(float &x, float &y, float &z) const { x = m_velocityX; y = m_velocityY; z = m_velocityZ; }
        void SetCachedHealth(float health) { m_health = health; }
        float GetCachedHealth() const { return m_health; }
        void SetCachedArmour(float armour) { m_armour = armour; }
        float GetCachedArmour() const { return m_armour; }
        void SetCachedWeapon(uint8_t weapon) { m_weapon = weapon; }
        uint8_t GetCachedWeapon() const { return m_weapon; }
        void SetCachedKeys(uint16_t keys) { m_keys = keys; }
        uint16_t GetCachedKeys() const { return m_keys; }

        void SyncClientStateFromNetwork();
        void ResetRuntimeState();
        bool SendOnFootSync(uint32_t nowMs);
        bool SendInCarSync(int vehicleId, const SampVector3& position, const SampVector3& velocity, float vehicleHealth = 1000.0f);
        // A bot that sits in a vehicle without driving anywhere still has to
        // report its vehicle state like any game client does; without it the
        // server never switches the player to DRIVER / PASSENGER.
        bool SendVehicleIdleSync(uint32_t nowMs);

        // ------------------------------------------------------------------
        // On-foot movement (steering toward a single target or a queued
        // patrol route). Positions are simulated server-side every tick and
        // pushed to the server through real SA-MP player-sync packets.
        // ------------------------------------------------------------------
        void SetMoveTarget(float x, float y, float z, float speed, BotMoveType type);
        void StopMoving();
        bool IsMoving() const { return m_moving; }
        BotMoveType GetMoveType() const { return m_moveType; }
        void GetMoveTarget(float &x, float &y, float &z) const;
        float GetMoveSpeed() const { return m_moveSpeed; }

        void PushWaypoint(float x, float y, float z);
        void ClearWaypoints();
        void SetLoopWaypoints(bool loop) { m_loopWaypoints = loop; }
        bool IsLoopingWaypoints() const { return m_loopWaypoints; }
        std::deque<Waypoint>& Waypoints() { return m_waypoints; }
        int GetWaypointIndex() const { return m_waypointIndex; }
        void SetWaypointIndex(int index) { m_waypointIndex = index; }

        // Last movement-type animation actually applied, so the simulation
        // only re-issues ApplyAnimation() when the style changes instead of
        // every single tick (the animation itself already loops client-side).
        BotMoveType GetLastAppliedAnim() const { return m_lastAppliedAnim; }
        void SetLastAppliedAnim(BotMoveType type) { m_lastAppliedAnim = type; }

        // ------------------------------------------------------------------
        // Vehicle driving (straight-line waypoint interpolation - see
        // BotMovement.cpp for the steering implementation). This gives
        // gamemode authors a reliable primitive to build patrol routes,
        // traffic simulation, taxi bots, etc. on top of.
        // ------------------------------------------------------------------
        void SetDriveTarget(int vehicleId, float x, float y, float z, float speed);
        void StopDriving();
        bool IsDriving() const { return m_driving; }
        int  GetVehicleId() const { return m_vehicleId; }
        void GetDriveTarget(float &x, float &y, float &z) const;
        float GetDriveSpeed() const { return m_driveSpeed; }

        void SetVehicleId(int vehicleId) { m_vehicleId = vehicleId; }

        // Multi-stop driving routes - same idea as on-foot waypoints, but
        // for vehicles (e.g. a taxi/bus route made of dense, road-following
        // points exported from a map).
        void PushDriveWaypoint(float x, float y, float z, float speed);
        void ClearDriveWaypoints();
        void SetLoopDriveWaypoints(bool loop) { m_loopDriveWaypoints = loop; }
        bool IsLoopingDriveWaypoints() const { return m_loopDriveWaypoints; }
        std::deque<Waypoint>& DriveWaypoints() { return m_driveWaypoints; }
        int GetDriveWaypointIndex() const { return m_driveWaypointIndex; }
        void SetDriveWaypointIndex(int index) { m_driveWaypointIndex = index; }

        // ------------------------------------------------------------------
        // Follow AI - continuously steers the bot to stay near a target
        // player at a fixed local offset (used for group formations, taxi
        // "escort" behaviour, etc.). Re-evaluated every tick rather than a
        // one-shot GoTo(), since the target keeps moving.
        // ------------------------------------------------------------------
        void SetFollowTarget(int targetPlayerId, float offsetX, float offsetY, float speed);
        void StopFollowing();
        bool IsFollowing() const { return m_following; }
        int  GetFollowTargetId() const { return m_followTargetId; }
        void GetFollowOffset(float &x, float &y) const { x = m_followOffsetX; y = m_followOffsetY; }
        float GetFollowSpeed() const { return m_followSpeed; }

        // ------------------------------------------------------------------
        // Combat AI - lightweight, opt-in: faces the target, plays a
        // firing animation and periodically applies damage while both are
        // alive and within range. No line-of-sight/pathing to cover - see
        // docs/API-Reference.md for the exact behaviour and limitations.
        // ------------------------------------------------------------------
        void SetCombatTarget(int targetPlayerId, float damagePerHit, float fireIntervalSeconds, float range);
        void StopCombat();
        bool IsInCombat() const { return m_inCombat; }
        int  GetCombatTargetId() const { return m_combatTargetId; }
        float GetCombatDamagePerHit() const { return m_combatDamagePerHit; }
        float GetCombatFireInterval() const { return m_combatFireInterval; }
        float GetCombatRange() const { return m_combatRange; }
        float GetCombatCooldown() const { return m_combatCooldown; }
        void  SetCombatCooldown(float seconds) { m_combatCooldown = seconds; }
        void  TickCombatCooldown(float deltaSeconds) { m_combatCooldown -= deltaSeconds; }

        // ------------------------------------------------------------------
        // Idle wander - when enabled, a spawned bot with nothing else to do
        // (not following a route, not driving, not following/fighting)
        // periodically picks a random point within `radius` of its anchor
        // and walks there, pausing briefly on arrival.
        // ------------------------------------------------------------------
        void SetIdleWander(bool enabled, float anchorX, float anchorY, float anchorZ, float radius, float speed);
        bool IsIdleWanderEnabled() const { return m_idleWanderEnabled; }
        void GetIdleWanderAnchor(float &x, float &y, float &z) const { x = m_idleAnchorX; y = m_idleAnchorY; z = m_idleAnchorZ; }
        float GetIdleWanderRadius() const { return m_idleWanderRadius; }
        float GetIdleWanderSpeed() const { return m_idleWanderSpeed; }
        float GetIdleWaitRemaining() const { return m_idleWaitRemaining; }
        void  SetIdleWaitRemaining(float seconds) { m_idleWaitRemaining = seconds; }
        bool HasIdleWanderTarget() const { return m_hasIdleTarget; }
        void SetIdleWanderTarget(float x, float y, float z) { m_idleTarget = {x, y, z}; m_hasIdleTarget = true; }
        void ClearIdleWanderTarget() { m_hasIdleTarget = false; }
        const Waypoint& GetIdleWanderTarget() const { return m_idleTarget; }

        // ------------------------------------------------------------------
        // Nearby-player tracking (drives OnFakeBotPlayerNearby, fired only
        // on the "entered range" transition to avoid spamming scripts every
        // tick a player happens to stay close).
        // ------------------------------------------------------------------
        std::unordered_set<int>& NearbyPlayers() { return m_nearbyPlayers; }

        // ------------------------------------------------------------------
        // Simulation throttling - bots far from every real player are
        // stepped less often (see BotManager::Tick()). This counter is
        // just scratch space for that skip-cycle logic.
        // ------------------------------------------------------------------
        int  GetThrottleCounter() const { return m_throttleCounter; }
        void SetThrottleCounter(int value) { m_throttleCounter = value; }
        float GetThrottleAccumulator() const { return m_throttleAccumulator; }
        void AddThrottleTime(float seconds) { m_throttleAccumulator += std::max(0.0f, seconds); }
        float ConsumeThrottleTime(float currentDelta) { const float total = m_throttleAccumulator + std::max(0.0f, currentDelta); m_throttleAccumulator = 0.0f; return total; }
        void ResetThrottle() { m_throttleCounter = 0; m_throttleAccumulator = 0.0f; }
        void SetThrottleAccumulator(float seconds) { m_throttleAccumulator = std::max(0.0f, seconds); }

    private:
        int             m_botId;
        uint64_t        m_generation = 0;
        int             m_playerId;
        bool            m_connectForwarded = false;
        int             m_connectRetries = 0;
        std::string     m_name;
        BotLifeState    m_state;
        PendingSpawn    m_pendingSpawn;

        float           m_cachedX, m_cachedY, m_cachedZ;
        float           m_facingAngle = 0.0f;
        float           m_velocityX = 0.0f, m_velocityY = 0.0f, m_velocityZ = 0.0f;
        float           m_health = 100.0f;
        float           m_armour = 0.0f;
        uint8_t         m_weapon = 0;
        uint16_t        m_keys = 0;

        // On-foot state
        bool            m_moving;
        BotMoveType     m_moveType;
        float           m_targetX, m_targetY, m_targetZ;
        float           m_moveSpeed;
        std::deque<Waypoint> m_waypoints;
        bool            m_loopWaypoints;
        int             m_waypointIndex;
        BotMoveType     m_lastAppliedAnim;

        // Vehicle state
        bool            m_driving;
        int             m_vehicleId;
        float           m_driveTargetX, m_driveTargetY, m_driveTargetZ;
        float           m_driveSpeed;
        std::deque<Waypoint> m_driveWaypoints;
        bool            m_loopDriveWaypoints;
        int             m_driveWaypointIndex;

        // Follow state
        bool            m_following;
        int             m_followTargetId;
        float           m_followOffsetX, m_followOffsetY;
        float           m_followSpeed;

        // Combat state
        bool            m_inCombat;
        int             m_combatTargetId;
        float           m_combatDamagePerHit;
        float           m_combatFireInterval;
        float           m_combatRange;
        float           m_combatCooldown;

        // Idle wander state
        bool            m_idleWanderEnabled;
        float           m_idleAnchorX, m_idleAnchorY, m_idleAnchorZ;
        float           m_idleWanderRadius;
        float           m_idleWanderSpeed;
        float           m_idleWaitRemaining;
        Waypoint        m_idleTarget{};
        bool            m_hasIdleTarget = false;

        std::unordered_set<int> m_nearbyPlayers;
        int             m_throttleCounter;
        float           m_throttleAccumulator = 0.0f;
        SampBotClient   m_client;
    };
}
