// ============================================================================
// FakeBots :: BotMovement.cpp
// ============================================================================
#include "BotMovement.h"
#include "../callbacks/CallbackDispatcher.h"

namespace FakeBots
{
    namespace BotMovement
    {
        static const char* AnimNameFor(BotMoveType type)
        {
            switch (type)
            {
                case BotMoveType::Run:    return "run_civi";
                case BotMoveType::Sprint: return "sprint_civi";
                case BotMoveType::Walk:
                default:                  return "walk_civi";
            }
        }

        static void AdvanceWaypointOrStop(Bot &bot, int botId)
        {
            if (!bot.Waypoints().empty())
            {
                CallbackDispatcher::Get().OnBotWaypointReached(botId, bot.GetWaypointIndex());
                Waypoint next = bot.Waypoints().front();
                bot.Waypoints().pop_front();
                if (bot.IsLoopingWaypoints())
                    bot.PushWaypoint(next.x, next.y, next.z);
                bot.SetWaypointIndex(bot.GetWaypointIndex() + 1);

                if (!bot.Waypoints().empty())
                {
                    const Waypoint &nextTarget = bot.Waypoints().front();
                    bot.SetMoveTarget(nextTarget.x, nextTarget.y, nextTarget.z, bot.GetMoveSpeed(), bot.GetMoveType());
                    return;
                }
            }

            float tx, ty, tz;
            bot.GetMoveTarget(tx, ty, tz);
            bot.StopMoving();
            CallbackDispatcher::Get().OnBotReachDestination(botId, tx, ty, tz);
        }

        static void AdvanceDriveWaypointOrStop(Bot &bot, int botId, int vehicleId, float tx, float ty, float tz)
        {
            if (!bot.DriveWaypoints().empty())
            {
                Waypoint next = bot.DriveWaypoints().front();
                bot.DriveWaypoints().pop_front();
                if (bot.IsLoopingDriveWaypoints())
                    bot.PushDriveWaypoint(next.x, next.y, next.z, bot.GetDriveSpeed());
                bot.SetDriveWaypointIndex(bot.GetDriveWaypointIndex() + 1);

                if (!bot.DriveWaypoints().empty())
                {
                    const Waypoint &nextTarget = bot.DriveWaypoints().front();
                    bot.SetDriveTarget(vehicleId, nextTarget.x, nextTarget.y, nextTarget.z, bot.GetDriveSpeed());
                    return;
                }
            }

            bot.StopDriving();
            CallbackDispatcher::Get().OnBotVehicleReachDestination(botId, vehicleId, tx, ty, tz);
        }

        bool StepTowardPoint(Bot &bot, int playerId, float tx, float ty, float tz,
                             float speed, BotMoveType type, float deltaSeconds)
        {
            (void)playerId;
            float px, py, pz;
            bot.GetCachedPosition(px, py, pz);

            const float distSq = DistanceSquared(px, py, pz, tx, ty, tz);
            if (distSq <= kMinMoveDistance * kMinMoveDistance)
            {
                bot.SetCachedPosition(tx, ty, tz);
                bot.SetCachedVelocity(0.0f, 0.0f, 0.0f);
                return true;
            }

            const float distance = std::sqrt(distSq);
            const float step = std::max(0.0f, speed) * deltaSeconds;
            const float ratio = (step >= distance) ? 1.0f : (step / distance);
            const float nx = px + (tx - px) * ratio;
            const float ny = py + (ty - py) * ratio;
            const float nz = pz + (tz - pz) * ratio;
            const float facing = AngleTowards(px, py, tx, ty);

            bot.SetCachedPosition(nx, ny, nz);
            bot.SetCachedFacing(facing);
            const float invDt = deltaSeconds > 0.0001f ? (1.0f / deltaSeconds) : 0.0f;
            bot.SetCachedVelocity((nx - px) * invDt, (ny - py) * invDt, (nz - pz) * invDt);

            if (bot.GetLastAppliedAnim() != type)
            {
                // ApplyAnimation is still the normal server API for telling
                // observers what the player is doing. Position itself is no
                // longer driven with SetPlayerPos; movement is sent by the
                // embedded client as PLAYER_SYNC.
                sampgdk::ApplyAnimation(bot.GetPlayerId(), "PED", AnimNameFor(type), 4.1f,
                    1, 0, 0, 0, 0, 1);
                bot.SetLastAppliedAnim(type);
            }
            return false;
        }

