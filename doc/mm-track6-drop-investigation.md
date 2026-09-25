# Monomachine dropped notes on tracks 5/6: investigation notes

Status: reproduced on unmodified code, not yet fixed. Everything below is diagnostic tooling on the
`investigate/mm-track6-drops` branch. Nothing here changes default behaviour: every hook is opt-in
through an environment variable.

## Symptom

Users report that the Monomachine drops notes on track 6, and sometimes track 5. One user's pattern E12
(BBOX kick on track 6, a trig every quarter note at ~91 BPM) drops kicks during ordinary playback.

## Reproduction

`mmPatternRepro` restores a saved device state, selects a pattern, taps PLAY, and writes the stereo
output as raw float32. `tools/mmdrop/kicks.py` counts kick onsets and reports gaps.

The device state is the plugin's `MIDI` chunk payload without the two-byte plugin prefix; decode it
from the standalone's `filterState` with `tools/mmdrop/extract_device_state.py`.

```sh
export GEARMULATOR_MM_FIRMWARE_BIN=/path/to/elektron_sfx6-60_os1.32b.bin
# Isolate track 6: zero CC7 on the five other track channels only (base channel 5 here -> indexes 4..8).
# Never zero the auto channel: it controls the selected track.
MM_REPRO_LEVEL0=4,5,6,7,8 MM_REPRO_BLOCK=512 mmPatternRepro device-state.bin out.f32 300 75
python3 tools/mmdrop/kicks.py out.f32 4000
```

Results with the user's state (saved 2026-09-23):

- 300 s of plain playback: 3 of 457 track-6 kicks missing.
- With random CC traffic on tracks 1-5 (`MM_REPRO_FUZZ=<seed> MM_REPRO_FUZZ_CH=4,5,6,7,8`), 30 s per seed:
  seeds 4 and 8 each drop one kick; seed 4 drops the kick at 22.37 s. Every seed replays exactly.

The DigiPRO ensemble firmware test (`mmAudioFirmwareTest --digipro-ensemble`) shows the same symptom
(track index 5 silent) whenever timing is nudged: the dsp56300 cadence fixes in combination, the
threading branch's `GEARMULATOR_MDMM_LOOKAHEAD_US` 1/5/10 and `GEARMULATOR_MDMM_LINK_DELAY_SLOTS` 1-8.
It passes on unmodified code.

## What is established

- The controller sends the trigger. Per block, it sends each voice a 52-word frame (host commands
  0x10/0x12/0x14 = voices 1-3 of that DSP; the mixer DSP1 plays tracks 4-6, the producer DSP2 tracks 1-3)
  and a 1-word command 0x0c. A trig is word 32 = 1 and word 40 = 0x81 for one frame. The dropped beat
  has an identical trig frame.
- The mixer reads all 52 words of that frame, in order.
- The voice never starts. Mixer `x:0x06d8` idles at 2, goes to 0 while a note sounds, and stays at 2 on
  the dropped beat. `y:0x063e`/`y:0x073e` (envelope) stay 0. `x:0x00e3`/`e4`/`ec`/`ed` cycle through
  0x41-0x43 and look like ring indices; they sit at a different phase on the dropped beat.
- At the dropped beat, the controller's frames arrive in a shifted order relative to the 0x0c command.
- Ruled out: overwritten queued host commands, empty or held HRX reads, link drop heuristics (the
  strobe purge, prefix drops and strobe-during-catch-up drops show the same counts around played and
  dropped beats), and the next frame for the voice arriving early.

## ColdFire speed: a phase window, not a speed bug

`MM_UC_CYCLE_SCALE=<percent>` scales every ColdFire instruction's cost without touching its timers.
Missing kicks over 8 fuzz seeds x 46 beats:

| scale | 80 | 100 | 110 | 120 | 130 | 150 | 200 |
|---|---|---|---|---|---|---|---|
| missing | 25 | 2 | 1 | 0 | 4 | 4 | 0 |

