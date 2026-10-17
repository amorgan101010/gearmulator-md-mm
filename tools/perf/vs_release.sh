#!/usr/bin/env bash
# Builds three mdmmBench binaries and measures them the same way:
#   release   the upstream release (a worktree with mdmmBench.cpp dropped in), plain Release
#   main      this tree's plain Release build (build/)
#   main-pgo  this tree with GCC PGO (build-pgo/generate -> build-pgo/use)
#
#   tools/perf/vs_release.sh RELEASE_WORKTREE OUT_DIR
#
# Builds run one job at a time under a memory cap. Results: OUT_DIR/icount.txt (retired instructions per
# emulated second of playback, load independent) and OUT_DIR/ab.txt (pinned, alternating cycle A/B).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
release="$(cd "$1" && pwd)"; out="$(mkdir -p "$2" && cd "$2" && pwd)"
: "${GEARMULATOR_MM_FIRMWARE_BIN:?}"; : "${GEARMULATOR_MD_FIRMWARE_BIN:?}"
export GEARMULATOR_MM_FIRMWARE_BIN GEARMULATOR_MD_FIRMWARE_BIN

capped() {
	setsid systemd-run --user --scope -q -p MemoryMax=3G -p MemorySwapMax=2G nice -n19 ionice -c3 \
		env CMAKE_BUILD_PARALLEL_LEVEL=1 "$@"
}
bench=source/elektron/md/mdLibTest/mdmmBench
common=(-G Ninja -DCMAKE_BUILD_TYPE=Release -Dgearmulator_SYNTH_ELEKTRON=ON)

echo "== $(date +%H:%M) release build"
cmake -S "$release" -B "$release/build" "${common[@]}" > "$out/configure-release.log"
capped ninja -C "$release/build" -j1 mdmmBench > "$out/build-release.log" 2>&1
cp "$release/build/$bench" "$out/mdmmBench-release"

echo "== $(date +%H:%M) main build"
capped ninja -C "$here/build" -j1 mdmmBench > "$out/build-main.log" 2>&1
cp "$here/build/$bench" "$out/mdmmBench-main"

echo "== $(date +%H:%M) PGO instrumented build"
profile="$here/build-pgo/profile"
cmake -S "$here" -B "$here/build-pgo/generate" "${common[@]}" -DGEARMULATOR_GCC_PGO=generate \
	-DGEARMULATOR_GCC_PGO_DIR="$profile" > "$out/configure-generate.log"
capped ninja -C "$here/build-pgo/generate" -j1 mdmmBench > "$out/build-generate.log" 2>&1

echo "== $(date +%H:%M) collecting the profile (same workloads as tools/gcc-pgo.sh)"
rm -rf "$profile"
g="$here/build-pgo/generate/$bench"
MDMM_BENCH_PGO_PLAYBACK_ONLY=1 "$g" mm 8 512 machine:32 6 > /dev/null 2>&1 || true
MDMM_BENCH_PGO_PLAYBACK_ONLY=1 "$g" mm 6 256 machine:4 6 > /dev/null 2>&1 || true
MDMM_BENCH_PGO_PLAYBACK_ONLY=1 "$g" md 8 512 > /dev/null 2>&1 || true
echo "   $(find "$profile" -name '*.gcda' | wc -l) profile files"

echo "== $(date +%H:%M) PGO optimized build"
cmake -S "$here" -B "$here/build-pgo/use" "${common[@]}" -DGEARMULATOR_GCC_PGO=use \
	-DGEARMULATOR_GCC_PGO_DIR="$profile" > "$out/configure-use.log"
capped ninja -C "$here/build-pgo/use" -j1 mdmmBench > "$out/build-use.log" 2>&1
# The profile must actually apply: no missing-profile notes for the emulator sources.
grep -o "[^ ‘]*\.gcda’ profile count data file not found" "$out/build-use.log" | sort -u > "$out/missing-profile.txt" || true
echo "   $(wc -l < "$out/missing-profile.txt") sources without a profile"
cp "$here/build-pgo/use/$bench" "$out/mdmmBench-main-pgo"

echo "== $(date +%H:%M) instruction counts (15 s of playback each)"
{
	for v in release main main-pgo; do
		echo "$v mm $("$here/tools/perf/icount.sh" "$out/mdmmBench-$v" 15 mm 512 machine:32 6)"
		echo "$v md $("$here/tools/perf/icount.sh" "$out/mdmmBench-$v" 15 md 512)"
	done
} | tee "$out/icount.txt"

echo "== $(date +%H:%M) pinned cycle A/B (6 rounds, 15 s)"
"$here/tools/perf/ab_realtime.sh" 6 15 release="$out/mdmmBench-release" main="$out/mdmmBench-main" \
	main-pgo="$out/mdmmBench-main-pgo" > "$out/ab.txt" 2> "$out/ab-runs.txt"
cat "$out/ab.txt"
echo "== $(date +%H:%M) done"
