// ============================================================================
//  FakeBots :: BotGroupManager.h
//  Lightweight tagging system: a group is just a named bucket of bot ids
//  that bulk-action natives (FakeBotGroup*) iterate over. Groups have no
//  effect on their own - they only exist to save gamemode code from
//  looping over bot ids by hand for common "do X to all of them" cases
//  (patrol squads, traffic sets, crowd fillers, etc.).
// ============================================================================
#pragma once

#include "../core/Common.h"
#include <unordered_set>

namespace FakeBots
{
    class BotGroupManager
    {
    public:
        static BotGroupManager& Get();

        void Reset();

        bool CreateGroup(int groupId);
        bool DestroyGroup(int groupId);
        bool IsValidGroup(int groupId) const;

        bool AddToGroup(int botId, int groupId);
        bool RemoveFromGroup(int botId, int groupId);
        bool IsInGroup(int botId, int groupId) const;

        int GetGroupCount(int groupId) const;
        const std::unordered_set<int>* GetMembers(int groupId) const;

        // Automatically drops a bot from every group it belongs to; called
        // by BotManager when a bot is destroyed/disconnects.
        void PurgeBot(int botId);

    private:
        BotGroupManager() = default;

        std::unordered_map<int, std::unordered_set<int>> m_groups; // groupid -> botids
    };
}
