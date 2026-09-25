# Brief for the macOS session: Monomachine dropped notes

Context: `doc/mm-track6-drop-investigation.md` on this branch has the full investigation. Read it first.
In short, the Monomachine drops notes on track 6 (sometimes 5) because a trigger frame can reach the
mixer DSP while that voice is being processed, and the firmware then clears the trigger before it is
seen. On Linux this reproduces deterministically, and an opt-in guard (`GEARMULATOR_MM_TRIGGER_GUARD=1`)
stops it. The macOS build uses the ARM64 JIT backend and dispatcher, so the Linux results need checking
there.

## Setup

```sh
git clone git@github.com:amorgan101010/gearmulator-md-mm.git
cd gearmulator-md-mm
git checkout investigate/mm-track6-drops
git submodule update --init --recursive
```

The dsp56300 submodule points at `amorgan101010/dsp56300-md-mm`, branch `investigate/mm-track6-drops`.

Build the MD/MM libraries and tests. The JUCE plugin is not needed for this: configure with
`-Dgearmulator_BUILD_JUCEPLUGIN=OFF -Dgearmulator_BUILD_JUCEPLUGIN_CLAP=OFF`, the other synths off and
`-DBUILD_TESTING=ON`, then build `mdmmBench`, `mmPatternRepro` and `mmAudioFirmwareTest`.

Inputs copied over from the Linux machine: the two firmware images (`elektron_sfx6-60_os1.32b.bin` for
the Monomachine, `elektron_sps1-1uw_os1.63.bin` for the Machinedrum) and `device-state.bin`, the user's
saved Monomachine state. If only the settings snapshot is available, extract the state with
`tools/mmdrop/extract_device_state.py "<Gearmulator MM.settings>" device-state.bin`.

```sh
export GEARMULATOR_MM_FIRMWARE_BIN=/path/to/elektron_sfx6-60_os1.32b.bin
export GEARMULATOR_MD_FIRMWARE_BIN=/path/to/elektron_sps1-1uw_os1.63.bin
```

## Task 1: default output must match Linux

With no environment variables set, this branch reproduces upstream bit for bit on Linux:

```sh
mdmmBench md 10 512 pattern:C04   # expect hash=b8e0c7d9d71809bc
mdmmBench mm 10 512 pattern:B15   # expect hash=ed4758a81d23aedd
```

Report both hashes. A mismatch means the ARM64 build behaves differently, which matters for everything
below.

## Task 2: reproduce the drop, then check the guard

Pattern E12 is slot 75. The user's MIDI base channel is 5, so track n is channel index 3+n (tracks 1-5 are indexes 4-8). The
repro silences tracks 1-5 through CC7 on their own channels (never the auto channel), adds random
controller traffic on them, and counts track 6's kicks:

```sh
for s in $(seq 1 16); do
  MM_REPRO_LEVEL0=4,5,6,7,8 MM_REPRO_BLOCK=512 MM_REPRO_FUZZ_CH=4,5,6,7,8 MM_REPRO_FUZZ=$s \
    mmPatternRepro device-state.bin out-$s.f32 30 75
  python3 tools/mmdrop/kicks.py out-$s.f32 4000 | sed -n 2,4p
done
```

On Linux, seeds 4 and 8 each drop one kick (seed 4 at 22.37 s); the other seeds drop none. Expect about
46 kicks per 30 s at about 91 BPM. Then repeat with `GEARMULATOR_MM_TRIGGER_GUARD=1`; on Linux that gives
zero drops across all 16 seeds.

Report the per-seed missing counts with and without the guard. If macOS drops different beats, include
the gap times that `kicks.py` prints.

## Task 3 (optional): the real fix

The guard is a workaround. The open question is what keeps the controller's frames clear of the DSP's
window on real hardware. This is static reverse engineering of the Monomachine ColdFire code and does
not need a build. Entry points (ColdFire addresses, OS 1.32b):

- `0x24829e`, `0x243926`, `0x24395e`, `0x248214`: loops that re-read the host command register (CVR)
  until the DSP accepts the previous command.
- `0x2482ba`: a loop that waits for a host-port status bit.
- The code that sends the per-voice frames (host commands 0x10/0x12/0x14, then 52 data words each) and
  the 1-word command 0x0c, which reads DSP address `x:$701`.

Find what the controller waits for before it starts a frame burst, i.e. what event on real hardware ties
the burst to the DSP's 16-sample processing cycle. Report addresses and reasoning.

## Results from the macOS session (2026-09-25)

Apple Silicon (arm64 slice of the universal build), Release, JUCE plugin off, built from this branch at
`97f3f466` with dsp56300 at `6a63e831`. No source changes were needed to build.

### Task 1: default output matches Linux

```
mdmmBench md 10 512 pattern:C04   hash=b8e0c7d9d71809bc   (match)
mdmmBench mm 10 512 pattern:B15   hash=ed4758a81d23aedd   (match)
```

### Task 2: drops differ from Linux; the guard still removes all of them

Missing track 6 kicks per 30 s run (46 expected), E12, the command line above:

| Seed | macOS, no guard | Linux, no guard | macOS, `GEARMULATOR_MM_TRIGGER_GUARD=1` |
|---|---|---|---|
| 4 | 1 (gap at 21.765 s) | 1 (22.37 s) | 0 |
| 8 | 1 (gap at 21.105 s) | 1 | 0 |
| 9 | 2 (gaps at 21.765 s, 26.390 s) | 0 | 0 |
| 14 | 1 (gap at 23.085 s) | 0 | 0 |
| other 12 | 0 | 0 | 0 |

- `kicks.py` prints the start of each gap, so the missing kick is one beat (660 ms) later: seed 4 loses the
  kick at about 22.43 s, which agrees with Linux's 22.37 s.
- macOS is deterministic too: rerunning seeds 4, 9 and 14 gave byte-identical `.f32` output.
- So the default path is bit-exact across platforms, but the fuzzed repro is not: macOS drops on two
  extra seeds (5 drops over 4 seeds, against 2 over 2 on Linux). Something platform-dependent shifts where
  host-port frames land relative to the DSP's processing window in this path, probably the arm64 JIT's
  cycle accounting or one of the opt-in timing hooks. Not investigated further yet.

## Notes

- Do not play test audio through speakers without asking the user.
- Keep MD and MM firmware images apart: both are exactly 8 MB, and a ROM loader cannot tell them apart by
  size.
