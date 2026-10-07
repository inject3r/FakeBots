#!/usr/bin/env bash
# =============================================================================
#  Runs the whole live-server matrix: a real server is started for every line,
#  the bots join over the network, and the result is checked from inside the
#  gamemode AND from outside (SA-MP query = what a server browser sees).
#
#      ./run_all_tests.sh              # omp + samp, full matrix (about 25 minutes)
#      ./run_all_tests.sh omp          # one server only
#      ./run_all_tests.sh samp quick   # shorter matrix, no 300-bot run
#      ./run_all_tests.sh omp full 'overfill|churn'   # only the lines whose label matches (appends to SUMMARY.txt)
#
#  Needs: python3, the 32-bit runtime libs (libc6-i386 lib32stdc++6), omp/ (tools/setup_test_servers.sh)
#  and for samp: samp/get_samp_server.sh run once. Results: tests/results/*.json|log, SUMMARY.txt
# =============================================================================
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"
TARGET="${1:-all}"
MODE="${2:-full}"
FILTER="${3:-}"
PLUGIN="${PLUGIN:-$ROOT/dist/FakeBots.so}"
PROBE="${PROBE:-$ROOT/tests/bin/rakprobe}"
COMPILER="${COMPILER:-$ROOT/omp/qawno/pawncc}"
SUMMARY="$ROOT/tests/results/SUMMARY.txt"
mkdir -p tests/results
[ -n "$FILTER" ] || : > "$SUMMARY"

[ -f "$PLUGIN" ] || { echo "plugin not found: $PLUGIN"; exit 2; }
python3 tools/source_audit.py || exit 3

failures=0
run_one() {   # kind label args...
  local kind="$1" label="$2"; shift 2
  if [ -n "$FILTER" ] && ! echo "$label" | grep -E -q "$FILTER"; then return; fi
  local dir="$ROOT/$kind"
  local exe="omp-server"; [ "$kind" = samp ] && exe="samp03svr"
  if [ ! -x "$dir/$exe" ]; then
    printf '%-6s %-34s SKIPPED (%s/%s missing)\n' "$kind" "$label" "$kind" "$exe" | tee -a "$SUMMARY"
    return
  fi
  local started=$SECONDS
  if python3 tests/run_server_test.py --kind "$kind" --server-dir "$dir" --plugin "$PLUGIN" \
        --probe "$PROBE" --compiler "$COMPILER" "$@" > "tests/results/last-$kind.txt" 2>&1; then
    printf '%-6s %-34s PASS  (%ds)\n' "$kind" "$label" "$((SECONDS-started))" | tee -a "$SUMMARY"
  else
    printf '%-6s %-34s FAIL  (%ds)   see tests/results/last-%s.txt\n' "$kind" "$label" "$((SECONDS-started))" "$kind" | tee -a "$SUMMARY"
    cp "tests/results/last-$kind.txt" "tests/results/FAILED-$kind-${label// /_}.txt"
    failures=$((failures+1))
  fi
}

matrix() {
  local k="$1"
  run_one "$k" "basic API (70+ checks), 20 bots"     --scenario basic    --bots 20  --hold 6
  run_one "$k" "30 bots join"                        --scenario join     --bots 30  --hold 6
  run_one "$k" "15 bots, password protected"         --scenario join     --bots 15  --hold 5 --password s3cret_pw
  run_one "$k" "100 bots join"                       --scenario join     --bots 100 --hold 8
  [ "$MODE" = quick ] || run_one "$k" "300 bots join"  --scenario join   --bots 300 --hold 10
  run_one "$k" "server full, real player gets in"    --scenario overfill --bots 20
  run_one "$k" "nickname rules"                      --scenario badname  --bots 2
  run_one "$k" "walking headings (8 directions)"     --scenario facing   --bots 1
  run_one "$k" "create/destroy x4, leak check"       --scenario churn    --bots 40  --rounds 4
  run_one "$k" "demo gamemode, 25 bots"              --scenario demo     --bots 25  --hold 8
  run_one "$k" "sample gamemode boots"               --scenario sample   --bots 3
}

case "$TARGET" in
  omp)  matrix omp ;;
  samp) matrix samp ;;
  all)  matrix omp; matrix samp ;;
  *) echo "usage: $0 [omp|samp|all] [full|quick]"; exit 2 ;;
esac

echo
echo "================ summary ================"
cat "$SUMMARY"
[ "$failures" -eq 0 ] && echo "ALL PASSED" || echo "$failures run(s) FAILED"
exit "$failures"
