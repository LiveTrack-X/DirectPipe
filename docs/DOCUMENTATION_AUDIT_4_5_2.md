# DirectPipe 4.5.2 documentation and source-comment audit

Date: 2026-11-16
Status: Source/document reconciliation; release execution is recorded separately.

## Scope and evidence

The published 4.5.1 source is the comparison baseline. This audit reconciles
4.5.2's manual DSP processor, spectrum view and fresh Receiver default against
current source/tests. It also checks current usage, product, architecture, API,
build, test, update, dependency and release-document routes for stale claims.
The active local release packet is `SPEC/DP-452-RELEASE-0001.md`; local control
files are not required to build or use the public source.

The consistency guard inventories tracked/current Markdown by path, line count
and SHA-256, classifies historical records, checks current document links and
canonical version metadata, and rejects selected stale capability claims.
Targeted semantic review supplements that inventory; neither proves every
pre-existing sentence/comment correct. Machine inventory includes historical
records without treating them as current instructions or current test evidence.

## Documentation coverage

| Surface | Review and reconciliation |
|---|---|
| `README.md`, `docs/USER_GUIDE.md`, `docs/QUICKSTART.md` | Four available built-ins versus three AUTO defaults; manual DSP entrypoint; Learn/Realtime, saved profile, real delay and graph interpretation; Medium for new Receivers with saved choices retained; current host/Receiver pairing and older updater behavior |
| `docs/PRODUCT_SPEC.md`, `docs/ARCHITECTURE.md` | DSP type/factory/state, independent channels, native FFT geometry and N-sample delay, coherent telemetry and RT/control ownership; RNNoise conversion introduced in 4.5.1 rather than unreleased; Receiver APVTS default/state boundary |
| `docs/CONTROL_API.md`, `docs/API_EXAMPLES.md` | Add `builtin_noise_removal_dsp` to observed plugin types; existing bypass/type/latency schema retained; AUTO action excludes DSP; no invented DSP mode/Learn or spectrum endpoint |
| `docs/BUILDING.md`, `TESTING.md`, `CONTRIBUTING.md` | Public-source standalone C++17 core reproduction, main CTest native registration, focused host/Receiver checks and unchecked manual follow-up; historical counts kept separate; draft-only CI and final-file/public-download gates |
| `docs/PLATFORM_GUIDE.md`, `SECURITY.md` | Current evidence routing and upgrade baseline; software/CI evidence separated from device/listening and security assurance; existing platform maturity/support distinctions retained |
| `host/Source/Audio/README.md`, `host/Source/UI/README.md` | DSP/core/editor/view inventory and thread ownership; input L/R tap corrected to precede Mono mapping with numerical gain/mute, while scalar telemetry remains after mapping |
| `docs/DSP_NOISE_REMOVAL.md`, `docs/NOISE_LR_METERS.md` | Current DSP behavior versus retained RNNoise/L/R behavior and historical experiment/test checkpoints; no local-source result relabelled as field acceptance |
| `docs/AUTO_DESIGN.md`, `docs/STREAMDECK_GUIDE.md` | AUTO remains Filter + RNNoise + Auto Gain; existing action identities, discovery/control paths and IPC scope retained; no new Stream Deck DSP action claimed |
| `docs/DPWIM_WINDOWS_SETUP.md`, `THIRD_PARTY.md` | DPWIM remains an explicit optional external Windows VST3, separate from DSP/AUTO; no added DSP SDK/dependency or proprietary SDK redistribution required |
| `docs/BACKUP_RESTORE_DIAGNOSTICS.md`, `docs/WINDOWS_UPDATE_RECOVERY.md` | Historical diagnostic/update checkpoints retained; new processor state follows existing preset/full-backup paths; no claim that settings-only backups now contain plugin chains |
| Current changelog/release notes/body/evidence | Release coordinator maintains version, actual software/CI results, scan dates/hashes/coverage, final payload/checksum and publication status; this audit does not substitute for those records |
| Prior release/audit records, old `dist` bodies, plans and experiment checkpoints | Preserved as historical records; prior hashes/test totals/release authorization do not prove 4.5.2 delivery |

## Source and comment coverage

The source inventory covers host audio/UI/control/IPC/persistence/platform
modules, shared core, Receiver and tests. Detailed new-feature comment review
and targeted integration checks covered:

