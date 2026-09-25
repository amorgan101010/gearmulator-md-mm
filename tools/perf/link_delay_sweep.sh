#!/usr/bin/env bash
# Threading experiment: delay every DSP<->DSP link frame by N link slots (96 DSP cycles each).
# Golden-labelled tests encode the old timing and are excluded here. GEARMULATOR_MDMM_LINK_DELAY_CYCLES
# overrides the slot count with an exact cycle delay for finer diagnosis.
#
#   GEARMULATOR_MM_FIRMWARE_BIN=... GEARMULATOR_MD_FIRMWARE_BIN=... \
#     tools/perf/link_delay_sweep.sh BUILD_DIR OUT_DIR [slots...]
set -uo pipefail

if (( $# < 2 )); then
	echo "usage: $0 BUILD_DIR OUT_DIR [slots...]" >&2
	exit 2
fi
build="$(cd "$1" && pwd)" || exit 2
mkdir -p "$2" || exit 2
out="$(cd "$2" && pwd)" || exit 2
shift 2
values=("$@")
(( ${#values[@]} )) || values=(0 1 2 4 8)

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
for slots in "${values[@]}"; do
	log="$out/link-delay-$slots.log"
	# mdSysexLifecycleTest is registered without its four fixture arguments, so it can only
	# skip (or fail under MD_AUTOMATION_REQUIRE_FIRMWARE); it says nothing about the delay.
	if ( cd "$build" && GEARMULATOR_MDMM_LINK_DELAY_SLOTS="$slots" \
		nice -n19 ctest -L FirmwareTest -LE '^Golden$' -E '^mdSysexLifecycleTest$' -j1 --timeout 900 \
		--no-tests=error --output-on-failure > "$log" 2>&1 ); then
		status=PASS
	else
		status=FAIL
		failed=1
	fi
	summary=$(grep -E '[0-9]+% tests passed' "$log" | tail -1 || true)
	failures=$(grep -E '^\s+[0-9]+ - .*\((Failed|Timeout|SEGFAULT|Subprocess aborted)\)' "$log" \
		| sed -E 's/^\s+[0-9]+ - //' | paste -sd ';' - || true)
	printf '%s slots: %s; %s; %s; log: %s\n' "$slots" "$status" "${summary:-no CTest summary}" \
		"${failures:-no failures}" "$log" | tee -a "$out/summary.txt"
done
exit "$failed"
