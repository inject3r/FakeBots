#!/usr/bin/env bash
# =============================================================================
#  Prepares the open.mp test server folder  omp/  (Linux x86) next to the project.
#
#      tools/setup_test_servers.sh                 # download v1.5.8.3079 (pinned)
#      OMP_ARCHIVE=/path/open.mp-linux-x86.tar.gz tools/setup_test_servers.sh
#
#  The SA-MP folder is prepared by  samp/get_samp_server.sh  (the SA-MP licence forbids
#  shipping the server with this project).
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OMP_VERSION="${OMP_VERSION:-v1.5.8.3079}"
OMP_URL="https://github.com/openmultiplayer/open.mp/releases/download/${OMP_VERSION}/open.mp-linux-x86.tar.gz"
PLUGIN="${PLUGIN:-}"

if [ -z "$PLUGIN" ]; then
  for c in "$ROOT/dist/FakeBots.so" "$ROOT/build/FakeBots.so"; do [ -f "$c" ] && PLUGIN="$c" && break; done
fi
[ -f "${PLUGIN:-/nonexistent}" ] || { echo "FakeBots.so not found: build the plugin first or set PLUGIN=/path/FakeBots.so"; exit 1; }

mkdir -p "$ROOT/omp"
if [ ! -x "$ROOT/omp/omp-server" ]; then
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' EXIT
  if [ -n "${OMP_ARCHIVE:-}" ]; then
    cp "$OMP_ARCHIVE" "$tmp/omp.tar.gz"
  else
    echo "downloading $OMP_URL"
    curl -fsSL --retry 5 --retry-delay 3 -o "$tmp/omp.tar.gz" "$OMP_URL"
  fi
  tar -xzf "$tmp/omp.tar.gz" -C "$tmp"
  cp -a "$tmp/Server/." "$ROOT/omp/"
  sha256sum "$tmp/omp.tar.gz" | awk '{print $1}' > "$ROOT/omp/ARCHIVE.sha256"
fi

# the plugin, its language files and the Pawn include
mkdir -p "$ROOT/omp/plugins/FakeBots/lang"
cp "$PLUGIN" "$ROOT/omp/plugins/FakeBots.so"
cp "$ROOT"/lang/*.json "$ROOT/omp/plugins/FakeBots/lang/"
cp "$ROOT/include/FakeBots.inc" "$ROOT/omp/qawno/include/FakeBots.inc"
mkdir -p "$ROOT/omp/scriptfiles"
cp "$ROOT/tests/gamemode/fbtest_bots.json" "$ROOT/omp/scriptfiles/"
chmod +x "$ROOT/omp/omp-server" "$ROOT/omp/qawno/pawncc"

# gamemodes: keep the .pwn next to the compiled .amx
mkdir -p "$ROOT/omp/gamemodes"
cp "$ROOT/tests/gamemode/fbtest.pwn" "$ROOT/tests/gamemode/fbscn.pwn" "$ROOT/omp/gamemodes/"
cp "$ROOT/examples/gamemode/FakeBots_Demo.pwn" "$ROOT/omp/gamemodes/fakebots_demo.pwn"
cp "$ROOT/examples/gamemode/FakeBots_Sample.pwn" "$ROOT/omp/gamemodes/fakebots_sample.pwn"
for gm in fbtest fbscn fakebots_demo fakebots_sample; do
  (cd "$ROOT/omp" && LD_LIBRARY_PATH=qawno ./qawno/pawncc "gamemodes/$gm.pwn" "-ogamemodes/$gm.amx" -iqawno/include -d3 >/dev/null 2>&1) \
     || { echo "compile of $gm failed"; exit 1; }
done

# shipped configuration: the demo gamemode, no NPC slots, not announced to the master list
python3 - "$ROOT/omp/config.json" <<'PYEOF'
import json, sys
p = sys.argv[1]
c = json.load(open(p))
c["name"] = "FakeBots open.mp test server"
c["announce"] = False
c["artwork"]["enable"] = False
c["max_players"] = 120
c["max_bots"] = 0                       # zero NPC slots: the bots are ordinary players
c["rcon"]["enable"] = False
c["logging"]["use_timestamp"] = True
c["pawn"]["main_scripts"] = ["fakebots_demo 1"]
c["pawn"]["legacy_plugins"] = ["FakeBots"]
json.dump(c, open(p, "w"), indent=2)
PYEOF

cat > "$ROOT/omp/start.sh" <<'EOF'
#!/bin/sh
# Starts the open.mp test server (32-bit Linux). Keep stdin open: the server's console reads it.
cd "$(dirname "$0")" && exec ./omp-server
EOF
chmod +x "$ROOT/omp/start.sh"
echo "omp/ is ready:  cd omp && ./start.sh"
