#!/usr/bin/env bash
# Steady-state retired-instruction count of mdmmBench, with firmware boot and setup excluded.
#
# The bench boots the firmware (~20 emulated seconds) before its measured window, and boot is
# ~83% of a 5 s run's instructions. Emulation is deterministic, so running the same workload with
# 0 and N measured seconds and subtracting gives the exact cost of N seconds of playback.
# Instruction counts do not depend on machine load, so this is safe to run while the PC is in use.
#
#   tools/perf/icount.sh BENCH [seconds] [bench args after "seconds"...]
#   tools/perf/icount.sh build/source/elektron/md/mdLibTest/mdmmBench 15 mm 512 machine:32 6
#
# Args after the duration are the bench's own: model, block, mode, voices. Prints instructions
# per emulated second of playback and the output hash of the N-second run.
set -euo pipefail
bench=$1; secs=${2:-15}; model=$3; shift 3
: "${GEARMULATOR_MM_FIRMWARE_BIN:?set GEARMULATOR_MM_FIRMWARE_BIN and GEARMULATOR_MD_FIRMWARE_BIN}"
: "${GEARMULATOR_MD_FIRMWARE_BIN:?set GEARMULATOR_MM_FIRMWARE_BIN and GEARMULATOR_MD_FIRMWARE_BIN}"; export GEARMULATOR_MM_FIRMWARE_BIN GEARMULATOR_MD_FIRMWARE_BIN
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

# Both runs in parallel: instruction counts are unaffected by contention.
perf stat -e instructions:u -x, -o "$tmp/p0" "$bench" "$model" 0 "$@" > /dev/null 2>&1 &
perf stat -e instructions:u -x, -o "$tmp/pN" "$bench" "$model" "$secs" "$@" > "$tmp/outN" 2>&1
wait
zero=$(grep instructions "$tmp/p0" | cut -d, -f1)
full=$(grep instructions "$tmp/pN" | cut -d, -f1)
hash=$(grep -o "hash=[0-9a-f]*" "$tmp/outN" || echo "hash=?")
python3 -c "z,f,s=$zero,$full,$secs; print(f'playback_instructions_per_s={(f-z)/s:.6e} boot={z:.4e} total={f:.4e} $hash')"
