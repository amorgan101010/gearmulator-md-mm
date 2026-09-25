# MD/MM: running the DSPs on more than one core

Status: phase 0 UC↔DSP timing experiment implemented and measured; no MD/MM DSP worker thread yet.
Written 2026-09-18; phase 0 updated 2026-09-23.

This branch starts at upstream's `release/md-mm-alpha` commit `8cea0524`. It carries the opt-in phase 0
scheduler experiment and its sweep and handoff tools. The measurements below were made on Aileen's
fork at commit `377d0062`, which also has a larger firmware/golden test suite and `mdmmBench`.
They are evidence for trying this design, not a complete validation of the port.
This baseline registers fewer firmware tests, so matching pass percentages alone will not reproduce
the original gate. On 2026-09-23, Linux GCC Release builds of `mdLib` and `mmAudioFirmwareTest`
passed, and `mmSineFirmwareTest` passed at both zero delay and symmetric 5 µs delay using MM OS
1.32b. The complete sweep and DSP↔DSP channel still need validation on this base.

The existing shareable test suite is on Aileen's `fix/mdmm-test-suite` branch at `b3b01eb4`.
It adds golden output and sensitivity checks, block-size and audio-queue tests, and firmware
deadlines. That branch now starts from upstreams's alpha.14 commit `8cea0524`; it is not included in
this threading branch or upstream's release branch. Its golden output and sensitivity tests passed
after the rebase with both firmware images.

## Why

MD/MM emulation runs the ColdFire (UC) and both DSP56300s on one host thread. The mdhardware
scheduler interleaves them and catches one up to another whenever they interact. Every remaining
single-thread optimisation is worth a percent or less. Splitting the work across cores is the only
lever left that is large enough to move the buffer size below 512.

## What the work looks like today

The figures below are from the measured fork: exclusive host time during playback only, with boot
removed by subtracting a 5 s run from a 15 s run. Its instrumentation patch and analysis script
are at `tools/perf/host-split-instrumentation.patch` and `tools/perf/steady_state_split.py` in
that fork, and are not part of this branch.

| | mixer DSP (index 0) | producer DSP (index 1) | UC |
|---|---|---|---|
| MD | 48% | 38% | 14% |
| MM `machine:32 6` | 35% | 36% | 29% |

Cross-processor interactions per emulated second:

| | DSP↔DSP link frames | UC→DSP catch-ups (mixer / producer) |
|---|---|---|
| MD | 382k into the mixer, 423k into the producer | 19k / 4k |
| MM | 255k into the mixer only | 79k / 37k |

On MD, the UC also polls both DSPs' host transmit registers before every UC instruction
(`pumpDsp2HostRequest`), and HREQ is wired from the producer only.

The link slot is 96 DSP cycles. The DSPs run at 2304 cycles per 44.1 kHz frame, which is
101.6 MHz. MD therefore interacts roughly every 120 emulated DSP cycles, or about 0.8 µs of host
time. MM interacts roughly every 270 cycles, or about 2.6 µs.

## Ceiling

The best two-thread partition keeps the UC with whichever DSP it talks to most. The other DSP goes
to a worker thread.

| Partition | MD | MM |
|---|---|---|
| UC + mixer / producer | 62 / 38, at most 1.6x | 64 / 36, at most 1.55x |
| UC + producer / mixer | 52 / 48, at most 1.9x | 65 / 35, at most 1.55x |

Synchronisation overhead comes off these figures. Realistically that leaves about 1.3 to 1.5x on MD
and 1.25 to 1.4x on MM. On this machine, a 1.3x gain puts the heavy MM patch at block 512 from about
1.04 of the realtime budget to about 0.8, and makes 256 plausible.

## The core problem

Today, when one processor touches another, the scheduler first runs the other processor forward to
exactly the same emulated instant. On one thread that is free. Across threads it means one core
waits for the other every microsecond, which costs about as much as the work itself.

A threaded design can only win if each cross-thread channel has **lookahead**, a minimum emulated
delay between one side acting and the other side observing it. With lookahead L, a thread may run up
to L past the other thread's published time without waiting. This is conservative parallel
discrete-event simulation. It stays **deterministic**, meaning the same output on every run, because
every message is delivered at a fixed emulated time and never at a host-timing-dependent one.
Deterministic is not the same as identical to today. The current catch-up overshoot cannot be
reproduced exactly, so the output hashes change once, to a new reference value.

