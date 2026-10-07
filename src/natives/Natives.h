// ============================================================================
//  FakeBots :: Natives.h
//  Declares and registers every AMX_NATIVE_INFO backing FakeBots.inc.
// ============================================================================
#pragma once

#include "../core/Common.h"

namespace FakeBots
{
    namespace Natives
    {
        // Registers all natives on the given AMX instance (call from AmxLoad).
        int RegisterAll(AMX *amx);
    }
}