| Source | Checked behavior/comment boundary |
|---|---|
| `StreamingStftDenoiser.h` | Preallocated per-channel state, Hann/WOLA normalization, exact N-sample output scheduling, post-Learn window eligibility, explicit capture versus adaptive estimation |
| `BuiltinNoiseRemovalDSP.h/.cpp` | Render-excluded lifecycle, control-only serialization retries/mutex, one bounded RT restore read, profile validity/generation, exact two-second capture and completed-profile publication, inactive/rate status, one-sided bin-power calibration |
| `DspNoiseRemovalEditPanel.h/.cpp`, `DspNoiseSpectrumView.h/.cpp` | Message-thread controls/plots, actual cached analysis/mask, L/Mono and R, labelled resolution, partial capture, stale expiry and no added transform/audio queue |
| `VSTChain.h/.cpp`, `PluginPreloadCache.cpp`, `PresetManager.cpp` | New type and factories, async/preset/preload integration, ordinary graph PDC, old types/slots preserved and no AUTO insertion |
| `StatusUpdater.cpp`, `PluginChainEditor.cpp` | Actual plugin type/latency/status propagation and explicit Built-in menu entry; existing scalar status/API identities preserved |
| `AudioEngine.cpp` | Existing capture mapping/gain/mute versus independent L/R meter taps; final protection/output metering and additional device/PDC latency boundaries |
| Receiver `PluginProcessor.h/.cpp`, `PluginEditor.cpp` | Choice index 2/1024 Medium for fresh instances, existing five parameter values and APVTS project restore, unchanged IPC/latency identities |
| DSP/core/spectrum/graph/preset/Receiver tests and CMake | Test intent matches actual source paths; native checks are part of regular CTest and independently reproducible without private SDKs |

Two ambiguous source comments were corrected: render-thread ownership now also
names render-excluded lifecycle access; the core comment distinguishes manual
capture from Realtime estimator initialization. Existing FIFO/converter, mono
routing, AUTO, control/IPC and update comments were inspected at integration
points rather than mechanically rewritten across unrelated code.

## Reconciled behavior and claim limits

- Learn is the default and captures two seconds of processed noise-only input;
  Realtime adapts without overwriting the last completed manual profile.
  Mode/reduction/completed profile persist; unfinished capture and transient
  Realtime estimates are not saved as a new completed manual profile.
- Missing/incompatible Learn profiles use delayed unity. All supported active
  modes retain actual N-sample delay even at zero reduction: 256 samples /
  5.333 ms at 48 kHz. Unsupported/released states are explicitly inactive.
- Blue input includes speech, orange estimates noise and purple shows actual
  applied reduction. Plots are per-channel cached analysis, not isolated removed
  noise or additional high-resolution DSP. Viewing adds no audio delay.
- Medium changes only fresh Receiver defaults. All five saved project choices,
  existing IPC protocols and preset parameter identities remain compatible.
- No new manual checklist, singing/listening, physical loopback, OBS frontend,
  UAC, long-run or platform-device acceptance is claimed from source/fixture
  checks. Signing/notarization and scan coverage remain explicit release limits.
- Prior VirusTotal reports bind exact prior bytes. Final archives/payloads and
  public downloads require their own identity/hash checks and applicable scans.

## Validation and outstanding gates

Documentation validation uses `python tools/check_release_consistency.py`,
`python tools/check_text_integrity.py --repo-root .`, and `git diff --check`.
Their executed results and fresh software/CI results are recorded in the
[4.5.2 verification record](RELEASE_4_5_2.md), not inferred from this inventory.

At source reconciliation, exact-source multi-platform CI, final public-payload
scan/hash review and independently verified publication remain release gates.
The release stays draft until the coordinator records those gates as passed.
Historical records, live app/devices/configuration and installed plugins were
not modified by this documentation audit. No source-level important defect was
identified by this document/comment pass; software and artifact review remain
separate evidence.

The first PR CI exposed an additional cross-platform source issue: LP64 makes
int64_t differ from JUCE's exact var conversion type. The DPWIM release asset
size conversion now names juce::int64 and explains the portability constraint.
The failed run remains evidence of the original defect; subsequent platform
validation is tracked in RELEASE_4_5_2.md.
