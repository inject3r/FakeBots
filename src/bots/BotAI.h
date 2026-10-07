// ============================================================================
//  FakeBots :: BotAI.h
//  Small, opt-in behaviour helpers layered on top of the core movement
//  system: combat (face + periodic damage), idle wander (random local
//  patrol when a bot has nothing else to do), and nearby-player detection
//  (drives OnFakeBotPlayerNearby). None of these run unless explicitly
//  enabled per-bot through the matching FakeBot* native.
// ============================================================================
#pragma once

#include "Bot.h"

namespace FakeBots
{
    namespace BotAI
    {
        // Faces the combat target, periodically applies damage via
        // SetPlayerHealth/SetPlayerArmour while both are alive, connected
        // and within range. Automatically stops (and fires nothing) once
        // the target disconnects, dies, or leaves range for too long.
        void StepCombat(Bot &bot, float deltaSeconds);

        // Picks a random point within the bot's wander radius and walks to
        // it, pausing briefly on arrival, whenever the bot is spawned and
        // has no other active route/follow/combat command.
        void StepIdleWander(Bot &bot, float deltaSeconds);

        // Diffs the bot's current nearby-player set against a fresh
        // distance scan and fires OnFakeBotPlayerNearby for every newly
        // entered player (not on every tick they stay close).
        void CheckNearbyPlayers(Bot &bot, float radius);
    }
}
