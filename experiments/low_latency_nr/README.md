# Low-latency voice noise-removal prototype

Date: 2026-11-16. Historical experimental checkpoint, separate from shipped-product verification.
Authority/acceptance: `../../SPEC/DP-STFT-NR-0001.md` (local protocol).

This document preserves the earlier isolated 33-test/30-row experiment. Later
integration shipped Learn/frozen and Realtime/adaptive modes in 4.5.2; see
[current DSP feature/evidence](../../docs/DSP_NOISE_REMOVAL.md). The shared core
now lives in `host/Source/Audio/StreamingStftDenoiser.h`; this directory's header
is a compatibility alias. Original WAV/timing hashes remain historical and were
not relabelled as current product/live validation. The old raw probe retains its
explicit-Learn plus adaptive-core comparison behavior; it is not the new editor's
frozen-mode processor wrapper.

## Result

Prefer **256 samples** for the next voice-monitoring listening experiment.
The 512-sample version gives slightly better stationary-noise metrics but doubles
the buffering delay. This is not proof of real vocal/monitoring quality.

At 48 kHz, native C++ impulse/unity reconstruction measurements confirm 256
samples = **5.333 ms**, 512 = **10.667 ms**. At 44.1 kHz: 5.805/11.610 ms.
Current actual-host RNNoise reports 30.000/32.834 ms, respectively. Device,
other plugin, graph, Receiver and downstream delays remain additional.

48 kHz synthetic fan case, identical signal levels and declared-delay alignment:

| Measurement | 256 | 512 |
|---|---:|---:|
| Noise-only pause attenuation | 10.62 dB | 11.24 dB |
| Speech-active SNR improvement (including voice distortion) | 2.36 dB | 2.68 dB |
| Clean-case level change after learning fan profile | -0.27 dB | -0.26 dB |
| Clean-case unscaled output SNR | 22.75 dB | 21.85 dB |

Hiss speech-active SNR improvement: 8.70/8.87 dB. Hum: 1.80/2.52 dB.
Clicks: only 1.63/1.16 dB; the prototype is weak against transient noise.
This is one locally synthesized spoken voice, not singing or a real mic room.
Pointwise SNR also penalizes waveform/phase changes; it does not rank perceived
quality against RNNoise. No listener/owner acceptance has been recorded.

## Evidence and listening

- `../../build/low-latency-nr/evidence/report.md`: all 30 measured rows.
- `report.json` in that directory: metrics, timing, input/source/executable hashes.
- `fan-comparison-reel.wav`: clean -> noisy -> 256 -> 512 -> current RNNoise,
  each eight seconds plus a half-second silent separator. All segments are delay
  aligned; no per-output gain/peak normalization. Alignment removes transport
  delay for listening only; `*-delayed.wav` retains prototype latency.
- `48000-fan/` and the other rate/noise directories contain individual clean,
  input and candidate WAVs. Noise-only Learn occupies the first second, followed
  by half a second of gain settling; the reel omits this prefix.
- Original speech: previously locally synthesized
  `build/noise-lr-release-evidence/audio/speech-original.wav`, not user audio.
  Its SHA-256 is recorded; future reproduction requires this input or a new
  explicitly identified speech WAV. Noise seeds are deterministic and separate
  between training and held-out evaluation.

## Implementation

- `StftDenoiser.h`: mono preallocated radix-2 FFT, sqrt-periodic-Hann WOLA with
  quarter-window hop, phase-preserving decision-directed soft Wiener gains,
  12 dB maximum nominal gain attenuation, 2 ms opening/60 ms closing smoothing.
- Explicit Learn makes a noise spectrum. Without Learn the processor reconstructs
  the voice with delay and does not silently learn the startup voice as noise.
  A conservative noise tracker updates quiet bins afterwards. Weak sustained
  voice can still be mistaken for noise; long-duration robustness is unproven.
- Future output ring schedules exactly N samples of unity delay independent of
  host callback boundaries. Initial history is zero padded; the CLI flushes 2N
  zeros. Reuse separate instances per channel; no shared mutable DSP state.
- `tests.cpp`: 33 checks covering both rates/sizes, startup/tail/random/impulses
  at every hop phase, callback partition invariance, learned held-out suppression,
  strong tone retention, reset, silence, NaN/Inf and no implicit learning.
  Global new/new[] instrumentation detects no render/reset allocations on these
  paths. Source inspection found no render locks, logs or OS operations. This is
  not a proof about future backends or production integration.
- `probe.cpp`: offline float32 mono CLI, output and callback wall-time statistics.
- `compare.py`: fixed clean-derived 10 ms/-35 dB RMS activity mask, no fitted
  output gain, same evaluation intervals and held-out noise for all candidates.
  Uses the actual existing BuiltinNoiseRemoval host test/probe, reset per case.
  RNNoise sees the same noise context but uses its default adaptation/VAD rather
  than STFT's explicit profile Learn. Standard VAD threshold is 0.70.

## Timing limits

The 48 kHz run spent roughly 0.58-0.74% of audio duration in STFT-256 processing;
this is elapsed processing wall time, not a hardware CPU-utilization measurement.
The callback size was 128 samples (2.667 ms nominal at 48 kHz).

**Observed maximum wall-time overruns must not be hidden by low p99 values:**
the 512 click case reached 3.5082 ms, and the actual-host RNNoise click case
reached 5.6501 ms. These are observed offline wall times; the cause is not
established and no audio-device dropout was measured. This run does not prove
hard real-time stability. RNNoise's existing probe provides mean/max, not p99.
All timings were sequential, not concurrent DSP benchmark runs.

## Reproduce on this Windows checkout

Use the installed Visual Studio CMake (or put it on PATH):

```powershell
cmake -S experiments/low_latency_nr -B build/low-latency-nr/native -G "Visual Studio 17 2022" -A x64
cmake --build build/low-latency-nr/native --config Release
build/low-latency-nr/native/Release/stft-nr-tests.exe
python experiments/low_latency_nr/compare.py --output build/low-latency-nr/evidence
```

For RNNoise comparison first build the current `directpipe-host-tests` Release
target from the existing main build. Python requires NumPy and SciPy. The runner
uses Windows executable paths; other platforms have not been built or measured.
`--skip-rnnoise` explicitly omits that baseline; do not imply it ran.
`--refresh-report` derives presentation/budget flags from existing JSON without
rerunning DSP or replacing measured timings/audio. The measurement-run source
hashes remain distinct from the current report-analysis source hash.

The raw CLI arguments are:

```text
stft-nr-probe input.f32 output.f32 stats.json rate N reductionDb learn learnSamples blockSize
```

## Original experiment integration boundary

At the original checkpoint no app menu, Auto chain, RNNoise processor, preset
state, Receiver, installed binary or production build target was changed.
Subsequent local manual Built-in integration is documented in the current guide;
AUTO/RNNoise/Receiver behavior and installed/live binaries remain preserved.
Before any app integration: listen on real spoken/sung voice at matched levels,
test weak/continuous voice and noise changes, cover channel isolation and latency
reporting in the graph, then obtain the intended feature scope. Device latency,
physical loopback, live stability and macOS/Linux remain unverified.
