// ============================================================================
//  FakeBots :: Bot.cpp
// ============================================================================
#include "Bot.h"

#include <atomic>

namespace {
    std::atomic<uint64_t> g_botGeneration{0};
}

namespace FakeBots
{
    Bot::Bot(int botId, std::string name)
        : m_botId(botId)
        , m_generation(++g_botGeneration)
        , m_playerId(kInvalidId)
        , m_name(std::move(name))
        , m_state(BotLifeState::Connecting)
        , m_cachedX(0.0f), m_cachedY(0.0f), m_cachedZ(0.0f)
        , m_moving(false)
        , m_moveType(BotMoveType::None)
        , m_targetX(0.0f), m_targetY(0.0f), m_targetZ(0.0f)
        , m_moveSpeed(kDefaultWalkSpeed)
        , m_loopWaypoints(false)
        , m_waypointIndex(0)
        , m_lastAppliedAnim(BotMoveType::None)
        , m_driving(false)
        , m_vehicleId(kInvalidId)
        , m_driveTargetX(0.0f), m_driveTargetY(0.0f), m_driveTargetZ(0.0f)
        , m_driveSpeed(kDefaultDriveSpeed)
        , m_loopDriveWaypoints(false)
        , m_driveWaypointIndex(0)
        , m_following(false)
        , m_followTargetId(kInvalidId)
        , m_followOffsetX(0.0f), m_followOffsetY(0.0f)
        , m_followSpeed(kDefaultRunSpeed)
        , m_inCombat(false)
        , m_combatTargetId(kInvalidId)
        , m_combatDamagePerHit(5.0f)
        , m_combatFireInterval(1.0f)
        , m_combatRange(20.0f)
        , m_combatCooldown(0.0f)
        , m_idleWanderEnabled(false)
        , m_idleAnchorX(0.0f), m_idleAnchorY(0.0f), m_idleAnchorZ(0.0f)
        , m_idleWanderRadius(10.0f)
        , m_idleWanderSpeed(kDefaultWalkSpeed)
        , m_idleWaitRemaining(0.0f)
        , m_throttleCounter(0)
        , m_client(m_name)
    {
    }

    Bot::~Bot() = default;

    void Bot::ResetRuntimeState()
    {
        StopMoving();
        StopDriving();
        StopFollowing();
        StopCombat();
        ClearWaypoints();
        ClearDriveWaypoints();
        m_waypointIndex = 0;
        m_driveWaypointIndex = 0;
        m_loopWaypoints = false;
        m_loopDriveWaypoints = false;
        m_lastAppliedAnim = BotMoveType::None;
        m_followTargetId = kInvalidId;
        m_combatTargetId = kInvalidId;
        m_combatCooldown = 0.0f;
        m_idleWaitRemaining = 0.0f;
        m_hasIdleTarget = false;
        m_nearbyPlayers.clear();
        ResetThrottle();
        SetCachedVelocity(0.0f, 0.0f, 0.0f);
        SetCachedKeys(0);
    }

    void Bot::SyncClientStateFromNetwork()
    {
        const SampBotLocalState &state = m_client.GetLocalState();
        m_cachedX = state.position.x;
        m_cachedY = state.position.y;
        m_cachedZ = state.position.z;
        m_facingAngle = state.facingAngle;
        m_velocityX = state.velocity.x;
        m_velocityY = state.velocity.y;
        m_velocityZ = state.velocity.z;
        m_health = state.health;
        m_armour = state.armour;
        m_weapon = state.weapon;
        m_keys = state.keys;
    }

    bool Bot::SendOnFootSync(uint32_t nowMs)
    {
        SampBotLocalState state = m_client.GetLocalState();
        state.position = {m_cachedX, m_cachedY, m_cachedZ};
        state.velocity = {m_velocityX, m_velocityY, m_velocityZ};
        state.facingAngle = m_facingAngle;
        state.health = m_health;
        state.armour = m_armour;
        state.weapon = m_weapon;
        state.keys = m_keys;
        m_client.SetLocalState(state);
        return m_client.SendOnFootSync(nowMs);
    }

    bool Bot::SendInCarSync(int vehicleId, const SampVector3& position, const SampVector3& velocity, float vehicleHealth)
    {
        return m_client.SendInCarSync(vehicleId, position, velocity, vehicleHealth, m_facingAngle, m_weapon, m_keys);
    }