        void StepOnFoot(Bot &bot, float deltaSeconds)
        {
            if (bot.GetPlayerId() == kInvalidId)
                return;

            float tx, ty, tz;
            bot.GetMoveTarget(tx, ty, tz);
            if (StepTowardPoint(bot, bot.GetPlayerId(), tx, ty, tz,
                                bot.GetMoveSpeed(), bot.GetMoveType(), deltaSeconds))
                AdvanceWaypointOrStop(bot, bot.GetBotId());
        }

        void StepDriving(Bot &bot, float deltaSeconds)
        {
            const int vehicleId = bot.GetVehicleId();
            if (bot.GetPlayerId() == kInvalidId || vehicleId == kInvalidId)
                return;

            float vx, vy, vz;
            if (!sampgdk::GetVehiclePos(vehicleId, &vx, &vy, &vz))
            {
                bot.StopDriving();
                return;
            }

            float vehicleHealth = 1000.0f;
            sampgdk::GetVehicleHealth(vehicleId, &vehicleHealth);

            float tx, ty, tz;
            bot.GetDriveTarget(tx, ty, tz);
            const float snapRadius = kMinMoveDistance * 4.0f;
            const float distSq = DistanceSquared(vx, vy, vz, tx, ty, tz);
            if (distSq <= snapRadius * snapRadius)
            {
                const SampVector3 pos{tx, ty, tz};
                const SampVector3 vel{0.0f, 0.0f, 0.0f};
                bot.SendInCarSync(vehicleId, pos, vel, vehicleHealth);
                AdvanceDriveWaypointOrStop(bot, bot.GetBotId(), vehicleId, tx, ty, tz);
                return;
            }

            const float distance = std::sqrt(distSq);
            const float step = std::max(0.0f, bot.GetDriveSpeed()) * deltaSeconds;
            const float ratio = (step >= distance) ? 1.0f : (step / distance);
            const float nx = vx + (tx - vx) * ratio;
            const float ny = vy + (ty - vy) * ratio;
            const float nz = vz + (tz - vz) * ratio;
            const float heading = AngleTowards(vx, vy, tx, ty);
            const float speed = bot.GetDriveSpeed();
            const float rad = heading * (3.14159265358979323846f / 180.0f);
            const SampVector3 vel{std::sin(rad) * -speed / 40.0f,
                                  std::cos(rad) * speed / 40.0f,
                                  0.0f};

            bot.SetCachedFacing(heading);
            bot.SendInCarSync(vehicleId, {nx, ny, nz}, vel, vehicleHealth);
        }

        void StepFollow(Bot &bot, float deltaSeconds)
        {
            const int targetId = bot.GetFollowTargetId();
            if (bot.GetPlayerId() == kInvalidId || targetId == kInvalidId)
                return;

            if (!sampgdk::IsPlayerConnected(targetId))
            {
                bot.StopFollowing();
                return;
            }

            float tx, ty, tz;
            if (!sampgdk::GetPlayerPos(targetId, &tx, &ty, &tz))
                return;

            float offX, offY;
            bot.GetFollowOffset(offX, offY);
            float targetAngle = 0.0f;
            sampgdk::GetPlayerFacingAngle(targetId, &targetAngle);
            const float rad = targetAngle * (3.14159265358979323846f / 180.0f);
            const float worldOffX = offX * std::cos(rad) - offY * std::sin(rad);
            const float worldOffY = offX * std::sin(rad) + offY * std::cos(rad);

            StepTowardPoint(bot, bot.GetPlayerId(), tx + worldOffX, ty + worldOffY, tz,
                            bot.GetFollowSpeed(), BotMoveType::Walk, deltaSeconds);
        }
    }
}
