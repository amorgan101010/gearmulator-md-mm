# MD/MM threading, phase 1: channel inventory and plan

Status: exploration. This document lists every place where one emulated processor (the ColdFire UC,
the mixer DSP at index 0, the producer DSP at index 1) observes or changes another's state. Phase 1
must turn each of them into a timestamped message or a timestamped snapshot before any thread exists.
It also records the link-delay experiment that phase 0 left open. Line numbers refer to this branch
(Joe's `release/md-mm-alpha` 8cea0524 plus the phase 0 experiment).

Each entry is classified by what it needs:

- **time-driven**: the caller runs the other processor forward to a time. With lookahead, this becomes
  a horizon wait (*may I run to t?*).
- **condition-driven**: the caller runs the other processor until something happens (a word appears,
  a command finishes), bounded by a clamp. There is no time bound to wait for. These are the hard ones:
  the answer has to be delivered later as a message, and the waiting side has to tolerate that.
- **state read / state write**: direct access to another processor's registers inside a callback.
  These need either a message, or a snapshot published with a timestamp.

## UC ↔ DSP (HI08 host port)

Phase 0 tested this channel with a symmetric delay (reads see the DSP up to L behind, writes land up to
L ahead). The firmware tests pass at 3–7 µs.

| Where | What | Kind |
|---|---|---|
| `mddsp.cpp:395` `hdiUcReadIsr` → `schedCatchUpDsp` | UC status read runs the DSP to the UC's time | time-driven (phase 0 covers it) |
| `mddsp.cpp:92` MM `setReadCvrCallback` → `schedCatchUpDsp` | UC reads the host-command bit | time-driven (phase 0) |
| `mddsp.cpp:294` `hdiTransferUCtoDSP` → `schedCatchUpDsp(Write)` | UC data word lands in the DSP | time-driven (phase 0) |
| `mddsp.cpp:355` `hdiSendIrqToDSP` → `schedCatchUpDsp(Write)` | UC host command (CVR) | time-driven (phase 0) |
| `mddsp.cpp:305-322` `writeWordToDsp` | runs the DSP until HRX drains, then writes | condition-driven |
| `mddsp.cpp:324-341` `waitForHostCommandIdle` | runs the DSP until the previous host command's RTI | condition-driven |
| `mddsp.cpp:361-376` MM in-order CVR | runs the DSP until HORX is empty, clamp ×4 | condition-driven |
| `mddsp.cpp:260-289` `onUCRxEmpty(needMoreData)` | a blocking UC read runs the DSP until it replies | condition-driven |
| `mdhardware.cpp` `pumpDsp2HostRequest` / `Dsp::pumpHostRx` | the UC side drains the DSP's HOTX register into its own queue | state write (UC mutates DSP HI08 state) |
| `mdhardware.cpp` `pumpDsp2HostRequest` → `setExternalIrq4` | producer HREQ drives UC IRQ4 | state read, every UC step on MD |
| `mddsp.cpp:425-448` MM `hdiTransferDSPtoUC` with `m_timedHostRx` | DSP→UC word staged with a ready cycle, released by UC time | **already a timestamped message**. Use it as the template. |

The condition-driven loops only terminate correctly today because the scheduler can run the peer
inline. Across threads the UC would instead see "not yet" and poll again, which the firmware already
does on real hardware. The open question per loop is whether the firmware *does* poll there, or whether
the emulation leans on the inline run for correctness. Phase 0 kept these loops, so it did not answer
that.

## DSP ↔ DSP (ESSI0 link and cross-DSP callbacks)

| Where | What | Kind |
|---|---|---|
| `mdhardware.cpp` `pushToInput` → `schedCatchUpDspToDsp` (two sites) | producer TX runs the consumer to the producer's time before enqueueing | time-driven |
| `mdhardware.cpp` `setOnDemandTxIdleCallback` (MD rendezvous) → `schedCatchUpDspToDsp(1, 0)` | mixer idle TX slot runs the producer inline | time-driven, but called from inside the other DSP |
| `mdhardware.cpp` `pushToInput`, MD rendezvous branch | reads mixer DMA4 DCR, producer `getLastTxWrittenMask` | state read |
| `mdhardware.cpp` `pushToInput`, MD ROE model | reads mixer ESSI0 SR.RDF, writes SR.ROE (`setReceiverOverrun`) | state read + write |
| `mdhardware.cpp` `pushToInput`, MD await-fresh | reads producer `getLastTxWrittenMask` | own state; safe |
| `mdhardware.cpp` producer TX wrapper, MM await-fresh | reads mixer DMA4 DCR and producer DMA1 DCR | state read (mixer) |
| `mdhardware.cpp` mixer PortC write callback | purges the mixer ring, reads both DMA DCRs, `hostWrite`s the **producer's** PortC | state read + write, runs in mixer context |
| `mdhardware.cpp` producer PortC `setHostInputSource` (MD rendezvous) | producer reads mixer DMA4 DCR to release a pending edge | state read, runs in producer context |
| `mdhardware.cpp` `mdLinkWindowFlushed` | reconfigures **both** ESSIs and installs callbacks | state write, one-time arm |
| `mdhardware.cpp` `blockingPop` MM stall purge | producer RX reads `m_esaiFrameIndex`, which the mixer's ESSI1 advances | state read |
| `mddsp.cpp:66-80` MD mixer RX-consume flush | purges the mixer's own ring, calls `mdLinkWindowFlushed` | own state, but triggers the arm above |

The link frames themselves have a natural lookahead of one slot (96 DSP cycles). Everything else in
this table is coupling that the fork added for link fidelity. It reads the peer's state at the instant of
the callback, which is only well-defined on one thread.

## Scheduler-level reads (`schedStep`)

| Where | What |
|---|---|
| MM backpressure | reads each DSP's `hostTxBacklog()` to park it |
| UC idle skip gate | reads both DSPs' `hdi08().hasTX()` and `hasDeferredHostRx()` |
| DSP origin latch | reads `booted()` of both DSPs to latch the rate-lock origin |

These become reads of published snapshots. The origin latch in particular defines the time base for
every other channel, so it must be latched from a message, not observed.

## Other

- `serviceRamRecordingMode` (`mdhardware.cpp`) patches the producer's P memory from `advance()`. It
  becomes a timestamped P-memory patch message, applied by the producer's own thread.
- Codec ESSI1: both DSPs read host audio input (`codecInput`), and the mixer's ESSI1 callback drains
  the codec ring. That stays with whichever thread owns the mixer.

## Experiment: does the firmware tolerate link latency?

The plan's ceiling table puts the two DSPs on different threads in both partitions, so the ESSI0 link
must tolerate at least one slot of delivery latency. If it cannot, the only split left is UC alone
against both DSPs, worth roughly 1.16x on MD and 1.4x on MM by the host-split table.

`GEARMULATOR_MDMM_LINK_DELAY_SLOTS=N` delays every link frame's arrival at the consumer by N slots
(N × 96 DSP cycles) after the producer transmitted it. The consumer treats a frame as absent until its
own cycle counter reaches the arrival time: skip-on-empty RX and `blockingPop` both check it, and the
stall and flush purges discard only frames that have arrived. Unset or 0 leaves behaviour unchanged.

What it does not change: the consumer catch-up still runs to the producer's time, and the cross-DSP
state reads above still happen at transmit time. In particular, the MD ROE model tests the mixer's RDF
when the word is sent, not when it arrives. A failure at small N may therefore be an artifact of those
couplings rather than of the firmware. The MM strobe purge discards in-flight frames too, because they
belong to the interval that the new request ends.

`GEARMULATOR_MDMM_LINK_DELAY_CYCLES=N` sets the delay in DSP cycles instead, for finer sweeps.

### Results (2026-09-23, Linux GCC Release, MD OS 1.63, MM OS 1.32b)

Sweep: `tools/perf/link_delay_sweep.sh` over the 22 non-golden `FirmwareTest` ctests, one at a time,
with `MD_AUTOMATION_REQUIRE_FIRMWARE=1`. `mdSysexLifecycleTest` fails at every setting, including 0,
because it is registered without its four fixture arguments. It is excluded below. Logs are in
`artifacts/threading-link-delay/` in Aileen's workspace.

| Link delay | Machinedrum tests | Monomachine tests |
|---|---|---|
| 0 (control) | all pass | all pass |
| 1, 2, 4, 8 slots | all pass | `mmSineMidiFirmwareTest` and `mmDigiproFirmwareTest` fail |

At 1, 2, 4 and 8 slots, the two MM failures print identical numbers down to the last digit:
- `mmSineMidiFirmwareTest`: the GND SIN smoothness/pitch check fails on track 5 (note 84 nearly silent).
- `mmDigiproFirmwareTest`: track 5 produces silence.

The other MM tests pass, including boot, audio, sine without MIDI, input, the DigiPRO ensemble and the
LCD edit pages. Test durations do not change measurably.

Finer sweep of `mmSineMidiFirmwareTest` in cycles:

| Cycles | 1, 2, 8, 24, 48, 72 | 84 | 90, 93 | 95 | 96 and up |
|---|---|---|---|---|---|
| Result | pass | fail | pass | fail | fail (identical output) |

Follow-up checks, with a counter of receive checks that found the front word still in flight
(printed at teardown when the delay is on):

- **MD positive control.** At 768 cycles, `mdAudioFirmwareTest` saw about 533k in-flight checks on
  the mixer and 18k on the producer, and still passed. The delay really took effect on MD; the MD pass
  is not a no-op.
- **MM strobe purge.** The PDRC strobe purge normally also discards words still in flight. Changing
  it to discard only arrived words made the MM output differ between 96 and 768 cycles, so words were
  delivered late rather than dropped. But it failed *earlier*: track 0 became rough (roughness 0.012 at
  96 cycles and 0.098 at 768, against about 2e-6 clean), because words from the completed interval
  landed in the new DMA4 window. Neither purge choice works once a reply word is in flight across a
  strobe. The purge was restored to discard everything.
- MM in-flight checks happen on the mixer only (11.8k at 96 cycles, 37.8k at 768); the producer
  side saw none.

Reading:
- **MD tolerates at least 8 slots (768 cycles) of link delivery delay** in these tests. The on-demand
  rendezvous protocol does not depend on same-slot delivery. A DSP/DSP split looks viable for MD.
- **The MM link, as emulated, needs same-slot delivery around the PDRC strobe handshake.** The
  sub-slot band behaves erratically (84 fails, 90 and 93 pass), like phase 0's 1 µs/2 µs result, so
  bisecting it further will not converge. Before the MM DSPs can run apart, its strobe protocol needs
  the same transaction-level decoupling the MD rendezvous has.
- **The knob adds delivery delay. It does not measure the lookahead a threaded design would have.**
  The emulation performs a slot's TX and RX in one tick. A threaded design could fix the TX word at the
  start of a slot and read it at the end, keeping same-slot delivery, and synchronise the two DSPs once
  per 96-cycle slot, about every 0.8 µs of host time. With the measured ~75 ns round trip, that is on
  the order of 10% overhead plus load imbalance. So the MM DSP split is not ruled out; it needs a
  per-slot sync design instead of a latency budget.
- Hypothesis, not yet checked against the DSP56303 user manual's ESSI chapter: the physical link is
  synchronous, so a word sent in slot k is received at the end of slot k. If so, "one slot of latency
  is faithful" was too generous.

## Revised phase 1 order

1. **MD DSP link first.** It tolerates delivery delay. Convert its cross-DSP couplings (rendezvous
   state reads, ROE model, PortC, idle-TX catch-up) into timestamped messages and snapshots.
2. **MM link: per-slot sync or transaction-level decoupling.** Either design a per-slot synchronised
   DSP pair, or decouple the PDRC strobe/reply handshake like the MD rendezvous. Decide from a
   prototype's measured overhead.
3. **The four condition-driven UC↔DSP loops** (`writeWordToDsp`, `waitForHostCommandIdle`, the MM
   in-order CVR drain and `onUCRxEmpty`). Check whether the firmware polls at each of them. If it does,
   replace the inline run with "not yet" plus a timestamped reply, modelled on the MM `m_timedHostRx`.
4. Scheduler-level snapshot reads (MM backpressure, idle-skip gate, origin latch) and the RAM
   recording P-memory patch.
5. The hash-invariance harness across scheduling orders and quanta, with golden references re-taken
   deliberately once.
