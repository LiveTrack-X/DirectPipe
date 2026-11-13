# DirectPipe 4.5.1 documentation and comment consistency audit

Date: 2026-11-13
Status: Final source scope; release execution evidence is recorded separately.

## Coverage

The consistency guard inventories tracked/current additions by path, line count
and SHA-256, classifies historical records, checks current links and canonical
version metadata, and rejects stale meter/embedded-DPWIM capability claims.
Targeted semantic review compares usage, specification, architecture, build,
Auto/API, module READMEs and release notes against changed source/tests.
This does not prove that every pre-existing comment is correct.

## Reconciled behavior

| Area | Current source and documentation |
|---|---|
| RNNoise rates/status | 8–192 kHz internal conversion, native 48 kHz path retained; chain/editor display actual native/converted/inactive state |
| RNNoise PDC | FIFO 480 + intrinsic 960 = 1440 native samples; converted latency includes host-rate buffering/conversion |
| Converter/FIFO comments | Prepared allocation, independent channels, bounded clocks and power-of-two output queue wrapping |
| Input L/R | Selected capture channels before Mono mixing/duplication, with gain/mute applied; absent second source is zero |
| Output/scalar API | Output after final protection, before destination gain/mute; existing scalar API taps preserved |
| Built-ins/Auto | Filter, Noise Removal, Auto Gain only; no embedded DPWIM processor or builtin_dpwim type |
| DPWIM Windows action | Install DPWIM directly below Built-in, explicit optional external VST3 install/update, official SHA-256 and bundle/version verification |
| DPWIM lifecycle | Versioned per-user installs; current/external instances and saved paths retained; explicit addition from the installed plugin list |
| Installer concurrency/cache | Serialized final version recheck; targeted scan merges unrelated cache entries and selects the new managed bundle |
| Preview/version boundaries | DirectPipe 4.5.1 and standalone DPWIM 0.3.4 are separate versions; deferred integration fixes are not claimed in the public plugin |
| Current release metadata | Canonical 4.5.1 host/Receiver/Stream Deck metadata, changelog and release body aligned; prior published records preserved |

Current DPWIM guide: [Windows installation and usage](DPWIM_WINDOWS_SETUP.md).
The official latest stable-release metadata was inspected on 2026-11-13 and
identifies the separately published DPWIM 0.3.4 Windows x64 VST2/VST3 assets.
The helper manages only a VST3 copy; VST2/manual installations are retained.

## Superseded experiment

The embedded-DPWIM candidate was withdrawn by the owner's final direction.
Its source, tests and review fixes are preserved locally in the deferred
snapshot; old evidence is explicitly marked superseded in RELEASE_4_5_1.md.
There is no compiled DPWIM DSP/editor, embedded license payload, auto-install on
host startup, or silent migration of live/saved plugin paths in the final code.

## Evidence boundaries

- Offline speech/noise probes establish RNNoise execution, not hardware listening.
- Installer fixtures cover failure/preservation/concurrency/cache behavior.
  The exact published ZIP fixture also exercises real extraction and Windows
  metadata/resource checks without executing the external plugin.
- The Windows CI opt-in smoke uses the real HTTPS/latest-release/install path
  only inside an owned test directory. No actual user plugin folder is changed.
- Local socket/DNS/registry/desktop restrictions are not passes. Exact-source
  CI and public artifact checks are recorded in [release verification](RELEASE_4_5_1.md).
- Hardware/app capture, prolonged realtime sessions and broad DAW listening
  acceptance remain separate from software checks and source review.
