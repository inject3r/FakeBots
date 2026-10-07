// ============================================================================
//  FakeBots :: BotFileLoader.cpp
// ============================================================================
#include "BotFileLoader.h"
#include "BotManager.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <exception>
#include <fstream>

namespace FakeBots
{
    namespace BotFileLoader
    {
        using nlohmann::json;

        namespace
        {
            int GetInt(const json &o, const char *key, int fallback)
            {
                auto it = o.find(key);
                if (it == o.end() || !it->is_number())
                    return fallback;
                const double v = it->get<double>();
                if (!(v > -2147483648.0 && v < 2147483647.0))
                    return fallback;
                return static_cast<int>(v);
            }

            float GetFloat(const json &o, const char *key, float fallback)
            {
                auto it = o.find(key);
                if (it == o.end() || !it->is_number())
                    return fallback;
                const float v = it->get<float>();
                return std::isfinite(v) ? v : fallback;
            }

            bool GetBool(const json &o, const char *key, bool fallback)
            {
                auto it = o.find(key);
                return (it != o.end() && it->is_boolean()) ? it->get<bool>() : fallback;
            }
        }

        int LoadFromFile(const std::string &path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                sampgdk::logprintf("[FakeBots] FakeBotLoadFromFile: could not open '%s'.", path.c_str());
                return 0;
            }

            json root;
            try
            {
                file >> root;
            }
            catch (const std::exception &e)
            {
                sampgdk::logprintf("[FakeBots] FakeBotLoadFromFile: invalid JSON in '%s' (%s).", path.c_str(), e.what());
                return 0;
            }

            if (!root.is_array())
            {
                sampgdk::logprintf("[FakeBots] FakeBotLoadFromFile: '%s' must contain a JSON array.", path.c_str());
                return 0;
            }

            int created = 0;
            int index = -1;
            for (const auto &entry : root)
            {
                ++index;
                // A bad entry (wrong type, missing field, nonsense number) must never take
                // the server down: nlohmann::json throws on every type mismatch.
                try
                {
                    if (!entry.is_object())
                    {
                        sampgdk::logprintf("[FakeBots] FakeBotLoadFromFile: entry %d is not an object - skipped.", index);
                        continue;
                    }
                    if (!entry.contains("name") || !entry["name"].is_string())
                    {
                        sampgdk::logprintf("[FakeBots] FakeBotLoadFromFile: entry %d has no string \"name\" - skipped.", index);
                        continue;
                    }

                    PendingSpawn spawn;
                    spawn.skin         = GetInt(entry, "skin", 0);
                    spawn.x            = GetFloat(entry, "x", 0.0f);
                    spawn.y            = GetFloat(entry, "y", 0.0f);
                    spawn.z            = GetFloat(entry, "z", 0.0f);
                    spawn.angle        = GetFloat(entry, "angle", 0.0f);
                    spawn.weapon       = GetInt(entry, "weapon", 0);
                    spawn.ammo         = GetInt(entry, "ammo", 0);
                    spawn.virtualWorld = GetInt(entry, "virtualworld", 0);
                    spawn.interior     = GetInt(entry, "interior", 0);

                    const std::string name = entry["name"].get<std::string>();
                    const int botId = BotManager::Get().RequestCreate(name, spawn);
                    if (botId == kInvalidId)
                        continue;

                    created++;

                    // Waypoints/loop can't be applied yet - the bot hasn't
                    // finished connecting. Queue them onto the Bot object now;
                    // BotManager::ResolveConnection() only touches spawn info,
                    // so it's safe for these to already be sitting there when
                    // OnFakeBotConnect fires and the gamemode (or the bot
                    // itself, once spawned) starts consuming them.
                    if (entry.contains("waypoints") && entry["waypoints"].is_array())
                    {
                        Bot *bot = BotManager::Get().GetByBotId(botId);
                        if (bot != nullptr)
                        {
                            for (const auto &wp : entry["waypoints"])
                            {
                                if (wp.is_array() && wp.size() >= 3 &&
                                    wp[0].is_number() && wp[1].is_number() && wp[2].is_number())
                                    bot->PushWaypoint(wp[0].get<float>(), wp[1].get<float>(), wp[2].get<float>());
                                else
                                    sampgdk::logprintf("[FakeBots] FakeBotLoadFromFile: entry %d has an invalid waypoint - skipped.", index);
                            }
                            bot->SetLoopWaypoints(GetBool(entry, "loop", false));

                            if (!bot->Waypoints().empty())
                            {
                                const Waypoint &first = bot->Waypoints().front();
                                bot->SetMoveTarget(first.x, first.y, first.z, 1.0f, BotMoveType::Walk);
                            }
                        }
                    }
                }
                catch (const std::exception &e)
                {
                    sampgdk::logprintf("[FakeBots] FakeBotLoadFromFile: entry %d rejected (%s).", index, e.what());
                }
            }

            return created;
        }
    }
}
