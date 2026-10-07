cmake_minimum_required(VERSION 3.16)

project(FakeBots LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(OUTPUT_NAME "FakeBots")

# --------------------------------------------------------------------
# Vendor sources (official SA-MP plugin SDK glue - no hooking libraries)
# --------------------------------------------------------------------

set(VENDOR_SOURCES
    ${CMAKE_SOURCE_DIR}/lib/sdk/amxplugin.cpp
    ${CMAKE_SOURCE_DIR}/lib/sampgdk/sampgdk.c
)

# --------------------------------------------------------------------
# Plugin sources
# --------------------------------------------------------------------

set(PLUGIN_SOURCES
    src/core/Plugin.cpp

    src/bots/Bot.cpp
    src/bots/BotManager.cpp
    src/bots/BotMovement.cpp
    src/bots/BotGroupManager.cpp
    src/bots/BotAI.cpp
    src/bots/BotFileLoader.cpp

    src/natives/Natives.cpp

    src/callbacks/CallbackDispatcher.cpp
    src/callbacks/ServerHooks.cpp

    src/i18n/LanguageManager.cpp

    src/network/SampBotClient.cpp
)

# --------------------------------------------------------------------
# Embedded RakNet client (third_party/raknet)
# --------------------------------------------------------------------
#
# A vendored, client-mode build of the openmultiplayer RakNet fork (the same
# reliability layer / RPC layer / bitstream the real servers use). It is
# compiled from the files that live in this repository - nothing is
# downloaded while configuring, so builds are offline and reproducible.
#
# The upstream fork is *server* oriented (it needs the whole open.mp SDK and
# decodes instead of encodes datagrams). third_party/raknet therefore carries a
# small, reviewable set of client-mode patches - see
# third_party/raknet/FAKEBOTS_PATCHES.md and tools/apply_raknet_client_patches.py.
set(RAKNET_DIR ${CMAKE_SOURCE_DIR}/third_party/raknet)

set(RAKNET_SOURCES
    ${RAKNET_DIR}/Source/BitStream.cpp
    ${RAKNET_DIR}/Source/CheckSum.cpp
    ${RAKNET_DIR}/Source/CommandParserInterface.cpp
    ${RAKNET_DIR}/Source/ConsoleServer.cpp
    ${RAKNET_DIR}/Source/DS_HuffmanEncodingTree.cpp
    ${RAKNET_DIR}/Source/DataBlockEncryptor.cpp
    ${RAKNET_DIR}/Source/GetTime.cpp
    ${RAKNET_DIR}/Source/InternalPacketPool.cpp
    ${RAKNET_DIR}/Source/NetworkTypes.cpp
    ${RAKNET_DIR}/Source/PluginInterface.cpp
    ${RAKNET_DIR}/Source/RPCMap.cpp
    ${RAKNET_DIR}/Source/RakClient.cpp
    ${RAKNET_DIR}/Source/RakNetCommandParser.cpp
    ${RAKNET_DIR}/Source/RakNetworkFactory.cpp
    ${RAKNET_DIR}/Source/RakPeer.cpp
    ${RAKNET_DIR}/Source/RakSleep.cpp
    ${RAKNET_DIR}/Source/Rand.cpp
    ${RAKNET_DIR}/Source/ReliabilityLayer.cpp
    ${RAKNET_DIR}/Source/SHA1.cpp
    ${RAKNET_DIR}/Source/SimpleMutex.cpp
    ${RAKNET_DIR}/Source/SocketLayer.cpp
    ${RAKNET_DIR}/Source/StringCompressor.cpp
    ${RAKNET_DIR}/Source/StringTable.cpp
    ${RAKNET_DIR}/Source/_findfirst.cpp
    ${RAKNET_DIR}/Source/rakserver.cpp
    ${RAKNET_DIR}/Source/rijndael.cpp
    ${RAKNET_DIR}/SAMPRakNet.cpp
)

add_library(FakeBotsRakNet STATIC ${RAKNET_SOURCES})
target_include_directories(FakeBotsRakNet PUBLIC
    ${RAKNET_DIR}
    ${RAKNET_DIR}/Include
    ${RAKNET_DIR}/Include/raknet
)
# RAKNET_LEGACY selects the SA-MP wire protocol (packet ids, reliability layer
# framing). It changes the *headers* too, so it must be visible to every
# consumer of the library - hence PUBLIC.
target_compile_definitions(FakeBotsRakNet PUBLIC RAKNET_LEGACY=1)
set_target_properties(FakeBotsRakNet PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(${OUTPUT_NAME} SHARED
    ${VENDOR_SOURCES}
    ${PLUGIN_SOURCES}
)

target_link_libraries(${OUTPUT_NAME} PRIVATE FakeBotsRakNet)

if(NOT WIN32)
    set_target_properties(${OUTPUT_NAME} PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
endif()

# --------------------------------------------------------------------
# Optional: rakprobe - stand-alone diagnostic client (not part of the plugin)
#
# Joins ONE bot to the server on 127.0.0.1 and prints the complete handshake
# (cookie -> auth -> accept -> join -> class -> spawn). tests/run_server_test.py uses
# it, from another loopback address, as the "real player" that must still get the last
# free slot of a server the bots filled up.
#
#   cmake -S . -B build -DFAKEBOTS_BUILD_PROBE=ON && cmake --build build --target rakprobe
# --------------------------------------------------------------------
option(FAKEBOTS_BUILD_PROBE "Build the rakprobe diagnostic client" OFF)
if(FAKEBOTS_BUILD_PROBE AND NOT WIN32)
    add_executable(rakprobe tools/probe/rakprobe.cpp)
    target_link_libraries(rakprobe PRIVATE FakeBotsRakNet pthread)
    set_target_properties(rakprobe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    # same ABI as the library it links: a 32-bit client (see "Compiler options" below)
    target_compile_options(rakprobe PRIVATE -m32 -w)
    target_link_options(rakprobe PRIVATE -m32)
endif()

# --------------------------------------------------------------------
# Include directories
# --------------------------------------------------------------------

target_include_directories(${OUTPUT_NAME}
PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/lib/sampgdk
    ${CMAKE_SOURCE_DIR}/lib/sdk
    ${CMAKE_SOURCE_DIR}/lib/sdk/amx
    ${CMAKE_SOURCE_DIR}/lib/json
)

# --------------------------------------------------------------------
# Defines
# --------------------------------------------------------------------

target_compile_definitions(${OUTPUT_NAME}
PRIVATE
    NDEBUG

    FAKEBOTS_EXPORTS

    HAVE_STDINT_H

    SAMPGDK_AMALGAMATION
    SAMPGDK_STATIC
    SAMPGDK_CPP_WRAPPERS
)

# sampgdk.c already #defines IN_SAMPGDK itself at the top of the file, so
# nothing extra is needed here - it's the *consuming* translation units
# (our own .cpp files) that must NOT have IN_SAMPGDK defined, so they get
# the C++ namespace wrappers instead of the plain C macro aliases (see
# SAMPGDK_CPP_WRAPPERS above).
#
# sampgdk.c is third-party, unmodified vendor code that happens to trip
# -Wmaybe-uninitialized on some GCC/MinGW versions inside its large
# variadic-style parameter marshalling switch (false positives - each
# path is fully initialized before use, GCC just can't always prove it
# across that particular macro-generated switch). Silenced for this one
# file only so real warnings in FakeBots' own code are never masked.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/lib/sampgdk/sampgdk.c
        PROPERTIES COMPILE_OPTIONS "-Wno-maybe-uninitialized;-Wno-unknown-pragmas"
    )
endif()

# --------------------------------------------------------------------
# Windows
# --------------------------------------------------------------------

if(WIN32)

    target_compile_definitions(${OUTPUT_NAME}
    PRIVATE
        WIN32
        _WIN32
        SAMPGDK_WINDOWS
        _CRT_SECURE_NO_WARNINGS
    )

    target_link_libraries(${OUTPUT_NAME}
    PRIVATE
        ws2_32
        winmm
        psapi
    )

    # Required plugin ABI: SA-MP calls exported functions with __stdcall on
    # Windows (see lib/sdk/plugincommon.h), so GCC/MinGW mangles them as
    # e.g. "_Supports@0" while FakeBots.def lists the plain "Supports" the
    # server actually looks up via GetProcAddress. --enable-stdcall-fixup
    # is what makes ld match the two - passing it explicitly (rather than
    # relying on the implicit, noisy default) keeps the build silent.
    target_link_options(${OUTPUT_NAME}
    PRIVATE
        -Wl,--enable-stdcall-fixup
    )

    # The plugin is loaded by a server that ships none of the MinGW runtime DLLs
    # (libgcc_s_dw2-1.dll, libstdc++-6.dll, libwinpthread-1.dll): link them in so the
    # single FakeBots.dll is self-contained.
    if(MINGW)
        target_link_options(${OUTPUT_NAME}
        PRIVATE
            -static-libgcc
            -static-libstdc++
            -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic
        )
    endif()

    target_sources(${OUTPUT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/FakeBots.def
    )

    set_target_properties(${OUTPUT_NAME}
    PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
    )

# --------------------------------------------------------------------
# Linux
# --------------------------------------------------------------------

else()

    target_compile_definitions(${OUTPUT_NAME}
    PRIVATE
        LINUX
        _LINUX
        __LINUX__
        SAMPGDK_LINUX
    )

    target_link_libraries(${OUTPUT_NAME}
    PRIVATE
        rt
        pthread
    )

    # Export only the plugin ABI, and carry our own libstdc++/libgcc so the
    # plugin does not depend on the host's C++ runtime version.
    target_link_options(${OUTPUT_NAME}
    PRIVATE
        -Wl,--version-script=${CMAKE_SOURCE_DIR}/FakeBots.map
        -Wl,--exclude-libs,ALL
        -static-libstdc++
        -static-libgcc
    )
    set_target_properties(${OUTPUT_NAME} PROPERTIES LINK_DEPENDS ${CMAKE_SOURCE_DIR}/FakeBots.map)

    set_target_properties(${OUTPUT_NAME}
    PROPERTIES
        PREFIX ""
        SUFFIX ".so"
    )

endif()

# --------------------------------------------------------------------
# Compiler options
#
# SA-MP servers (and their plugin ABI) are 32-bit on every platform, so
# the plugin must be built 32-bit even on a 64-bit host/toolchain - this
# matches the official SDK requirement and is why CI cross-compiles with
# -m32 / a 32-bit MinGW toolchain rather than producing a 64-bit binary.
# --------------------------------------------------------------------

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")

    # Third-party code: no warnings, and never export its symbols (another
    # plugin may statically link a different RakNet / sampgdk).
    target_compile_options(FakeBotsRakNet PRIVATE
        -O2
        -fno-strict-aliasing
        -w
        -fvisibility=hidden
        -fvisibility-inlines-hidden
    )

    target_compile_options(${OUTPUT_NAME}
    PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-O3>
        $<$<COMPILE_LANGUAGE:CXX>:-O3>

        -fshort-wchar
        -fno-strict-aliasing
        -mfpmath=sse
        -msse2
        -Wall
        -Wno-unknown-pragmas
    )

    if(NOT MINGW)

        target_compile_options(FakeBotsRakNet PRIVATE -m32)

        target_compile_options(${OUTPUT_NAME}
        PRIVATE
            -m32
        )

        target_link_options(${OUTPUT_NAME}
        PRIVATE
            -m32
        )

    endif()

elseif(MSVC)

    target_compile_options(${OUTPUT_NAME}
    PRIVATE
        /O2
        /W3
        /EHsc
    )

endif()

set_target_properties(${OUTPUT_NAME}
PROPERTIES
    OUTPUT_NAME ${OUTPUT_NAME}
)

# --------------------------------------------------------------------
# Install layout - mirrors a standard SA-MP/open.mp server directory so
# `cmake --install` can be pointed straight at a server folder.
# --------------------------------------------------------------------

install(TARGETS ${OUTPUT_NAME} DESTINATION plugins)
install(FILES include/FakeBots.inc DESTINATION pawno/include OPTIONAL)
install(FILES include/FakeBots.inc DESTINATION qawno/include OPTIONAL)
install(DIRECTORY lang/ DESTINATION plugins/FakeBots/lang)
