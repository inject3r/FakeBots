// ============================================================================
//  FakeBots :: LanguageManager.cpp
// ============================================================================
#include "LanguageManager.h"
#include <nlohmann/json.hpp>
#include <cctype>
#include <exception>
#include <fstream>

namespace FakeBots
{
    using nlohmann::json;

    LanguageManager& LanguageManager::Get()
    {
        static LanguageManager instance;
        return instance;
    }

    void LanguageManager::SetDirectory(const std::string &directory)
    {
        m_directory = directory;
        Reload();
    }

    std::string LanguageManager::BuildPath(const std::string &languageCode) const
    {
        if (languageCode.empty() || languageCode == "en")
            return m_directory + "/FakeBots.json";
        return m_directory + "/FakeBots." + languageCode + ".json";
    }

    bool LanguageManager::LoadFile(const std::string &path, std::unordered_map<std::string, std::string> &out) const
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
            return false;

        json root;
        try
        {
            file >> root;
        }
        catch (const std::exception &e)
        {
            sampgdk::logprintf("[FakeBots] Failed to parse language file '%s' (%s).", path.c_str(), e.what());
            return false;
        }

        if (!root.is_object())
            return false;

        out.clear();
        for (auto it = root.begin(); it != root.end(); ++it)
        {
            if (it.value().is_string())
                out[it.key()] = it.value().get<std::string>();
        }
        return true;
    }

    bool LanguageManager::SetLanguage(const std::string &languageCode)
    {
        // Always keep the English/default table loaded as a fallback so a
        // partially-translated language file never produces empty strings.
        if (m_defaultStrings.empty())
            LoadFile(m_directory + "/FakeBots.json", m_defaultStrings);

        // The code becomes part of a file name: allow only [A-Za-z0-9_-], 1..16 chars.
        // Anything else (path separators, "..", ...) is refused and changes nothing.
        if (!languageCode.empty())
        {
            bool valid = languageCode.size() <= 16;
            for (const unsigned char c : languageCode)
            {
                if (!(std::isalnum(c) || c == '_' || c == '-'))
                {
                    valid = false;
                    break;
                }
            }
            if (!valid)
            {
                sampgdk::logprintf("[FakeBots] Invalid language code (use letters, digits, '-' and '_' only).");
                return false;
            }
        }

        m_currentLanguage = languageCode.empty() ? "en" : languageCode;

        if (m_currentLanguage == "en")
        {
            m_strings = m_defaultStrings;
            m_missingKeyCache.clear();
            return !m_strings.empty();
        }

        std::unordered_map<std::string, std::string> loaded;
        const bool ok = LoadFile(BuildPath(m_currentLanguage), loaded);
        if (!ok)
        {
            sampgdk::logprintf("[FakeBots] Language '%s' not found, falling back to default (FakeBots.json).", m_currentLanguage.c_str());
            m_strings = m_defaultStrings;
            m_currentLanguage = "en"; // what is actually in use now
            return false;
        }

        m_strings = loaded;
        m_missingKeyCache.clear();
        return true;
    }

    bool LanguageManager::Reload()
    {
        m_defaultStrings.clear();
        return SetLanguage(m_currentLanguage);
    }

    const std::string& LanguageManager::GetText(const std::string &key) const
    {
        auto it = m_strings.find(key);
        if (it != m_strings.end())
            return it->second;

        auto fallback = m_defaultStrings.find(key);
        if (fallback != m_defaultStrings.end())
            return fallback->second;

        // Last resort: surface the key so missing text is obvious in-game,
        // via a memoized self-mapping entry so we can return a reference.
        auto missing = m_missingKeyCache.find(key);
        if (missing != m_missingKeyCache.end())
            return missing->second;

        return m_missingKeyCache.emplace(key, key).first->second;
    }
}
