#!/usr/bin/env bash
# Build the MD/MM standalones with GCC profile-guided optimization.
#
# Three steps: build an instrumented mdmmBench, run it over a few representative workloads to
# collect a profile, then build the real targets against that profile. The emulator is
# deterministic, so this only changes code layout: mdmmBench prints an output hash, and it is the
# same before and after.
#
# Needs your own firmware images:
#   GEARMULATOR_MM_FIRMWARE_BIN=/path/mm.bin GEARMULATOR_MD_FIRMWARE_BIN=/path/md.bin \
#     tools/gcc-pgo.sh [target...]
#
# Measured on a Ryzen 5 5600G, GCC 15: Machinedrum about 3% faster, Monomachine about 1%.

set -euo pipefail

source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
profile_dir="${GEARMULATOR_PGO_DIR:-${source_dir}/build-pgo/profile}"
generate_dir="${source_dir}/build-pgo/generate"
use_dir="${source_dir}/build-pgo/use"
targets=("$@")
[[ ${#targets[@]} -eq 0 ]] && targets=(mdJucePlugin_Standalone mmJucePlugin_Standalone)

if [[ -z "${GEARMULATOR_MM_FIRMWARE_BIN:-}" && -z "${GEARMULATOR_MD_FIRMWARE_BIN:-}" ]]; then
	echo "set GEARMULATOR_MM_FIRMWARE_BIN and/or GEARMULATOR_MD_FIRMWARE_BIN first" >&2
	exit 2
fi

common=(-G Ninja -DCMAKE_BUILD_TYPE=Release -Dgearmulator_SYNTH_ELEKTRON=ON
	-DGEARMULATOR_GCC_PGO_DIR="${profile_dir}")

echo "== 1/3 instrumented build"
cmake -S "${source_dir}" -B "${generate_dir}" "${common[@]}" -DGEARMULATOR_GCC_PGO=generate
cmake --build "${generate_dir}" --target mdmmBench

bench="${generate_dir}/source/elektron/md/mdLibTest/mdmmBench"

echo "== 2/3 collecting the profile"
rm -rf "${profile_dir}"
if [[ -n "${GEARMULATOR_MM_FIRMWARE_BIN:-}" ]]; then
	"${bench}" mm 8 512 machine:32 6 >/dev/null 2>&1 || true	# a heavy patch, six voices
	"${bench}" mm 6 256 machine:4 6  >/dev/null 2>&1 || true	# a light patch, smaller blocks
fi
if [[ -n "${GEARMULATOR_MD_FIRMWARE_BIN:-}" ]]; then
	"${bench}" md 8 512 >/dev/null 2>&1 || true
fi
echo "   profile: $(find "${profile_dir}" -name '*.gcda' 2>/dev/null | wc -l) files"

echo "== 3/3 optimized build"
cmake -S "${source_dir}" -B "${use_dir}" "${common[@]}" -DGEARMULATOR_GCC_PGO=use \
	-Dgearmulator_BUILD_JUCEPLUGIN=ON -Dgearmulator_BUILD_JUCEPLUGIN_Standalone=ON
cmake --build "${use_dir}" --target "${targets[@]}"

echo
echo "done. Note that every build tree writes its output to ${source_dir}/bin, so whichever"
echo "built last is what you install."
