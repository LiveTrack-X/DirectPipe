# NoiseRemover (DSP): Learn and Realtime modes

Status: NoiseRemover (DSP) is included in the published 4.5.2 release.
Exact distributable identities, software checks, scan results and field-test
limits: [4.5.2 verification](RELEASE_4_5_2.md).
Date: 2026-11-16
Implementation intent: `../SPEC/DP-DSP-NR-0001.md`; release scope: `../SPEC/DP-452-RELEASE-0001.md` (local protocol).

Add this processor manually through **Add Plugin -> Built-in -> NoiseRemover
(DSP)**. It is separate from RNNoise and is not included in AUTO defaults/reset
or automatically inserted into existing chains/presets.

## Mode selection

- **Learn (default):** with only background noise present, press Learn/Relearn.
  Capture runs for exactly two seconds of processed input, then freezes the
  completed profile. Changing noise conditions requires another Learn.
  Without a compatible completed profile, the editor says Learn required and
  the processor passes audio through its delayed reconstruction path.
- **Realtime:** automatically initializes and updates the noise estimate. This
  is a conventional statistical DSP estimator, not an AI speech classifier.
  Allow settling time; weak sustained voice can be mistaken for background.
  Its changing estimate does not overwrite the saved manual Learn profile.
- Switching back to Learn restores the completed manual profile. Interrupting
  a new Learn by switching mode does not overwrite the previous completed one.
- Reduction controls attenuation strength (0..24 dB, default 12 dB).
  At 0 dB, reconstruction still has its actual buffering delay and Realtime
  estimation continues. Zero attenuation is not zero latency.
- Mode, reduction and completed manual profile are saved in processor/preset
  state. Profile geometry must match the current rate/FFT size; otherwise Learn
  again. Partial capture is never persisted as a completed profile.

## Exact reported DSP latency

Both modes use the same native-rate STFT/Wiener path and fixed N-sample output
scheduling. Learning, missing profile, 0 dB reduction and mode switches do not
remove this delay. The editor shows samples and milliseconds; the processor
reports N via JUCE and contributes N to graph latency compensation.

| Sample rate | FFT / reported samples | DSP delay |
|---|---:|---:|
| 8 kHz | 64 | 8.000 ms |
| 16 kHz | 128 | 8.000 ms |
| 32 kHz | 256 | 8.000 ms |
| 44.1 kHz | 256 | 5.805 ms |
| 48 kHz | 256 | 5.333 ms |
| 88.2 kHz | 512 | 5.805 ms |
| 96 kHz | 512 | 5.333 ms |
| 192 kHz | 1024 | 5.333 ms |

Other finite 8..192 kHz rates use the smallest power-of-two N >= rate/187.5,
bounded to 64..1024. No internal resampling is used. Unsupported rates and released
resources pass through with explicit inactive status and zero DSP latency.

These values describe this processor. Driver buffers, other processors, Receiver
and downstream applications add separate delay. Graph PDC is a software delay
report, not measured microphone-to-headphone round-trip time.

## In-editor spectrum and masking

The editor shows actual DSP analysis, with independent **L / Mono** and **R**
display selection. The upper plot overlays **blue input** (including any voice)
and the **orange estimated noise profile**. In Learn it accumulates during the
two-second capture, then remains frozen; Realtime shows its adaptive estimate.
The lower purple plot shows **applied reduction in dB**, calculated from the
actual per-bin amplitude gains. It is not a measurement of isolated removed noise.

Power is one-sided power per FFT bin, referenced to unit full-scale power
(`10*log10(power)`); reduction uses `-20*log10(gain)`. The display names its Hz/bin
resolution and current FFT/rate. At 48 kHz/256 samples there are 187.5 Hz/bin;
the low-frequency area before the first non-DC bin is shaded rather than filled
with invented detail. DC is omitted from this log-frequency display.

The view reuses the existing FFT, profile and applied mask; it adds no analysis
transform or audio queue. Preallocated coherent atomic snapshots publish at
approximately 20 Hz, with UI-only logarithms/drawing. There is no extra audio
latency. If callback publications stop, the curves expire after 750 ms instead
of remaining falsely live. Inactive/rate-reset states clear immediately.

## Implementation and verification

`StreamingStftDenoiser.h` is the single shared mono core. Input/output history,
FFT tables and spectra are prepared outside rendering. Profile transitions do
not reset WOLA audio buffers. Learn excludes analysis windows containing audio
from before the capture request. Processor controls/status and profile mailboxes
keep control serialization separate from mutable rendering state.

The original isolated 256/512 comparison remains a historical prototype checkpoint
in `../experiments/low_latency_nr/README.md`; its waveform/timing figures do not
prove the extended processor's product or live readiness. Current processor,
editor, graph/preset and exact-delay checks are recorded below after validation.

## Prior implementation evidence and limits

Windows Release candidate app and host tests built successfully. Candidate:
`../build/dsp-nr-spectrum-candidate/bin/DirectPipe.exe` (not launched/installed).

- Native shared core: 41/41 tests passed, including allocation checks, every-hop
  startup/tail impulses, profile transitions, silent-start recovery and tracking
  at zero attenuation. Initial missing-behavior tests failed before implementation.
- Focused host regression: 215 selected, **213 passed, 2 skipped, 0 failures**
  (`../build/dsp-nr-spectrum-evidence/regression.json`). Skips are the opt-in original
  RNNoise audio probe (no fixture env supplied) and isolated external GForm VST2.
- All thirteen processor/editor tests and six spectrum-view tests passed, including eight native rates in
  both modes, exact two-second capture, fractional-rate mono capture, frozen
  profile/Realtime/interrupted-relearn persistence, invalid spectra/rates,
  callback timeline and concurrent serialization.
- Actual AudioProcessorGraph impulse output matched the processor/graph's
  reported 256 samples in both modes. Reprepare at 96 kHz reports 512 samples;
  DSP plus native RNNoise at 48 kHz reports 1696 samples. Async preset restore
  preserved the new type/mode/reduction; existing three-processor AUTO tests pass.
- Spectrum telemetry adds known-bin tone/RMS-power calibration, frozen-vs-live
  input, actual mask/zero reduction, stereo separation and concurrent reads.
  UI fixtures verify actual plotted marks, power/gain dB scaling and stale expiry.
- Software editor screenshots inspected for Learn required, Realtime and frozen
  states under `../build/dsp-nr-spectrum-evidence/ui/`. Samples/ms and mode controls are
  visible. These are fixture snapshots, not a live application/device session.
- Independent plan, core, processor/spec and code-quality source reviews passed
  after fixes. Text-integrity and whitespace checks passed. Existing C4324
  alignment/padding compiler warnings remain in AudioRingBuffer.h.

No installed/live app was replaced.
No real vocal/singing, physical loopback, long-session deadline, macOS/Linux
runtime or owner listening acceptance is claimed. Statistical noise tracking can
damage weak/sustained voice; transient noise remains a weak point. RNNoise is
retained as a separate option with its own delay/quality characteristics.
