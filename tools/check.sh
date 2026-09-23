#!/usr/bin/env bash
# The gate to run before pushing or merging: builds what the checks need and runs them.
#
#   tools/check.sh [--full] [--no-firmware] [build-dir]      (build-dir defaults to ./build)
#
# Default:
#   - every test labelled UnitTest (fast, no firmware)
#   - the DSP emulator's own tests (dsp56kTestRunner and friends); run these after ANY change to
#     source/dsp56300, including merges: the MD/MM firmware never exercises some DSP paths
#   - the golden output test and its sensitivity check (need the firmware)
# --full adds the host block-size test and the plugin-level firmware tests.
# --no-firmware runs only what needs no ROMs, as CI does.
#
# Firmware comes from GEARMULATOR_MM_FIRMWARE_BIN and GEARMULATOR_MD_FIRMWARE_BIN, or from
# GEARMULATOR_ROMS (a folder holding elektron_sfx6-60_os1.32b.bin and elektron_sps1-1uw_os1.63.bin).
# Without --no-firmware, missing firmware is an error, not a silent skip.
#
# Builds with one job under a memory/CPU cap; tests are low priority and heavy checks are serialized.
# --full keeps three concurrent instances per model for the block-size check, running MD and MM separately.
# Exits non-zero if anything fails.
set -uo pipefail

full=0; firmware=1; build=""
for arg in "$@"; do
	case "$arg" in
		--full) full=1 ;;
		--no-firmware) firmware=0 ;;
		-h|--help) sed -n 2,20p "$0"; exit 0 ;;
		*) build="$arg" ;;
	esac
done
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(cd "${build:-$root/build}" 2>/dev/null && pwd)" || { echo "build directory not found (configure it first)" >&2; exit 2; }
log="$build/check-logs"; mkdir -p "$log"

if (( firmware )); then
	if [[ -n "${GEARMULATOR_ROMS:-}" ]]; then
		export GEARMULATOR_MM_FIRMWARE_BIN="${GEARMULATOR_MM_FIRMWARE_BIN:-$GEARMULATOR_ROMS/elektron_sfx6-60_os1.32b.bin}"
		export GEARMULATOR_MD_FIRMWARE_BIN="${GEARMULATOR_MD_FIRMWARE_BIN:-$GEARMULATOR_ROMS/elektron_sps1-1uw_os1.63.bin}"
	fi
	for v in GEARMULATOR_MM_FIRMWARE_BIN GEARMULATOR_MD_FIRMWARE_BIN; do
		if [[ -z "${!v:-}" || ! -f "${!v}" ]]; then
			echo "$v is not set to a firmware file. Set it (or GEARMULATOR_ROMS), or pass --no-firmware." >&2
			exit 2
		fi
	done
fi

declare -a names results
record() { names+=("$1"); results+=("$2"); }

