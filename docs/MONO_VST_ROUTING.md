# Mono VST routing verification

Date: 2026-11-11
Status: Local source correction; not a published release or hardware acceptance
Baseline: 599ef9e8ac8a3b19cb9cd7a1aa060f321b3437f3

## Defect and correction

[Discussion 8](https://github.com/LiveTrack-X/DirectPipe/discussions/8) reports
left-only final output with GForm VST2, while bypass restores both channels.
The serial graph previously attempted channel 0 and 1 at every processor.
JUCE rejects channel 1 on a mono bus. The rejected final connection could also
skip the synchronous rebuild, temporarily retaining the earlier render graph.

`VSTChain::rebuildGraph()` now connects current main audio buses, excludes
auxiliary buses, and duplicates a one-channel output into both inputs of a
following stereo stage or final output. Stereo output remains distinct,
including stereo generated after a mono effect. All edits finish with one
explicit graph rebuild, including zero-output stages.

Input Mono still averages selected L/R before effects. A mono-input effect
still takes channel 0; it does not sum two identical inputs at double gain.
Saved input modes, device/channel settings, A-E/Auto presets, control identities
and output destinations retain their existing meanings. No persistence format,
Receiver/IPC protocol or Stream Deck action changed. Analog 1/2 independent
source support remains a separate future proposal, with explicit opt-in and
legacy-setting preservation required.

## Actual GForm reproduction

Official Windows x64 portable DLLs were downloaded into an isolated TEMP folder;
they were not installed or included in this repository. Sources:
[GForm](https://gvst.uk/Downloads/GForm),
[current archive](https://gvst.uk/Downloads/Get/GFormWin64.zip),
[legacy archive](https://gvst.uk/Downloads/Get/LegacyAllGVSTWin64.zip).

| Artifact | Version | DLL SHA-256 |
| --- | --- | --- |
| Current GForm | 3.1 | `a906717e7c236ea8438d57108b522e32e589f4f59ab15b86d7c10d91843778c1` |
| Legacy GForm | 1.2 | `f8f12ad894a08c7f89282e26a4fa26cc9186586f452072442c5f93c6a0a1973d` |

Both DLLs report native VST2 pin counts of 2-in/2-out, but return speaker
arrangement type Mono with a channel count of 2. JUCE interprets the Mono type
as one channel. Actual DirectPipe tests confirmed main and total layouts of
**1-in/1-out** for both versions, rather than relying on the scanned pin count.

The real `VSTChain`/JUCE wrapper rendered 51,200 frames of 0.2-amplitude,
220 Hz dual-mono sine input at 48 kHz, 512 frames per callback, factory settings.
Pending Windows graph updates were dispatched as in the normal message loop.

| Check | Before correction | After correction |
| --- | --- | --- |
| GForm 3.1 | L energy 1012.978602; R energy 0 | Both channels nonzero and matching |
| GForm 1.2 | L energy 1012.978602; R energy 0 | Both channels nonzero and matching |
| Equality requirement | Failed | Energy difference <= 0.000001; maximum sample difference < 0.000001 |

Native-ABI direct processing also produced equal L/R, supporting a host routing
cause rather than assuming GForm always erases its native right output.
The reporter's precise binary/settings were not supplied.

## Regressions and checks

Eight self-contained tests exercise actual graph rendering: mono final output;
stereo-to-mono-to-stereo; mono-input/stereo-output generation; ordinary stereo;
bypass/unbypass; main versus auxiliary buses; reprepare/reorder; zero-output
render commits. Six failed against the original builder; two stereo-preservation
checks already passed. All eight pass with the correction.

The optional actual-DLL test is
`VSTChainTest.GFormVst2KeepsBothOutputChannels`. Set
`DIRECTPIPE_TEST_GFORM_VST2` to an isolated official DLL and run the host tests
with that filter. It skips when the DLL or optional VST2 SDK is not provided;
the test does not download or install plug-ins. Test VST2 hosting now matches
the application's optional SDK setting.

- Windows Release application and host tests build successfully.
- Focused graph/engine/preset/settings run: 234 passed, 1 optional GForm test
  skipped. Separate actual-DLL runs passed for both versions above.
- Full Windows Release build passed. CTest: 665 registrations, 662 passed,
  3 skipped, 0 failed (235.02 seconds). Skips: concurrent-dispatch environment
  check, optional external GForm DLL, and already-held multi-instance check.
  The actual GForm test passed separately for both official DLL versions.
- UTF-8/text integrity, patch whitespace and strict SDAD Doctor checks pass.
- Independent source review found no actionable issue in routing, real-time
  boundaries, bypass, auxiliary-bus handling or the new regression coverage.

The counts above preserve the mono-fix checkpoint. After the subsequent backup
diagnostics change, the final combined build passed 669 CTest registrations
(666 passed, 3 skipped, 0 failed); both real GForm versions passed again.
See [`BACKUP_RESTORE_DIAGNOSTICS.md`](BACKUP_RESTORE_DIAGNOSTICS.md).

Local machine-readable evidence is under `build/discussion8-evidence/`:
`red-routing.json`, `red-gform-3.1.xml`, `red-gform-1.2.xml`,
`green-focused.json`, `green-gform-3.1.xml`, `green-gform-1.2.xml`, build logs
and CTest output. Native artifact metadata/probe scripts remain in the isolated
TEMP `directpipe-discussion8-gform-probe` folder.

## Limits

No physical audio endpoint, WASAPI/VB-Cable monitoring application or listening
session was used. This establishes the defect and correction through the real
software graph with two official GForm builds; it does not prove every plug-in,
parameter combination, hardware driver or cross-platform runtime. Existing
third-party process isolation and bypass-tail limitations are unchanged.
No installed software, published asset, tag or remote discussion was modified.
