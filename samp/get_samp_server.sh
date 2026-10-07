#!/usr/bin/env bash
# =============================================================================
#  Completes the SA-MP test server folder.
#
#  The SA-MP licence (clause e: "may not be distributed ... without written permission") does
#  not allow shipping samp03svr with this project, so this script fetches it from SA-MP's own
#  download host. It also fetches the Pawn standard includes (open source, pawn-lang) the test
#  gamemodes compile against, then compiles the gamemodes.
#
#      ./get_samp_server.sh
#      SAMP_SERVER_ARCHIVE=/path/samp037svr_R2-1.tar.gz ./get_samp_server.sh   # you already have it
#
#  The tests in this project were run with  samp03svr  "0.3.7-R2", sha256
#  5fed190e2aa503b98f8a74394864c9e5662f693aab9147593081a4cfb441e022
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")"
HERE="$(pwd)"
TESTED_SHA256="5fed190e2aa503b98f8a74394864c9e5662f693aab9147593081a4cfb441e022"
URLS=(
  "https://files.sa-mp.com/samp037svr_R2-1.tar.gz"
  "https://files.sa-mp.com/samp037svr_R2-2-1.tar.gz"
  "http://files.sa-mp.com/samp037svr_R2-1.tar.gz"
  "https://gta-multiplayer.cz/downloads/samp037svr_R2-2-1.tar.gz"
)
SAMP_STDLIB_COMMIT="8ffb055624308b25521665b60e78b5e6e6b3717f"
PAWN_STDLIB_COMMIT="e96507d9a6ddaae5bb0f3ec31479ba805aeff964"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT

# ---- 1. the server -----------------------------------------------------------
if [ ! -x samp03svr ]; then
  if [ -n "${SAMP_SERVER_ARCHIVE:-}" ]; then
    cp "$SAMP_SERVER_ARCHIVE" "$tmp/samp.tar.gz"
  else
    ok=0
    for u in "${URLS[@]}"; do
      echo "trying $u"
      if curl -fsSL --connect-timeout 15 --max-time 120 --retry 2 -o "$tmp/samp.tar.gz" "$u" && tar -tzf "$tmp/samp.tar.gz" >/dev/null 2>&1; then ok=1; break; fi
    done
    [ "$ok" = 1 ] || { echo "Could not download the SA-MP server. Download samp037svr_R2-1.tar.gz from https://sa-mp.com/download.php and run:  SAMP_SERVER_ARCHIVE=<file> ./get_samp_server.sh"; exit 1; }
  fi
  mkdir -p "$tmp/x" && tar -xzf "$tmp/samp.tar.gz" -C "$tmp/x"
  bin="$(find "$tmp/x" -name samp03svr -type f | head -1)"
  [ -n "$bin" ] || { echo "samp03svr not found inside the archive"; exit 1; }
  cp "$bin" samp03svr && chmod +x samp03svr
  lic="$(find "$tmp/x" -name 'samp-license.txt' -type f | head -1)"; [ -n "$lic" ] && cp "$lic" samp-license.txt || true
fi
file samp03svr | grep -q 'ELF 32-bit' || { echo "samp03svr is not a 32-bit Linux executable"; exit 1; }
version="$(strings -a samp03svr | grep -m1 -E '^0\.3\.[0-9A-Za-z]+(-R[0-9-]+)?$' || true)"
sum="$(sha256sum samp03svr | awk '{print $1}')"
echo "samp03svr version: ${version:-unknown}   sha256: $sum"
if [ "$sum" = "$TESTED_SHA256" ]; then echo "-> identical to the build the project was tested with."
else echo "-> NOT the exact build used for the recorded tests (they ran with $TESTED_SHA256); the protocol is the same for 0.3.7."; fi

# ---- 2. Pawn standard includes (for the test gamemodes) ------------------------
if [ ! -f pawno/include/a_samp.inc ]; then
  for repo in "pawn-lang/samp-stdlib:$SAMP_STDLIB_COMMIT" "pawn-lang/pawn-stdlib:$PAWN_STDLIB_COMMIT"; do
    name="${repo%%:*}"; commit="${repo##*:}"
    echo "fetching includes: $name @ ${commit:0:7}"
    curl -fsSL --retry 3 -o "$tmp/inc.tar.gz" "https://codeload.github.com/$name/tar.gz/$commit"
    mkdir -p "$tmp/inc" && tar -xzf "$tmp/inc.tar.gz" -C "$tmp/inc" --strip-components=1
    cp "$tmp"/inc/*.inc pawno/include/ && rm -rf "$tmp/inc"
  done
fi
cp ../include/FakeBots.inc pawno/include/FakeBots.inc

# ---- 3. compile the gamemodes with the Pawn compiler that ships with open.mp -------
PAWNCC="$HERE/../omp/qawno/pawncc"
if [ -x "$PAWNCC" ]; then
  for gm in fbtest fbscn fakebots_demo fakebots_sample; do
    LD_LIBRARY_PATH="$(dirname "$PAWNCC")" "$PAWNCC" "gamemodes/$gm.pwn" "-ogamemodes/$gm.amx" -ipawno/include -d3 >/dev/null 2>&1 \
      && echo "compiled gamemodes/$gm.amx" || echo "WARNING: gamemodes/$gm.pwn did not compile"
  done
else
  echo "omp/qawno/pawncc not found - compile the gamemodes with your own pawncc (include path: pawno/include)"
fi
echo "samp/ is ready:  ./start.sh"
