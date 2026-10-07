// ============================================================================
//  FakeBots :: BotGroupManager.cpp
// ============================================================================
#include "BotGroupManager.h"

namespace FakeBots
{
    BotGroupManager& BotGroupManager::Get()
    {
        static BotGroupManager instance;
        return instance;
    }

    void BotGroupManager::Reset()
    {
        m_groups.clear();
    }

    bool BotGroupManager::CreateGroup(int groupId)
    {
        if (m_groups.find(groupId) != m_groups.end())
            return false; // already exists
        m_groups.emplace(groupId, std::unordered_set<int>());
        return true;
    }

    bool BotGroupManager::DestroyGroup(int groupId)
    {
        return m_groups.erase(groupId) > 0;
    }

    bool BotGroupManager::IsValidGroup(int groupId) const
    {
        return m_groups.find(groupId) != m_groups.end();
    }

    bool BotGroupManager::AddToGroup(int botId, int groupId)
    {
        auto it = m_groups.find(groupId);
        if (it == m_groups.end())
            return false;
        it->second.insert(botId);
        return true;
    }

    bool BotGroupManager::RemoveFromGroup(int botId, int groupId)
    {
        auto it = m_groups.find(groupId);
        if (it == m_groups.end())
            return false;
        return it->second.erase(botId) > 0;
    }

    bool BotGroupManager::IsInGroup(int botId, int groupId) const
    {
        auto it = m_groups.find(groupId);
        if (it == m_groups.end())
            return false;
        return it->second.find(botId) != it->second.end();
    }

    int BotGroupManager::GetGroupCount(int groupId) const
    {
        auto it = m_groups.find(groupId);
        return (it == m_groups.end()) ? 0 : static_cast<int>(it->second.size());
    }

    const std::unordered_set<int>* BotGroupManager::GetMembers(int groupId) const
    {
        auto it = m_groups.find(groupId);
        return (it == m_groups.end()) ? nullptr : &it->second;
    }

    void BotGroupManager::PurgeBot(int botId)
    {
        for (auto &pair : m_groups)
            pair.second.erase(botId);
    }
}
