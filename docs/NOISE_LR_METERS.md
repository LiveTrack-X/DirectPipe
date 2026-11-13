# RNNoise and L/R meters — 4.5.1 behavior and checkpoints

Date: 2026-11-13
Scope: RNNoise sample-rate conversion/status and existing input/output meters.
Published 4.5.0 binaries and their original 48 kHz limitation are unchanged.

## Behavior

- Native 48 kHz retains the original 480-sample FIFO processing path.
- Other finite rates from 8 to 192 kHz are converted to/from 48 kHz by
  128-tap, phase-interpolated Blackman-windowed low-pass converters. Cutoff is
  90% of the lower Nyquist limit. Each channel has independent history and DSP.
- Coefficients, history and output queues are prepared outside rendering.
  Processing uses bounded sample loops and preallocated storage. This is source
  inspection, not an instrumented proof against every external allocation.
- Let r = hostRate / 48000. Converter buffering is ceil(64*(1+r))+2 host
  samples; total declared latency is that value plus ceil(1440*r), including
  the vendored model/overlap delay. Examples: 44.1 kHz: 1448 samples;
  48 kHz native: 1440; 96 kHz: 3074. These are software
  delay reports, not hardware loopback measurements.
- Chain rows and the open editor refresh native/converted/inactive status at
  5 Hz. Unsupported rates, released resources and unavailable RNNoise states
  do not claim active processing. Parameter serialization is unchanged.
- Existing meter areas contain separate L/R bars with independent smoothing
  and peak holds. Input shows selected capture channels before Mono mixing or
  duplication, with input gain/mute applied. Missing channels read zero. Output is measured after final
  protection, before destination
  volume/mute. Panic and device-loss reset paths clear both channels.
- Legacy scalar telemetry remains compatible: input getter is the original
  first-channel measurement; output getter remains max(L,R). No meter field schema or
  Receiver transport changes are introduced. DPWIM remains an external VST with the existing plugin type.

## Validation

- Windows Release app and host tests build successfully with the repository's
  existing dependencies and CMake/MSBuild (/m:1 /nr:false).
- Initial pre-readiness focused suite: 15 passed (conversion/native FIFO, invalid
  rates, stereo isolation, block-size invariance, anti-aliasing, fractional-clock
  sample counts, independent meters and unchanged Mono gain).
- Earlier pre-readiness regression: 337 tests, 336 passed, 1 skipped, 0 failed in
  29.563 seconds. The skip requires an isolated GForm VST2 DLL; no installed
  plugin was used instead. See `../build/noise-lr-regression-results.json`.
  Run with TEMP and TMP set to the existing workspace-owned
  `build/noise-lr-test-temp` directory. The default sandbox TEMP path was not
  writable by native file fixtures, causing file-save/restore failures; the
  earlier result is retained as `noise-lr-default-temp-results.json`.
- Open-editor status testing pumps pending timers with a bounded two-second
  wait, including timer startup and release/reprepare state changes.
- UTF-8 text integrity and `git diff --check` passed.

Documents consulted: `AGENTS.md`, `sdad-state.yaml`, `docs/INDEX.md`,
`SPEC/DP-450-RELEASE-0001.md` (previous scope),
`SPEC/proposals/DP-MULTI-INPUT-UX-PROPOSAL.md` (deferred boundary),
`TESTING.md`, `docs/USER_GUIDE.md`, `docs/AUTO_DESIGN.md`,
`docs/PRODUCT_SPEC.md`, `docs/ARCHITECTURE.md`, `docs/TODO-Open-Items.md`,
`docs/owner-decisions.md`, `review-findings.md` and `host/Source/Audio/README.md`.

## Limits

No live application was interrupted or replaced. Listening quality, microphone
hardware, end-to-end latency, long-session real-time deadline behavior and
macOS/Linux execution remain unverified. Multi-input capture, ASMR routing,
independent USB clock aggregation and Receiver resampling remain deferred.
This follow-up is local and does not authorize a release or external reply.

## 4.5.1 readiness follow-up

The owner subsequently authorized actual non-native-rate verification and
release preparation. Current evidence and corrections superseding the early
checkpoint above are in [4.5.1 readiness](RELEASE_4_5_1.md), including correct
intrinsic-delay reporting and counter-wrap ordering.

The owner's one-sided hardware screenshot superseded the original post-Mono
input-meter choice. Current input bars are pre-Mono capture with gain/mute,
while the output tap and scalar API remain unchanged. The earlier embedded-DPWIM/meter
checkpoint is superseded by the external installer scope; current evidence is in
[4.5.1 readiness](RELEASE_4_5_1.md). See [input meter examples](#input-meter-examples).


## Input meter examples

Constant-amplitude fixtures, input gain 1.0, effects/final protection disabled:

| Capture and source | INPUT L/R | Mono output L/R |
|---|---|---|
| Stereo pair, first 0.4 / second 0 | 0.4 / 0 | 0.2 / 0.2 |
| Stereo pair, first 0 / second 0.6 | 0 / 0.6 | 0.3 / 0.3 |
| Single selected input 0.4 | 0.4 / 0 | 0.4 / 0.4 |

Single capture uses the first displayed bar even when the selected physical
channel is IN 2; the device channel name remains in Input Ch. Post-effects
stereo and final protection can change output levels independently.
