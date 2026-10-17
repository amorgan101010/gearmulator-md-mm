#!/usr/bin/env bash
# Build one PGO-optimized mdmmBench per profile collected by pgo_collect_ab.sh.
#
#   tools/perf/pgo_build_ab.sh GENERATE_DIR USE_DIR kind...     e.g.  build-pgo build-pgouse whole playback
#
# Profile files are named after the generate build's absolute object paths, so they are renamed to
# the use build's paths before building. -Wmissing-profile stays ON: the build log must show no
# "profile count data file not found" for the emulator sources, otherwise the profile was not applied.
# Result: USE_DIR/mdmmBench-<kind> (the script refuses to reuse a stale binary), plus USE_DIR/missing-<kind>.txt listing sources that had no profile.
set -euo pipefail
gen="$(cd "$1" && pwd)"; use="$(cd "$2" && pwd)"; shift 2
# One profile directory per kind: the path is part of the flags, so switching kinds changes the flags and
# forces a full recompile. Ninja does not track .gcda files, so reusing one directory rebuilds nothing.
for kind in "$@"; do
	active="$use/profile-$kind"
	flags="-fprofile-use=$active -fprofile-correction -Wmissing-profile -Wno-error=coverage-mismatch"
	rm -rf "$active"; mkdir -p "$active"
	genTag="$(echo "$gen" | tr / '#')#"; useTag="$(echo "$use" | tr / '#')#"
	for f in "$gen/profiles/$kind"/*.gcda; do
		b="$(basename "$f")"; cp "$f" "$active/${b/"$genTag"/"$useTag"}"
	done
	cmake -S "$use/.." -B "$use" -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" > /dev/null
	setsid systemd-run --user --scope -q -p MemoryMax=3G -p MemorySwapMax=2G nice -n19 ionice -c3 \
		env CMAKE_BUILD_PARALLEL_LEVEL=1 ninja -C "$use" -j1 mdmmBench > "$use/build-$kind.log" 2>&1
	grep -o "[^ ‘]*\.gcda’ profile count data file not found" "$use/build-$kind.log" | sort -u > "$use/missing-$kind.txt" || true
	if ! grep -q "Building CXX object source/elektron/md/mdLib/" "$use/build-$kind.log"; then
		echo "$kind: emulator sources were not recompiled, refusing to label a stale binary" >&2; exit 1
	fi
	cp "$use/source/elektron/md/mdLibTest/mdmmBench" "$use/mdmmBench-$kind"
	echo "$kind: built, $(wc -l < "$use/missing-$kind.txt") sources without profile, $(ls "$active" | wc -l) profile files"
done
