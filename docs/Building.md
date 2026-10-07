# Building from source

CMake only. **Nothing is downloaded while configuring**: RakNet is vendored in
`third_party/raknet/`, sampgdk in `lib/`. The plugin is a **32-bit** shared library on both
platforms (the SA-MP / open.mp plugin ABI).

| Platform | Toolchain |
| --- | --- |
| Linux | CMake >= 3.16, GCC or Clang with 32-bit multilib (`gcc-multilib`, `g++-multilib`) |
| Windows | CMake >= 3.16 and MinGW-w64 (32-bit) or Visual Studio (Win32), **or** cross-compile from Linux |

## Linux

```bash
sudo apt-get install -y cmake gcc-multilib g++-multilib
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"          # -> build/FakeBots.so
```

## Windows DLL, cross-compiled from Linux (what the release workflow does)

```bash
sudo apt-get install -y g++-mingw-w64-i686
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-i686.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-win -j"$(nproc)"      # -> build-win/FakeBots.dll
```

The MinGW runtime is linked statically, so `FakeBots.dll` imports only `KERNEL32`, `WS2_32` and
`msvcrt` (the release workflow fails if anything else shows up):

```bash
i686-w64-mingw32-objdump -p build-win/FakeBots.dll | grep "DLL Name"
```

## Windows, native

```powershell
cmake -S . -B build -A Win32 -DCMAKE_BUILD_TYPE=Release     # Visual Studio
cmake --build build --config Release                         # -> build\Release\FakeBots.dll
```

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release   # MinGW-w64 i686
cmake --build build
```

(The native Windows builds are not exercised by the project's CI; the MinGW cross build is.)

## Optional targets

| Option | Effect |
| --- | --- |
| `-DFAKEBOTS_BUILD_PROBE=ON` then `--target rakprobe` | builds `rakprobe`, the one-bot diagnostic client used by the live tests (`tests/bin/rakprobe` is a prebuilt copy) |

## Installing into a server folder

```bash
cmake --install build --prefix /path/to/your-server
```

copies the library to `plugins/`, `FakeBots.inc` to the include folders and the language files to
`plugins/FakeBots/lang/`.

## Source audit

```bash
python3 tools/source_audit.py
```

Fails if a forbidden API (`ConnectNPC`, `IsPlayerNPC`, `SetPlayerPos`, `SetVehiclePos`,
unreviewed `SpawnPlayer`) or any NPC reference appears in executable code of the plugin or of the
vendored RakNet. Run by the release workflow before anything is built.

## Regenerating generated / patched files

| File | Generator |
| --- | --- |
| `third_party/raknet/` client-mode patches | `tools/apply_raknet_client_patches.py <pristine RakNet> <out dir>` (commit in `UPSTREAM_COMMIT.txt`) |
| `third_party/raknet/SampCryptData.inc` | `tools/gen_crypt_tables.py --raknet <RakNet checkout> --samp-binary <samp03svr>` |

## Release builds

Pushing a tag builds and publishes everything: [CI-Release.md](CI-Release.md).