The drops come back at 130-150%, so this is a periodic window in the alignment between the controller's
frame timing and the mixer's frame cycle. Modelling ColdFire bus timing would move the window, not
close it. For the record, the firmware programs CS0 flash 16-bit 4 WS, CS1 16-bit 3 WS, HDI08 CS2/CS3
8-bit 0 WS and main RAM CS5 16-bit 0 WS, and MCF5206E UM 3.6.1 says the instruction timing tables
assume zero-wait memory. The emulator models neither.

## Mechanism (mixer DSP firmware, 2026-09-25)

- Host commands 0x10/0x12/0x14 arm DMA channel 5 (`DCR5 = $8e9ac4`, request = host receive, destination Y)
  to copy the 52 frame words into `Y:$500`/`$600`/`$700` (voices 1-3; the mixer plays tracks 4-6). The
  controller then writes the words and DMA copies each as it arrives. Command 0x0c reads one DSP X word
  (`x:(addr)`) back to the controller; in E12 it always polls `x:$701` and always reads `0x1fff`.
- Frame word 32 (`Y:$x20`) is a latch the audio routine clears when it takes it (P:$4ff-$504).
- Frame word 40 (`Y:$x28`, `0x81` on a trigger frame) decides the note start. Per pass, the voice routine
  reads it at P:$e1 and turns bit 7 into "start pending" (P:$e2-$e3, `0x81 -> 1`); P:$88b starts the voice
  if it is 1; P:$143-$148 clears it at the end of the voice's processing.
- A trigger frame whose word 40 lands after voice 3's read (P:$e1) but before its clear (P:$148) is wiped
  unseen: the note never starts. The window lasts 9,406 of every 36,864 DSP cycles.
- Trigger frames normally land 9,775-21,777 cycles after voice 3's read (mostly 11-14k), just after the
  window closes. The dropped beat of fuzz seed 4 landed at 9,255, 167 cycles too early.
- The frame period equals the DSP pass, so the offset is the controller's processing latency after its
  sync point. A faster emulated ColdFire lands frames earlier, inside the window.

## ColdFire bus timing

The firmware enables the instruction cache (CACR `$81000503`). `MM_BUSSTATS` over 10 s of E12 playback
estimates that the real bus (3-clock external transfers plus wait states, 16-bit main RAM, 8-bit HDI08,
4 KB direct-mapped I-cache misses) adds about 61% cycles for data accesses and 16% for cache misses on
top of the emulated table timing. The emulated ColdFire therefore runs well ahead of silicon, which is
the direction that puts trigger frames into the danger window.

## Next step

Model ColdFire bus timing per access from the programmed chip selects (port size, wait states, burst)
and the instruction cache, behind a flag, and check that the trigger-frame margin grows and drops stop.

## Diagnostic knobs on this branch

| variable | where | purpose |
|---|---|---|
| `MM_REPRO_LEVEL0`, `_FUZZ`, `_FUZZ_CH`, `_BLOCK`, `_CLOCK`, `_DUMP`, `_DUMP_DELAY`, `_WATCH*`, `_DUMP_CS` | mmPatternRepro | isolate tracks, fuzz, dump or watch mixer memory, print chip selects |
| `OCTFIX_HOSTLOG=<file>`, `OCTFIX_HOSTLOG_FROM/TO` (ColdFire cycles) | dsp56300 hdi08, mddsp, mdhardware | host-port and link trace, rotating 300 MB files |
| `OCTFIX_OFF=irq,hdi,timer` | dsp56300 | switch off the experimental cadence fixes (all off = upstream behaviour) |
| `MM_UC_CYCLE_SCALE=<percent>` | mdmc | ColdFire instruction cost scale |
| `MM_BUSSTATS=1` | mdmc | count ColdFire bus accesses per region and estimate wait-state cost |
| `mmAudioFirmwareTest --phase-sweep` (`MM_SWEEP_*`) | test | note-on phase sweep after the ensemble pre-note sequence |

The dsp56300 cadence fixes (timer phase, prompt external interrupts, host-command completion polling)
are real model bugs but change output on both machines. They are on by default in this branch's
submodule commit and switched off with `OCTFIX_OFF=irq,hdi,timer`.
