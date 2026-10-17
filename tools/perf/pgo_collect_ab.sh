#!/usr/bin/env bash
# Collect two GCC profiles from the same workloads: whole run (boot included) and playback only.
set -uo pipefail
# Usage: pgo_collect_ab.sh GENERATE_BUILD_DIR  (profiles land in GENERATE_BUILD_DIR/profiles/{whole,playback})
here="$(cd "$1" && pwd)"
: "${GEARMULATOR_MM_FIRMWARE_BIN:?set GEARMULATOR_MM_FIRMWARE_BIN and GEARMULATOR_MD_FIRMWARE_BIN}"
: "${GEARMULATOR_MD_FIRMWARE_BIN:?set GEARMULATOR_MM_FIRMWARE_BIN and GEARMULATOR_MD_FIRMWARE_BIN}"; export GEARMULATOR_MM_FIRMWARE_BIN GEARMULATOR_MD_FIRMWARE_BIN
B="$here/source/elektron/md/mdLibTest/mdmmBench"
live=${GEARMULATOR_PGO_LIVE_DIR:?the -fprofile-generate directory the generate build writes to}

workloads() {
	"$B" mm 10 512 machine:32 6
	"$B" mm 8 256 machine:4 6
	"$B" mm 8 128 sine 6
	"$B" md 10 512
	"$B" md 8 128
}

for kind in whole playback; do
	rm -rf "$live"
	echo "== $kind"
	if [[ $kind == playback ]]; then
		MDMM_BENCH_PGO_PLAYBACK_ONLY=1 workloads 2>&1 | grep -E "hash="
	else
		workloads 2>&1 | grep -E "hash="
	fi
	rm -rf "$here/profiles/$kind"
	mkdir -p "$here/profiles"
	mv "$live" "$here/profiles/$kind"
	echo "   $(find "$here/profiles/$kind" -name '*.gcda' | wc -l) gcda files"
done
