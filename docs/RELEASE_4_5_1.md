# DirectPipe 4.5.1 release preparation

Status: Authorized release execution; publication pending verification
Date: 2026-11-13
Base: bca449916351a436284816f3555f7e43641a70e7 (published 4.5.0 follow-up)

Final scope: RNNoise and capture L/R meters plus optional Windows external-VST3
Install DPWIM action. Embedded-DPWIM candidate artifacts below are superseded
and excluded from release. See the final scope/execution sections.

## Changes and scope

- RNNoise 8-192 kHz internal conversion, native path retained at 48 kHz.
- Independent L/R input/output meters in the existing layout.
- Correct native/converted/inactive status, including Auto/control entrypoints.
- Correct latency reporting: FIFO 480 + vendored RNNoise overlap/lookahead 960
  = 1440 samples (30 ms) at 48 kHz. No audio delay is added by this correction.
- Power-of-two output FIFO capacity preserves ordering at uint32 counter wrap;
  an actual-processor regression compares nonconstant audio across the boundary.
- Existing Mono gains, presets, API schema, Receiver same-rate requirement and
  Stream Deck action identities remain compatible. No multi-input/ASMR routing.

## Verification

The current candidate builds successfully on Windows. No public release has
been created; remaining gates below are not satisfied by this local build.
Detailed behavior: [RNNoise/LR implementation](NOISE_LR_METERS.md).
Comprehensive documentation/comment inventory: [audit](DOCUMENTATION_AUDIT_4_5_1.md).

The offline probe uses locally synthesized speech and deterministic noise,
not a recording of the user. Actual RNNoise output WAVs are compared with clean
speech and noisy input at 8/16/32/44.1/48/88.2/96/192 kHz. Noise-only attenuation,
speech correlation, declared-vs-aligned delay and processing time are recorded.
Silence below the 24-bit WAV floor is capped at 100 dB in the report; it is not
a claim of 100 dB physical noise attenuation.

Evidence directory: `build/noise-lr-release-evidence/` (local, reproducible).

### Actual offline processing evidence

| Host rate | Speech correlation | Measured alignment | Reported processing latency |
|---|---:|---:|---:|
| 8 kHz | 0.9649 | 39.625 ms | 317 samples / 39.625 ms |
| 16 kHz | 0.9664 | 35.438 ms | 568 samples / 35.500 ms |
| 32 kHz | 0.9684 | 33.375 ms | 1069 samples / 33.406 ms |
| 44.1 kHz | 0.9681 | 32.812 ms | 1448 samples / 32.834 ms |
| 48 kHz native | 0.9663 | 29.938 ms | 1440 samples / 30.000 ms |
| 88.2 kHz | 0.9682 | 32.062 ms | 2830 samples / 32.086 ms |
| 96 kHz | 0.9680 | 32.000 ms | 3074 samples / 32.021 ms |
| 192 kHz | 0.9669 | 31.625 ms | 6082 samples / 31.677 ms |

All eight rates passed the actual suppression/speech/alignment checks.
Noise-only output was below the 24-bit output floor in the tested window;
this includes VAD gating and is not a general noise-reduction specification.
Maximum measured-vs-reported alignment error was 0.0625 ms. The input is a
19.452-second locally synthesized speech/noise fixture. This is offline mono
DSP evidence; physical microphone/driver performance is separate.

### Repository and package checks

- Full Windows Release build passed (host, Receiver VST2/VST3, core/host/
  Receiver/endpoint tests and isolated helper targets).
- Final feature/compatibility regression: **348 passed, 1 GForm-dependent skip,
  0 failed**, 349 tests in 34.257 seconds. Includes RNNoise signal rendering,
  L/R mappings, Auto actions, graph PDC, presets and backup compatibility.
  Evidence: `final-feature-regression.json` and `audio/analysis.json`.
- Consistency guard passed: 60 Markdown documents, 32 current and 28 historical;
  136 own-source files inventoried. Current local links, package versions and
  stale RNNoise capability patterns passed. Source-dependent claims received
  targeted semantic review; this does not prove every pre-existing comment.
- UTF-8 integrity and patch whitespace checks passed. Published 4.5.0 release
  records/body bytes and prior CHANGELOG/ReleaseNote entries remain unchanged.
- Windows candidate ZIP prepared and independently inspected: three AMD64
  binaries at file/product version 4.5.1 plus matching VST3 metadata, four payload
  files total. ZIP payload bytes match staging and individual source identities.
  Windows signatures remain NotSigned, consistent with the existing policy.
- ZIP: `build/noise-lr-release-evidence/DirectPipe-v4.5.1-Windows.zip`.
  SHA-256: `3ec5f3332a94fa4223ee69fe11ce9e094fa6ab21535e824e7fd87adbd0b57362`.
- Canonical metadata checks passed in `tools/check_release_consistency.py`.
  The original Bash version-only wrapper could not start because the sandbox
  denied MSYS named-object initialization; its failure is retained separately.

