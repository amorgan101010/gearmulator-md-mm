#!/usr/bin/env bash
# Threading plan, phase 0 (doc/mdmm-threading-plan.md): does the firmware tolerate a DSP running behind the UC?
# Runs the behavioural firmware tests (not the golden output hashes, which any timing change alters) with
# GEARMULATOR_MDMM_LOOKAHEAD_US set to each value, and prints a pass/fail table.
#
#   GEARMULATOR_ROMS=... tools/perf/lookahead_sweep.sh BUILD_DIR OUT_DIR [microseconds...]
set -uo pipefail
build="$(cd "$1" && pwd)"; out="$(mkdir -p "$2" && cd "$2" && pwd)"; shift 2
values=("$@"); [[ ${#values[@]} -eq 0 ]] && values=(0 0.5 1 2 5 10)
: "${GEARMULATOR_MM_FIRMWARE_BIN:?}"; : "${GEARMULATOR_MD_FIRMWARE_BIN:?}"
export GEARMULATOR_MM_FIRMWARE_BIN GEARMULATOR_MD_FIRMWARE_BIN

for us in "${values[@]}"; do
	log="$out/lookahead-$us.log"
	echo "== $(date +%H:%M) lookahead $us us"
	( cd "$build" && GEARMULATOR_MDMM_LOOKAHEAD_US="$us" nice -n19 ctest -L FirmwareTest -E "Golden" -j3 \
		--timeout 900 --output-on-failure > "$log" 2>&1 )
	summary=$(grep -E "tests passed|tests failed" "$log" | tail -1)
	failed=$(grep -E "^\s+[0-9]+ - " "$log" | sed 's/^\s*[0-9]* - //' | tr '\n' ' ')
	echo "   $summary ${failed:+-- failed: $failed}"
done | tee "$out/summary.txt"
