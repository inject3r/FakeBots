// ============================================================================
//  FakeBots :: LanguageManager.h
//  Loads FakeBots.json (default) or FakeBots.<lang>.json (e.g. FakeBots.fa.json,
//  FakeBots.ru.json) from the plugin's language directory and exposes flat
//  key -> string lookups to both the C++ core (for logprintf messages) and
//  Pawn scripts (via FakeBotGetText). The plugin ships with English text
//  only; additional languages are plain data files, not code, so server
//  owners can add or edit translations without recompiling anything.
// ============================================================================
#pragma once

#include "../core/Common.h"

namespace FakeBots
{
    class LanguageManager
    {
    public:
        static LanguageManager& Get();

        // Directory that holds FakeBots.json / FakeBots.<lang>.json.
        // Defaults to "plugins/FakeBots/lang" but is overridable via
        // FakeBotSetLanguageDirectory() for non-standard layouts.
        void SetDirectory(const std::string &directory);
        const std::string& GetDirectory() const { return m_directory; }

        // Switches the active language and (re)loads its JSON file. Falls
        // back to the default "FakeBots.json" for any key missing from the
        // selected language file, and ultimately to the key itself if the
        // default file is also missing the key - FakeBotGetText() never
        // returns an empty string.
        bool SetLanguage(const std::string &languageCode);
        const std::string& GetLanguage() const { return m_currentLanguage; }

        bool Reload();

        // Returns a reference into the internal string table - stable
        // until the next SetLanguage()/Reload() call, which is always fine
        // for the natives' immediate copy-out-to-Pawn-buffer use pattern.
        // Avoids an allocation+copy on every single lookup compared to
        // returning by value.
        const std::string& GetText(const std::string &key) const;

    private:
        LanguageManager() = default;

        bool LoadFile(const std::string &path, std::unordered_map<std::string, std::string> &out) const;
        std::string BuildPath(const std::string &languageCode) const;

        std::string m_directory = "plugins/FakeBots/lang";
        std::string m_currentLanguage = "en";
        std::unordered_map<std::string, std::string> m_strings;        // active language
        std::unordered_map<std::string, std::string> m_defaultStrings; // FakeBots.json fallback
        mutable std::unordered_map<std::string, std::string> m_missingKeyCache; // key -> key, memoized so GetText() can return a stable reference
    };
}