### Full-suite result and unresolved environment gates

CTest registered 791 tests. The initial full run could not connect an owned
socket; it was stopped rather than waiting on every network test. A separate
run explicitly excluded 12 WebSocket/HTTP socket tests and executed 779:
**760 passed, 2 skipped, 17 failed** in 353.41 seconds. Preserve the original
`ctest-nonnetwork.xml`/log; it is not a passing full-suite result.

Two ACK-write failures were reproduced as overly long fixture TEMP paths and
passed unchanged with a shorter workspace-owned TEMP (`short-temp-ack-tests.json`).
The other 15 failures remain blocked/unverified: two autostart registry writes,
four desktop-launch/paired-startup cases, one Windows PowerShell Archive-module
load, and eight Restart Manager file-use inspections returning error 29. No
production checks, permissions or test assertions were relaxed to make them pass.

Stream Deck tests: 27/28 passed; the owned WebSocket case failed with EACCES.
API parser tests: 8/9 passed; the owned TCP case failed with EACCES. JavaScript
syntax lint and 22 icon-reference checks passed. Full/production npm audits
failed with registry DNS ENOTFOUND. Official SDK validation, including a run
with schema updating disabled, was blocked by its manifest URL DNS check.
The Stream Deck package is therefore not declared validated/prepared.
Local JavaScript execution used Node 22.18.0 with the bundle targeting Node 20;
the exact Node 20 runtime gate remains part of CI verification.

GUI candidate execution approval timed out. The shell-started isolated copy
did not remain running, so no actual device/UI or listening acceptance is claimed.

## Superseded embedded-DPWIM checkpoint (excluded from release)

This section supersedes the earlier 349-test/four-file candidate checkpoint
above for the current source. Its old ZIP and hash are retained as prior
evidence, not the candidate to install.

- Corrected input bars to selected capture before Mono mixing/duplication,
  with gain/mute applied. One-sided pair input remains one-sided in INPUT;
  Mono audio/output duplication and scalar input API behavior are unchanged.
- Added manually selected Windows DPWIM 0.3.4 built-in, its existing editor,
  four-source mixer and `builtin_dpwim` preset/slot state. No Auto insertion,
  automatic migration or startup-chain insertion. Embedded updates ship with
  DirectPipe. Source hashes and adaptations: `build/dpwim-deferred/snapshot/thirdparty/dpwim/upstream.json`.
- Incremental Windows Release host and tests builds passed. The host was linked
  into `build/noise-lr-release-evidence/dpwim-candidate/bin/`, with project
  references disabled to avoid rebuilding/replacing the ordinary app target.
  Initial integration failures (Windows min/max macros and standalone updater
  assertions) are retained in logs; source-local Windows definitions and the
  embedded updater boundary resolved them without changing DSP assertions.
- Checkpoint feature/compatibility regression: **357 tests, 356 passed, 1 optional
  GForm VST2 skip, 0 failed**, 34.079 seconds. Five DPWIM tests include upstream
  DSP/FIFO/pitch/state regressions, Auto exclusion, graph dry-stereo rendering,
  bypass/PDC, state round-trip and in-process editor identity. Meter tests cover
  left-only/right-only pair input, selected mono, input gain/mute and Panic.
  `dpwim-candidate/feature-regression.json` and `.log` record the execution.
- This includes actual eight-rate RNNoise processing in the combined build;
  input/output analysis remains in `audio/analysis.json`. DPWIM captured packets
  in tests are synthetic and target an absent process; actual application
  capture, installed UI and physical listening are not accepted by these tests.
- Checkpoint consistency inventory: **64 Markdown files (36 current, 28 historical)**
  and **152 host/core/Receiver/vendored source files**. Links/version/stale-claim
  checks supplement targeted semantic review; they do not prove every comment.
- Superseded ZIP: `build/noise-lr-release-evidence/dpwim-candidate/DirectPipe-v4.5.1-Windows.zip`.
  SHA-256: `a956ae92e202ca0e06e653f0791994fe3f816b3cd56c084b0f7c322fc86f991b`.
  Seven exact payload files: three versioned AMD64 binaries, VST3 metadata,
  DPWIM GPL license, third-party notices and source attribution. ZIP payload
  hashes match staging; Windows signatures remain NotSigned. The checked-in
  Windows packaging job also stages these three attribution/license files;
  remote CI execution remains a separate gate.
- See [current external DPWIM setup](DPWIM_WINDOWS_SETUP.md) and
  [the expanded documentation audit](DOCUMENTATION_AUDIT_4_5_1.md).

The prior full-suite/environment gates below remain open. The current change
has not been committed, published, installed over the live app or attached to
an external issue.

## Remaining gates

- Re-run environment-blocked socket/registry/desktop/update cases in an
  authorized normal desktop/CI environment; complete candidate device/UI checks.
- Complete npm audits, full Stream Deck/API socket tests, official validation
  and Stream Deck package integrity with working DNS/loopback access.
