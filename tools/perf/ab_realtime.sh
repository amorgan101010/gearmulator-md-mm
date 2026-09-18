#!/usr/bin/env bash
# Alternating wall-clock A/B of mdmmBench binaries, playback window only (the bench's realtimeX
# excludes boot). Rounds alternate the binary order so drift in machine load hits every variant alike.
#
#   tools/perf/ab_realtime.sh ROUNDS SECONDS name=path [name=path...]
#
# Prints one line per run (round, variant, workload, realtimeX, p99, Mcycles/s, hash) and per-variant medians.
# Mcycles/s (user-mode CPU cycles per emulated second, from the bench) is the robust metric: it does not
# count time the bench spends descheduled. realtimeX and p99 are wall-clock and move with machine load.
set -euo pipefail
rounds=$1; secs=$2; shift 2
: "${GEARMULATOR_MM_FIRMWARE_BIN:?set GEARMULATOR_MM_FIRMWARE_BIN and GEARMULATOR_MD_FIRMWARE_BIN}"
: "${GEARMULATOR_MD_FIRMWARE_BIN:?set GEARMULATOR_MM_FIRMWARE_BIN and GEARMULATOR_MD_FIRMWARE_BIN}"; export GEARMULATOR_MM_FIRMWARE_BIN GEARMULATOR_MD_FIRMWARE_BIN
core=${AB_CORE:-3}
variants=("$@")
workloads=("mm $secs 512 machine:32 6" "md $secs 512")
for ((r = 0; r < rounds; ++r)); do
	order=("${variants[@]}")
	((r % 2)) && order=($(printf '%s\n' "${variants[@]}" | tac))
	for w in "${workloads[@]}"; do
		for v in "${order[@]}"; do
			name=${v%%=*}; bin=${v#*=}
			line=$(taskset -c "$core" "$bin" $w 2>/dev/null | grep -E "^M[DM] block")
			rt=$(grep -o "realtimeX=[0-9.]*" <<< "$line" | cut -d= -f2)
			p99=$(grep -o "p99=[0-9.]*" <<< "$line" | cut -d= -f2)
			hash=$(grep -o "hash=[0-9a-f]*" <<< "$line" | cut -d= -f2)
			mc=$(grep -o "Mcycles/s=[0-9.]*" <<< "$line" | cut -d= -f2)
			echo "run r=$r v=$name w=${w%% *} realtimeX=$rt p99=$p99 Mcycles=${mc:-0} hash=$hash"
		done
	done
done | tee /dev/stderr | python3 -c '
import sys, statistics, collections
d = collections.defaultdict(list); h = collections.defaultdict(set)
for l in sys.stdin:
    f = dict(kv.split("=") for kv in l.split()[1:])
    d[(f["w"], f["v"])].append((float(f["realtimeX"]), float(f["p99"]), float(f.get("Mcycles", 0)))); h[f["w"]].add(f["hash"])
for w in sorted({k[0] for k in d}):
    print(f"{w}: hashes {sorted(h[w])}")
    for (ww, v), xs in sorted(d.items()):
        if ww == w:
            print(f"  {v:10s} realtimeX median={statistics.median(x[0] for x in xs):.3f}  p99 median={statistics.median(x[1] for x in xs):.3f}  Mcycles/s median={statistics.median(x[2] for x in xs):.1f}  n={len(xs)}")
'
