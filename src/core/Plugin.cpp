// ============================================================================
//  FakeBots :: Plugin.cpp
//
//  Standard SA-MP/open.mp plugin entry points. Everything here goes through
//  the official, documented plugin ABI (Supports/Load/Unload/AmxLoad/
//  AmxUnload/ProcessTick + sampgdk's callback-export contract) - no process
//  memory scanning, no binary-specific offsets. That's what makes this
//  build portable across SA-MP server versions and open.mp without a
//  single code change.
// ============================================================================
#include <cstdlib>
#include <cstring>
#include "Common.h"
#include "../bots/BotManager.h"
#include "../callbacks/CallbackDispatcher.h"
#include "../i18n/LanguageManager.h"
#include "../natives/Natives.h"

#include <SAMPRakNet.hpp>
#include <string>

extern void *pAMXFunctions;

namespace
{
    void PrintBanner()
    {
        sampgdk::logprintf(" ");
        sampgdk::logprintf("  %s v%s", FAKEBOTS_NAME, FAKEBOTS_VERSION_STRING);
        sampgdk::logprintf("  by %s - %s", FAKEBOTS_AUTHOR, FAKEBOTS_REPOSITORY);
        sampgdk::logprintf("  Running on: %s", FAKEBOTS_WINDOWS ? "Windows" : "Linux");
        sampgdk::logprintf(" ");
    }
}

PLUGIN_EXPORT unsigned int PLUGIN_CALL Supports()
{
    // SUPPORTS_AMX_NATIVES is required for the server to ever call
    // AmxLoad()/AmxUnload() on this plugin at all - without it, none of
    // FakeBots.inc's natives get registered and every call from Pawn
    // fails with "Function not registered", even though the plugin
    // itself loads and prints its banner successfully.
    return sampgdk::Supports() | SUPPORTS_VERSION | SUPPORTS_AMX_NATIVES | SUPPORTS_PROCESS_TICK;
}

PLUGIN_EXPORT bool PLUGIN_CALL Load(void **ppData)
{
    pAMXFunctions = ppData[PLUGIN_DATA_AMX_EXPORTS];

    if (!sampgdk::Load(ppData))
        return false;

    PrintBanner();

    // Opt-in network trace for bug reports: start the server with FAKEBOTS_DEBUG=1.
    if (const char *dbg = std::getenv("FAKEBOTS_DEBUG"))
        SAMPRakNet::SetDebugLogging(dbg[0] != '\0' && std::strcmp(dbg, "0") != 0);

    // Default language directory; overridable at runtime via
    // FakeBotSetLanguageDirectory() before FakeBotSetLanguage() is
    // first called from Pawn.
    FakeBots::LanguageManager::Get().SetLanguage("en");

    return true;
}

PLUGIN_EXPORT void PLUGIN_CALL Unload()
{
    FakeBots::BotManager::Get().Reset();
    FakeBots::CallbackDispatcher::Get().ClearPending();
    // Let every bot finish its clean disconnect before the library goes away.
    FakeBots::SampBotClient::FlushBackgroundDisconnects();
    sampgdk::logprintf("  %s v%s unloaded.", FAKEBOTS_NAME, FAKEBOTS_VERSION_STRING);
    sampgdk::Unload();
}

PLUGIN_EXPORT int PLUGIN_CALL AmxLoad(AMX *amx)
{
    // sampgdk installs its own low-level amx_Exec hook once, globally, at
    // Load() time (see sampgdk_amxhooks_init in sampgdk.c) - it does not
    // need to be told about each individual script here. We only need to
    // register FakeBots' own natives and callback targets for this AMX.
    FakeBots::CallbackDispatcher::Get().RegisterAmx(amx);
    const int result = FakeBots::Natives::RegisterAll(amx);
    if (result != AMX_ERR_NONE)
        sampgdk::logprintf("[FakeBots] Warning: native registration returned error code %d for a loaded script.", result);
    return result;
}

PLUGIN_EXPORT int PLUGIN_CALL AmxUnload(AMX *amx)
{
    FakeBots::CallbackDispatcher::Get().UnregisterAmx(amx);
    return AMX_ERR_NONE;
}

PLUGIN_EXPORT void PLUGIN_CALL ProcessTick()
{
    sampgdk::ProcessTick();
    FakeBots::BotManager::Get().Tick();

    // Everything RakNet's per-bot network threads wanted to say is printed here,
    // on the server thread (the server's logger is not thread-safe).
    static std::string logBuffer;
    logBuffer.clear();
    if (SAMPRakNet::DrainLog(logBuffer) != 0)
    {
        size_t start = 0;
        while (start < logBuffer.size())
        {
            size_t end = logBuffer.find('\n', start);
            if (end == std::string::npos)
                end = logBuffer.size();
            if (end > start)
                sampgdk::logprintf("[FakeBots] %.*s", static_cast<int>(end - start), logBuffer.c_str() + start);
            start = end + 1;
        }
    }

    // OnFakeBot* events reach the scripts last, after the server's own
    // callbacks for the same moment have already run.
    FakeBots::CallbackDispatcher::Get().Flush();
}
