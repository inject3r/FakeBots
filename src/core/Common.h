// ============================================================================
//  FakeBots :: Common.h
//  Shared type definitions, version metadata and cross-platform helpers.
//
//  Project : FakeBots
//  Author  : Abolfazl Hosseini
//  Repo    : https://github.com/inject3r/FakeBots
//  License : MIT (see LICENSE)
// ============================================================================
#pragma once

// ----------------------------------------------------------------------------
// Version metadata
// ----------------------------------------------------------------------------
#define FAKEBOTS_NAME              "FakeBots"
#define FAKEBOTS_VERSION_MAJOR     3
#define FAKEBOTS_VERSION_MINOR     2
#define FAKEBOTS_VERSION_PATCH     0
#define FAKEBOTS_VERSION_STRING    "3.2.0"
#define FAKEBOTS_AUTHOR            "Abolfazl Hosseini"
#define FAKEBOTS_REPOSITORY        "https://github.com/inject3r/FakeBots"

// ----------------------------------------------------------------------------
// Platform detection
// ----------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
    #define FAKEBOTS_WINDOWS 1
    #define FAKEBOTS_LINUX 0
    #define WIN32_LEAN_AND_MEAN
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #define FAKEBOTS_EXPORT extern "C" __declspec(dllexport)
    #define FAKEBOTS_SLEEP(ms) Sleep(static_cast<DWORD>(ms))
#else
    #define FAKEBOTS_WINDOWS 0
    #define FAKEBOTS_LINUX 1
    #include <unistd.h>
    #define FAKEBOTS_EXPORT extern "C" __attribute__((visibility("default")))
    #define FAKEBOTS_SLEEP(ms) usleep(static_cast<useconds_t>((ms) * 1000))
#endif

// ----------------------------------------------------------------------------
// Common includes
// ----------------------------------------------------------------------------
#include <cstdint>
#include <algorithm>
#include <utility>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <cmath>

// ----------------------------------------------------------------------------
// SA-MP / open.mp SDK (via sampgdk - official, version-independent API)
// ----------------------------------------------------------------------------
#include <sampgdk/sampgdk.h>

// ----------------------------------------------------------------------------
// Values shared with FakeBots.inc - MUST be kept numerically identical to
// FAKEBOTS_MOVE_* / FAKEBOTS_STATE_* in include/FakeBots.inc.
// ----------------------------------------------------------------------------
#define FAKEBOTS_MOVE_WALK    1
#define FAKEBOTS_MOVE_RUN     2
#define FAKEBOTS_MOVE_SPRINT  3

// ----------------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------------
namespace FakeBots
{
    constexpr int      kMaxBots            = 1000;     // Hard ceiling (matches MAX_PLAYERS headroom)
    constexpr int      kInvalidId          = -1;
    constexpr float    kMinMoveDistance    = 0.05f;    // Snap-to-target threshold (GTA units)
    constexpr float    kDefaultWalkSpeed   = 1.0f;      // Units/tick baseline for FAKEBOTS_MOVE_WALK
    constexpr float    kDefaultRunSpeed    = 2.2f;
    constexpr float    kDefaultSprintSpeed = 3.2f;
    constexpr float    kDefaultDriveSpeed  = 30.0f;
    constexpr int      kTickIntervalMs     = 50;        // Default 20 Hz simulation step (see BotManager::SetTickInterval)
    constexpr float    kDefaultNearbyRadius = 15.0f;     // OnFakeBotPlayerNearby trigger distance
    constexpr float    kFarUpdateDistance   = 60.0f;     // Beyond this, movement ticks are throttled
    constexpr int       kFarUpdateSkipTicks  = 4;         // Simulate far bots on every Nth tick only

    inline float DistanceSquared(float x1, float y1, float z1, float x2, float y2, float z2)
    {
        const float dx = x2 - x1;
        const float dy = y2 - y1;
        const float dz = z2 - z1;
        return dx * dx + dy * dy + dz * dz;
    }

    inline float DistanceBetween(float x1, float y1, float z1, float x2, float y2, float z2)
    {
        return std::sqrt(DistanceSquared(x1, y1, z1, x2, y2, z2));
    }

    // Heading from (x1,y1) towards (x2,y2) in SA-MP's convention: 0 = north (+y),
    // 90 = west (-x), 180 = south, 270 = east. (A compass bearing runs the other way
    // round, which made every bot face the mirror image of the direction it walked.)
    inline float AngleTowards(float x1, float y1, float x2, float y2)
    {
        float angle = static_cast<float>(std::atan2(-(x2 - x1), y2 - y1) * (180.0 / 3.14159265358979323846));
        if (angle < 0.0f)
            angle += 360.0f;
        if (angle >= 360.0f)
            angle -= 360.0f;
        return angle;
    }
}
