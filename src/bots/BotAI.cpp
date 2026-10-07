// ============================================================================
//  FakeBots :: BotAI.cpp
// ============================================================================
#include "BotAI.h"
#include "BotMovement.h"
#include "BotManager.h"
#include "../callbacks/CallbackDispatcher.h"
#include <random>

namespace FakeBots
{
    namespace BotAI
    {
        // Single shared RNG for wander-point selection; not
        // cryptographically anything, just needs to look natural.
        static std::mt19937& Rng()
        {
            static std::mt19937 rng(std::random_device{}());
            return rng;
        }

        static float RandomFloat(float minValue, float maxValue)
        {
            std::uniform_real_distribution<float> dist(minValue, maxValue);
            return dist(Rng());
        }

        void StepCombat(Bot &bot, float deltaSeconds)
        {
            const int playerId = bot.GetPlayerId();
            const int targetId = bot.GetCombatTargetId();
            if (playerId == kInvalidId || targetId == kInvalidId)
                return;

            float targetHealth = 0.0f;
            if (!sampgdk::IsPlayerConnected(targetId) ||
                !sampgdk::GetPlayerHealth(targetId, &targetHealth) ||
                targetHealth <= 0.0f)
            {
                bot.StopCombat();
                return;
            }

            float px, py, pz;
            bot.GetCachedPosition(px, py, pz);

            float tx, ty, tz;
            sampgdk::GetPlayerPos(targetId, &tx, &ty, &tz);

            const float distance = DistanceBetween(px, py, pz, tx, ty, tz);
            const float range = bot.GetCombatRange();

            if (distance > range)
            {
                // Close the distance before anything else - a bot that
                // can't reach its target obviously can't fight it.
                BotMovement::StepTowardPoint(bot, playerId, tx, ty, tz, kDefaultRunSpeed, BotMoveType::Run, deltaSeconds);
                bot.TickCombatCooldown(deltaSeconds);
                return;
            }

            // In range: face the target and fire on the configured cadence.
            const float facing = AngleTowards(px, py, tx, ty);
            bot.SetCachedFacing(facing);

            bot.TickCombatCooldown(deltaSeconds);
            if (bot.GetCombatCooldown() > 0.0f)
                return;

            sampgdk::ApplyAnimation(playerId, "COLT45", "shoot", 4.1f,
                /*loop*/ 0, /*lockx*/ 0, /*locky*/ 0, /*freeze*/ 0, /*time*/ 0, /*forcesync*/ 1);

            float newHealth = targetHealth - bot.GetCombatDamagePerHit();
            if (newHealth < 0.0f)
                newHealth = 0.0f;
            sampgdk::SetPlayerHealth(targetId, newHealth);

            // weaponid/bodypart are unknown for this simplified combat model
            // (no real bullet/hit-detection simulation) - reported as 0;
            // gamemodes wanting exact values should track their own weapon
            // state and call SetPlayerHealth themselves instead of relying
            // on this convenience AI for anything beyond simple filler NPCs.
            CallbackDispatcher::Get().OnBotGiveDamage(bot.GetBotId(), targetId, bot.GetCombatDamagePerHit(), 0, 0);

            bot.SetCombatCooldown(bot.GetCombatFireInterval());
        }

        void StepIdleWander(Bot &bot, float deltaSeconds)
        {
            const int playerId = bot.GetPlayerId();
            if (playerId == kInvalidId)
                return;

            if (bot.GetIdleWaitRemaining() > 0.0f)
            {
                bot.SetIdleWaitRemaining(bot.GetIdleWaitRemaining() - deltaSeconds);
                return;
            }

            float px, py, pz;
            bot.GetCachedPosition(px, py, pz);

            // No destination chosen yet for this leg - pick one within the
            // wander radius of the anchor point.
            float ax, ay, az;
            bot.GetIdleWanderAnchor(ax, ay, az);

            if (!bot.HasIdleWanderTarget())
            {
                const float radius = bot.GetIdleWanderRadius();
                const float angle = RandomFloat(0.0f, 6.2831853f);
                const float dist  = RandomFloat(0.0f, radius);
                bot.SetIdleWanderTarget(ax + std::cos(angle) * dist,
                                        ay + std::sin(angle) * dist, az);
            }

            const Waypoint &target = bot.GetIdleWanderTarget();
            const bool arrived = BotMovement::StepTowardPoint(bot, playerId, target.x, target.y, target.z,
                                                               bot.GetIdleWanderSpeed(), BotMoveType::Walk, deltaSeconds);
            if (arrived)
            {
                bot.ClearIdleWanderTarget();
                bot.SetIdleWaitRemaining(RandomFloat(3.0f, 8.0f));
            }
        }

        void CheckNearbyPlayers(Bot &bot, float radius)
        {
            const int playerId = bot.GetPlayerId();
            if (playerId == kInvalidId)
                return;

            float px, py, pz;
            bot.GetCachedPosition(px, py, pz);

            std::vector<int> candidates;
            BotManager::Get().GetNearbyRealPlayers(px, py, pz, radius, candidates);

            std::unordered_set<int> currentlyNearby;
            currentlyNearby.reserve(candidates.size());
            for (int otherId : candidates)
            {
                if (otherId == playerId)
                    continue;

                float ox = 0.0f, oy = 0.0f, oz = 0.0f;
                if (!sampgdk::GetPlayerPos(otherId, &ox, &oy, &oz))
                    continue;

                currentlyNearby.insert(otherId);
                if (bot.NearbyPlayers().find(otherId) == bot.NearbyPlayers().end())
                {
                    const float distance = DistanceBetween(px, py, pz, ox, oy, oz);
                    CallbackDispatcher::Get().OnBotPlayerNearby(bot.GetBotId(), otherId, distance);
                }
            }

            bot.NearbyPlayers() = std::move(currentlyNearby);
        }
    }
}