- Exact-tag CI on Windows/macOS/Linux and public-download verification remain
  publication gates; no CI success or published artifact is claimed here.
- Real-device capability is distinct from synthetic signal tests. Physical
  listening, independent USB clocks and prolonged real-time sessions are not
  inferred from offline measurements.

The preparation checkpoint did not authorize publication. The owner has now
separately authorized release verification/publication and a post-publication
comment on issue #9 (OD-2026-11-13-0005/0006). Existing published releases and
the live audio session remain protected. Release execution evidence follows
below; preparation failures above are retained as historical local limits.


## Authorized release execution

The release remote is LiveTrack-X/DirectPipe (`directpipe`), whose main still
matches the local baseline bca4499 at the start of this execution. The connected
GitHub account can stage a CI-only branch despite the local CLI's invalid
authentication and network restrictions. Branch: `codex/release-4.5.1`.
Publication and an issue-success comment remain pending exact-source CI and
artifact verification. The user permits excluding DPWIM if integration cannot
be verified; do not publish documentation that claims an excluded built-in.

The pre-release module review found and fixed absent-app stale-PID restoration,
ADV parameter-attachment lifetime, and non-Windows preset backup misclassification.
New regressions cover all three; non-Windows execution is delegated to CI.
Local npm audits still fail with registry ENOTFOUND, and Bash cannot initialize
its MSYS named object. CI must run the original dependency/version/API/full-suite
checks; neither local failure is converted into a pass.


## Final DPWIM scope

The owner selected external VST use and then explicitly requested a Windows-only
Install DPWIM menu action. Embedded source and its candidate artifacts above are
superseded and are NOT part of the release. Its complete integration/review
snapshot is preserved locally in build/dpwim-deferred/snapshot. The final code
has three built-ins, with an optional verified external VST3 installer/updater
below their submenu. Installation is not automatic, requires a click, and does
not replace active instances, preset paths, VST2 copies or Auto contents.
Current documentation must follow DPWIM_WINDOWS_SETUP.md. Earlier DPWIM review
fixes belong to the deferred integration snapshot, not the published 0.3.4 VST.


## Final external-installer verification checkpoint

- Windows host/test Release builds pass with the external installer, no embedded
  DPWIM sources. Published DPWIM 0.3.4 ZIP SHA-256 matches the GitHub asset digest;
  its real metadata, AMD64 PE, numeric file version and product strings pass
  inspection and install into an owned temporary directory without plugin load.
- Broad current regression: **373 tests, 371 passed, 2 skipped, 0 failed** in
  37.087 seconds. Skips: opt-in live HTTPS installation (enabled in Windows CI)
  and optional isolated GForm VST2 fixture. Evidence:
  `build/release-451-remote/final-regression.json` / `.log`.
- Installer-targeted results: 9 passed, 1 explicit online skip. Includes cache
  preservation, no downgrade during concurrent installs, cancellation/owner
  lifetime and the exact published ZIP. Earlier failures in ZIP separator and
  JUCE product-version interpretation were corrected; original logs retained.
- Current source/document consistency: 62 Markdown files (34 current, 28
  historical), 138 source files; UTF-8, links/metadata, final-scope checks and
  diff whitespace pass. Review: PRE_RELEASE_REVIEW_4_5_1.md.
- No actual user plugin folder/device/active application was changed. Network
  runtime/full-platform checks and public assets remain pending CI execution.


### CI preflight correction

Commit 507651846d52062f607cd21957be53a0da618a88 was staged in PR #10.
Run 43358343218 reported success but only text/version checks actually ran:
the four build jobs inherited a skipped optional tag-preparation ancestor.
This is not a full validation pass. Explicit non-cancelled/text-success guards
were added to all four build jobs so CI-only/PR runs execute their complete
checks. The release run still requires all four validated artifacts.


### Cross-platform preflight and release orchestration

The complete macOS preflight ran 725 tests and exposed one stale HTTP
performance-latency assertion: RNNoise now correctly reports 1440 samples at
48 kHz, so the API fixture with two 480-sample device buffers must report
50 ms (20 ms device I/O + 30 ms active-chain PDC). Both expectations were
corrected in 71f78e5. The Linux compiler also required an explicit juce::int64
conversion of the release-asset size; that portability correction is in
241251a. Neither failed preflight is counted as a successful release gate.

Local shell and browser execution became unavailable because Windows execution
received incompatible Unix permission-root paths. Connected GitHub operations
remain available. A narrowly scoped, one-time main-push workflow dispatches
the existing exact-tag Build & Release workflow, waits for its full result,
and verifies the public release and all five nonempty assets. It does not
publish around a failed build. The owner additionally authorized removing
the three current non-main branches after publication; every recorded branch
head must remain unchanged and be an ancestor of main before any ref is removed.
The v4.5.1 source tag is retained. Hardware listening/device acceptance remains
distinct from synthetic and hosted-runner checks.