    bool Bot::SendVehicleIdleSync(uint32_t nowMs)
    {
        (void)nowMs;
        const SampBotLocalState &local = m_client.GetLocalState();
        if (!local.inVehicle || local.vehicleId == 0)
            return false;

        const int vehicleId = static_cast<int>(local.vehicleId);
        float vx = 0.0f, vy = 0.0f, vz = 0.0f;
        if (!sampgdk::GetVehiclePos(vehicleId, &vx, &vy, &vz))
            return false;

        if (local.passenger)
            return m_client.SendPassengerSync(vehicleId, local.vehicleSeat, {vx, vy, vz});

        float health = 1000.0f;
        sampgdk::GetVehicleHealth(vehicleId, &health);
        float vel[3] = {0.0f, 0.0f, 0.0f};
        sampgdk::GetVehicleVelocity(vehicleId, &vel[0], &vel[1], &vel[2]);
        float quat[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        const bool haveQuat = sampgdk::GetVehicleRotationQuat(vehicleId, &quat[0], &quat[1], &quat[2], &quat[3]);
        float angle = m_facingAngle;
        sampgdk::GetVehicleZAngle(vehicleId, &angle);
        m_facingAngle = angle;

        return m_client.SendInCarSync(vehicleId, {vx, vy, vz}, {vel[0], vel[1], vel[2]}, health,
                                      angle, m_weapon, m_keys, false, false, haveQuat ? quat : nullptr);
    }

    void Bot::SetState(BotLifeState state)
    {
        m_state = state;

        // Movement/driving/AI cannot continue once the bot leaves the world.
        if (state == BotLifeState::Dead || state == BotLifeState::Removing)
        {
            StopMoving();
            StopDriving();
            StopFollowing();
            StopCombat();
        }
    }

    void Bot::SetMoveTarget(float x, float y, float z, float speed, BotMoveType type)
    {
        m_targetX   = x;
        m_targetY   = y;
        m_targetZ   = z;
        m_moveSpeed = speed;
        m_moveType  = type;
        m_moving    = true;
    }

    void Bot::StopMoving()
    {
        m_moving   = false;
        m_moveType = BotMoveType::None;
    }

    void Bot::GetMoveTarget(float &x, float &y, float &z) const
    {
        x = m_targetX;
        y = m_targetY;
        z = m_targetZ;
    }

    void Bot::PushWaypoint(float x, float y, float z)
    {
        m_waypoints.push_back({x, y, z});
    }

    void Bot::ClearWaypoints()
    {
        m_waypoints.clear();
        m_waypointIndex = 0;
        m_loopWaypoints = false;
    }

    void Bot::SetDriveTarget(int vehicleId, float x, float y, float z, float speed)
    {
        m_vehicleId    = vehicleId;
        m_driveTargetX = x;
        m_driveTargetY = y;
        m_driveTargetZ = z;
        m_driveSpeed   = speed;
        m_driving      = true;
    }

    void Bot::StopDriving()
    {
        m_driving = false;
        m_vehicleId = kInvalidId;
    }

    void Bot::GetDriveTarget(float &x, float &y, float &z) const
    {
        x = m_driveTargetX;
        y = m_driveTargetY;
        z = m_driveTargetZ;
    }

    void Bot::PushDriveWaypoint(float x, float y, float z, float speed)
    {
        m_driveWaypoints.push_back({x, y, z});
        m_driveSpeed = speed;
    }

    void Bot::ClearDriveWaypoints()
    {
        m_driveWaypoints.clear();
        m_driveWaypointIndex = 0;
        m_loopDriveWaypoints = false;
    }

    void Bot::SetFollowTarget(int targetPlayerId, float offsetX, float offsetY, float speed)
    {
        m_followTargetId = targetPlayerId;
        m_followOffsetX  = offsetX;
        m_followOffsetY  = offsetY;
        m_followSpeed    = speed;
        m_following      = true;
    }

    void Bot::StopFollowing()
    {
        m_following      = false;
        m_followTargetId = kInvalidId;
    }

    void Bot::SetCombatTarget(int targetPlayerId, float damagePerHit, float fireIntervalSeconds, float range)
    {
        m_combatTargetId     = targetPlayerId;
        m_combatDamagePerHit = damagePerHit;
        m_combatFireInterval = fireIntervalSeconds;
        m_combatRange        = range;
        m_combatCooldown     = 0.0f;
        m_inCombat           = true;
    }

    void Bot::StopCombat()
    {
        m_inCombat       = false;
        m_combatTargetId = kInvalidId;
    }

    void Bot::SetIdleWander(bool enabled, float anchorX, float anchorY, float anchorZ, float radius, float speed)
    {
        m_idleWanderEnabled = enabled;
        m_idleAnchorX       = anchorX;
        m_idleAnchorY       = anchorY;
        m_idleAnchorZ       = anchorZ;
        m_idleWanderRadius  = radius;
        m_idleWanderSpeed   = speed;
        m_idleWaitRemaining = 0.0f;
        m_hasIdleTarget = false;
    }
}