# --- which test programs are needed: map each selected ctest to the target that builds it
selection='UnitTest|dsp56'
(( firmware )) && selection="$selection|mdGoldenOutputTest|mdGoldenSensitivityTest"
(( firmware && full )) && selection="$selection|mdBlockSizeTest|mdAutomationFirmwareTest|mdProgramChangeFirmwareTest|mdAutomationRobustnessTest|mdAutomationSoakTest"
targets=$(ctest --test-dir "$build" --show-only=json-v1 2>/dev/null | ROOT="$root" SELECTION="$selection" python3 -c '
import glob, json, os, re, sys
d = json.load(sys.stdin)
sel = re.compile(os.environ["SELECTION"])
by_test = {}
for f in glob.glob(os.environ["ROOT"] + "/source/**/CMakeLists.txt", recursive=True):
	for name, command in re.findall(r"add_test\(\s*NAME\s+(\S+)\s+COMMAND\s+([^\s\)]+)", open(f, errors="ignore").read()):
		by_test[name] = command
wanted = set()
for t in d["tests"]:
	labels = [v for p in t.get("properties", []) if p["name"] == "LABELS" for v in p["value"]]
	if not (sel.search(t["name"]) or any(sel.search(l) for l in labels)):
		continue
	command = by_test.get(t["name"])
	if command and not command.startswith("$") and "/" not in command:
		wanted.add(command)
if "dsp56kDisassembleZeroWords" in {t["name"] for t in d["tests"]}:
	wanted.add("dsp56kDisassemble")	# the test runs it but does not depend on it
print(" ".join(sorted(wanted)))
')
echo "== building: $targets"
if setsid systemd-run --user --scope -q -p MemoryHigh=1500M -p MemoryMax=3G -p MemorySwapMax=256M \
	-p CPUQuota=75% -p IOWeight=10 nice -n19 ionice -c3 \
	env CMAKE_BUILD_PARALLEL_LEVEL=1 ninja -C "$build" -j1 $targets > "$log/build.log" 2>&1; then
	record "build" PASS
else
	record "build" "FAIL (see $log/build.log)"
	grep -m5 -E "error|FAILED" "$log/build.log" >&2
fi

run_ctest() {	# name, ctest args...
	local name=$1; shift
	local memory=3G
	[[ $name == "block sizes "* ]] && memory=4G
	if systemd-run --user --scope -q -p MemoryMax="$memory" -p MemorySwapMax=256M -p IOWeight=10 \
		nice -n19 ionice -c3 ctest --test-dir "$build" --output-on-failure --no-tests=error "$@" > "$log/$name.log" 2>&1; then
		record "$name" "PASS ($(grep -oE 'out of [0-9]+' "$log/$name.log" | tail -1 | grep -oE '[0-9]+') tests)"
	else
		record "$name" "FAIL (see $log/$name.log)"
		grep -E "\*\*\*(Failed|Not Run|Timeout|Exception)" "$log/$name.log" >&2
	fi
}

echo "== unit tests"
run_ctest "unit tests" -L UnitTest -j1
echo "== DSP emulator tests"
run_ctest "DSP emulator tests" -R dsp56

if (( firmware )); then
	# Each golden process runs both models and uses about 2 GB. Serializing the two checks avoids
	# doubling that footprint on a desktop which is also running the user's applications.
	echo "== golden output and sensitivity (sequential)"
	run_ctest "golden output" -R '^mdGoldenOutputTest$'
	run_ctest "golden sensitivity" -R '^mdGoldenSensitivityTest$'
	if (( full )); then
		echo "== block sizes"
		# Preserve simultaneous 512/128/32-frame instances without running both models (six
		# emulated units) together. Each invocation still checks all scenarios for its model.
		GEARMULATOR_MM_FIRMWARE_BIN= run_ctest "block sizes MD" -R '^mdBlockSizeTest$'
		GEARMULATOR_MD_FIRMWARE_BIN= run_ctest "block sizes MM" -R '^mdBlockSizeTest$'
		# The plugin-level tests find firmware like the plug-in, next to the program, and ignore
		# the environment. Link it there (the build directory is not tracked) and require it.
		plugin_dir="$build/source/elektron/md/mdJucePlugin"
		ln -sf "$GEARMULATOR_MM_FIRMWARE_BIN" "$plugin_dir/$(basename "$GEARMULATOR_MM_FIRMWARE_BIN")"
		ln -sf "$GEARMULATOR_MD_FIRMWARE_BIN" "$plugin_dir/$(basename "$GEARMULATOR_MD_FIRMWARE_BIN")"
		echo "== plugin firmware tests"
		MD_AUTOMATION_REQUIRE_FIRMWARE=1 run_ctest "plugin firmware tests" \
			-R '^(mdAutomationFirmwareTest|mdProgramChangeFirmwareTest|mdAutomationRobustnessTest|mdAutomationSoakTest)$'
	fi
fi

echo
failed=0
for i in "${!names[@]}"; do
	printf '  %-24s %s\n' "${names[$i]}" "${results[$i]}"
	[[ ${results[$i]} == PASS* ]] || failed=1
done
if (( failed )); then echo "check: FAILED"; exit 1; fi
echo "check: all passed"
