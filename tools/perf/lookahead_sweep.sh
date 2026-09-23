#!/usr/bin/env bash
# Phase 0 UC/DSP experiment: apply the same delay to reads and writes.
# Golden-labelled tests encode the old timing and are excluded here.
#
#   GEARMULATOR_MM_FIRMWARE_BIN=... GEARMULATOR_MD_FIRMWARE_BIN=... \
#     tools/perf/lookahead_sweep.sh BUILD_DIR OUT_DIR [microseconds...]
set -uo pipefail

if (( $# < 2 )); then
	echo "usage: $0 BUILD_DIR OUT_DIR [microseconds...]" >&2
	exit 2
fi
build="$(cd "$1" && pwd)" || exit 2
mkdir -p "$2" || exit 2
out="$(cd "$2" && pwd)" || exit 2
shift 2
values=("$@")
(( ${#values[@]} )) || values=(0 1 2 3 4 5 7 10 20)

: "${GEARMULATOR_MM_FIRMWARE_BIN:?set GEARMULATOR_MM_FIRMWARE_BIN to a firmware file}"
: "${GEARMULATOR_MD_FIRMWARE_BIN:?set GEARMULATOR_MD_FIRMWARE_BIN to a firmware file}"
for firmware in "$GEARMULATOR_MM_FIRMWARE_BIN" "$GEARMULATOR_MD_FIRMWARE_BIN"; do
	if [[ ! -f "$firmware" ]]; then
		echo "firmware file not found: $firmware" >&2
		exit 2
	fi
done
export GEARMULATOR_MM_FIRMWARE_BIN GEARMULATOR_MD_FIRMWARE_BIN

: > "$out/summary.txt"
failed=0
for us in "${values[@]}"; do
	log="$out/lookahead-$us.log"
	if ( cd "$build" && GEARMULATOR_MDMM_LOOKAHEAD_US="$us" GEARMULATOR_MDMM_WRITEAHEAD_US="$us" \
		nice -n19 ctest -L FirmwareTest -LE '^Golden$' -j1 --timeout 900 \
		--no-tests=error --output-on-failure > "$log" 2>&1 ); then
		status=PASS
	else
		status=FAIL
		failed=1
	fi
	summary=$(grep -E '[0-9]+% tests passed' "$log" | tail -1 || true)
	printf '%s us: %s; %s; log: %s\n' "$us" "$status" "${summary:-no CTest summary}" "$log" | tee -a "$out/summary.txt"
done
exit "$failed"
