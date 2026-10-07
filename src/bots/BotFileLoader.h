// ============================================================================
//  FakeBots :: BotFileLoader.h
//  Optional convenience loader: reads a JSON array of bot definitions and
//  creates them all through the normal BotManager::RequestCreate() path.
//  This is purely a bulk-creation shortcut - it does not replace or
//  reintroduce the old file-driven configuration model. Nothing about the
//  plugin's core behaviour depends on this file existing; it's just a
//  faster way to spin up a starting cast of bots than calling
//  FakeBotCreate() once per bot from Pawn.
//
//  Expected JSON shape (array of objects):
//  [
//    {
//      "name": "Patrol_Bot", "skin": 280,
//      "x": 1958.33, "y": 1343.14, "z": 15.37, "angle": 0.0,
//      "weapon": 0, "ammo": 0, "virtualworld": 0, "interior": 0,
//      "waypoints": [[1958.33,1343.14,15.37], [1975.02,1343.96,15.37]],
//      "loop": true
//    }
//  ]
//  Every field except "name" is optional and defaults sensibly.
// ============================================================================
#pragma once

#include "../core/Common.h"

namespace FakeBots
{
    namespace BotFileLoader
    {
        // Returns the number of bots successfully created.
        int LoadFromFile(const std::string &path);
    }
}