The channels, and the lookahead each one has naturally:

1. **DSP↔DSP ESSI0 link.** A transmitted word physically occupies the next slot before the receiver
   sees it, so a latency of one slot (96 cycles) is faithful. The current emulation sometimes
   delivers within the same slot.
2. **UC↔DSP host port (HI08).** It has almost no natural latency. The UC reads the other DSP's
   status, writes words and raises host commands, and on MD it polls every instruction. This channel
   needs an **introduced** delay Δ.
3. **Direct cross-DSP state access inside callbacks.** These are the fork's link-fidelity mechanisms:
   - the producer's TX callback reads the mixer's DMA DCR4 and ESSI SR, and writes the mixer's SR.ROE;
   - the mixer's PortC write purges the mixer ring and `hostWrite`s the producer's PortC;
   - the MD rendezvous reconfigures both ESSIs and installs callbacks;
   - the mixer's on-demand TX idle callback runs the producer inline;
   - `serviceRamRecordingMode` patches the producer's P memory.

   None of these can run against a DSP that is executing concurrently. Each has to become a
   timestamped message or a state snapshot that is published with a timestamp.

## Plan

Each phase ends with a go/no-go gate. Phases 0 and 1 run entirely on one thread, so they are safe,
testable and cheap to abandon.

### Phase 0: will the firmware tolerate lookahead? (about a day)

The cheapest decisive experiment needs no threads. In the existing scheduler, add a delay queue to
each channel:
- link frames become visible to the consumer 1 slot after the producer's TX edge;
- UC→DSP writes, host commands and DSP→UC status and data become visible Δ later.

Sweep Δ at 0.5, 1, 2, 5 and 10 µs. For each setting:
- run the registered regression suite (`ctest`, including the firmware and timing labels);
- run the RAM record/playback oracles, the SysEx/turbo-MIDI manual tests, the pattern and kit
  load/save tests and a boot;
- listen.

Also micro-benchmark a cross-core atomic ping-pong on the 5600G to get the real sync cost.

**Gate:** Continue only if there is a Δ of at least about 1 µs at which everything passes. If the
firmware only works with Δ≈0, threading cannot win and the project stops here, having cost one day.

**Measured 2026-09-19 (the ping-pong part of this phase):** `tools/perf/pingpong.cpp`, two spinning threads on
separate physical cores of the 5600G (one CCX), 1M round trips. One-way handoff is about 37 ns, round trip about 75 ns.
Against the interaction intervals above (MD about 0.8 us, MM about 2.6 us of host time), one round trip per interaction
would cost about 9% on MD and 3% on MM if every interaction required one round trip. This microbenchmark does
not measure the proposed queues or a loaded DAW. Firmware tolerance is the next gate.
**Run 2026-09-19: the UC↔DSP channel passes its gate.** `GEARMULATOR_MDMM_LOOKAHEAD_US` makes a UC read see the
DSP that many microseconds behind (`schedCatchUpDsp` stops short of the UC time); `GEARMULATOR_MDMM_WRITEAHEAD_US`
makes a UC write land that far into the DSP's future (it runs the DSP past the UC first). Together they model what a
thread with lookahead L would see, without threads. The symmetric sweep selected 23 behavioural firmware tests;
`mdSysexLifecycleTest` skipped because it requires four explicit fixtures, so each passing run executed 22 tests. Golden
tests were excluded because their existing hashes encode the old timing. The saved logs remain in
`build-pgo/lookahead/` in the original experiment checkout; the command to rerun the sweep here is
`tools/perf/lookahead_sweep.sh BUILD_DIR OUT_DIR [microseconds...]`.

| Delay in both directions | Saved result |
|---|---|
| 1 µs | 22 pass, 1 skip (one run) |
| 2 µs | `mmSineMidiFirmwareTest` fails |
| 3, 4, 5, 7 µs | 22 pass, 1 skip at each value |
| 10, 20 µs | `mmSineFirmwareTest` and `mmSineMidiFirmwareTest` fail |

The 5 µs setting also passed three repeat runs. Two suspect tests passed five out of five at zero delay. The 1 µs
pass and 2 µs failure show that this is not a monotonic tolerance threshold; the reliable sampled band is 3–7 µs.
Earlier read-only sweeps gave different failures and mistakenly included the golden-labelled block-size test.
One-direction delays are not equivalent to the symmetric experiment. Informal listening at 5 µs sounded fine;
there is no controlled audio-quality or output-hash comparison yet.
**Gate: passed, for this channel.** A lookahead of about 5 us is far above the 0.8 us (MD) and 2.6 us (MM) between
chip interactions. That suggests room for useful overlap, but is not a measured threaded speedup.
Not yet tested: the DSP<->DSP link delay and the direct cross-DSP callbacks (rendezvous, ROE, the on-demand idle
callback), which phase 1 has to convert into timestamped messages anyway. RAM recording, SysEx/turbo-MIDI and
save/restore also need explicit phase 0 coverage under the symmetric delay.
### Phase 1: message-passing refactor, still single-threaded (about 3 to 6 days)

1. Give every processor a published time, an atomic cycle count. Give every cross-thread channel a
   timestamped single-producer/single-consumer queue:
   - link frames;
   - host port words and host commands;
   - DMA/PortC/SR state changes that the other side reads;
   - P-memory patches.
2. Replace the inline catch-ups (`schedCatchUpDsp`, `schedCatchUpDspToDsp`, `onUCRxEmpty`'s
   unbounded run, the idle TX callback) with a horizon check: *may I run to time t?* This is true
   when every other thread's published time plus its lookahead is at least t.
3. Keep driving everything from the one scheduler thread, and make the scheduler's order
   configurable (quantum sizes, which processor runs first).

**Correctness test, and the key idea of this plan:** once each processor's behaviour depends only on
timestamped messages, the output hash must be **identical for every scheduling order and quantum
size**. Run the bench with many different orderings. Any hash difference points at a channel that
still leaks host order. This proves the protocol is thread-safe before a single thread exists.

**Gate:** hash invariance across orderings, all regressions pass, and the single-thread speed loss
is under about 5%.

### Phase 2: move one DSP to a worker thread (about 2 to 3 days)

- Use a dedicated worker loop around `execUntilCycles(horizon)`. Do **not** use `dsp56k::DSPThread`:
  its batch trampoline only checks the instruction-count peripheral deadline and ignores the cycle
  deadlines that MM relies on (`exactEssiCycleDeadlines`).
- Wait by spinning briefly, then fall back to a futex or `atomic::wait`. Pin the worker thread and
  give it the same realtime priority as the audio thread, to avoid priority inversion.
- Choose the partition per model from the table above, measured rather than guessed.
- Verify:
  - the hash equals the Phase 1 hash over many runs, including under deliberate CPU load
    (stress-ng);
  - a ThreadSanitizer build is clean;
  - the plugin keeps working in the standalone and in Bitwig.

**Gate:** a measured wall-clock gain on the idle machine using alternating pairs, and a lower p99 at
block 256.

### Phase 3: integration and tuning (about 2 to 4 days)

- `Device::processAudio` also advances a second, deferred `Hardware` on the audio thread. That
  instance needs the same threading, or must stay single-threaded.
- Handle state save and load, DSP clock percent, the debugger, and shutdown/teardown ordering.
- Tune the lookahead per channel against the Phase 0 limits, and consider a third thread (UC alone)
  only if the numbers support it.
- Update `tools/gcc-pgo.sh`. Counters then need `-fprofile-update=atomic`, or a profile taken from a
  single-threaded build.

## Risks

- **Firmware timing sensitivity.** The fork already needed rendezvous, ROE emulation, strobe purges
  and timed host receive to get correct audio and SysEx. Introduced latency may break exactly those
  cases. Phase 0 exists to find this out first.
- **Timing changes invalidate current output hashes.** The registered golden output and sensitivity tests
  are available on `fix/mdmm-test-suite`. Bring that suite into the Phase 1 work,
  adopt new reference hashes deliberately after the timing refactor, then require invariance across
  scheduling orders and quanta.
- **Contention in a DAW.** The ceiling assumes a free core. In a busy DAW project the worker may
  wait for a core, and the standalone benefits most.
- **Wake-up latency at small buffers.** Each block adds a cross-thread wake-up of about 10 to 50 µs
  if the worker sleeps. Keep it spinning while audio is running.

## Rough total

Phases 0 to 3 take about two weeks of focused sessions. Phase 0 is the only part that must be done
before deciding whether the rest is worth it.
