# DirectPipe 4.5.0 verification record

Date: 2026-11-12 (Asia/Seoul)

This records software verification and its limits. Publication uses the exact
version tag through [Build & Release](https://github.com/LiveTrack-X/DirectPipe/actions/workflows/build.yml).
Public artifacts/checksums belong to the [4.5.0 release](https://github.com/LiveTrack-X/DirectPipe/releases/tag/v4.5.0);
the local artifacts below are independently packaged preparation evidence.

## Behavior and compatibility

- Existing stereo-pair capture remains the default. Opt-in single-channel
  capture exposes Analog 1, Analog 2, etc. and copies the selected input to
  internal L/R at unity before effects. Channel Mode keeps its existing meaning.
  Multiple independent microphone chains remain future work.
- Mono main-bus VST outputs feed both downstream stereo channels, including
  graph boundaries and final output. Later stereo output is preserved.
- Backup/settings errors identify the stage, reason and next check. Known
  incomplete rollback is reported; existing formats remain compatible.
- Windows mic-level hold is **default OFF**, fixed at 100 when enabled for the
  exact opened capture endpoint. Shared Windows Audio and Low Latency Mode are
  supported; ASIO/exclusive/DirectSound/other platforms do not enable it. It
  changes that microphone's system-wide master level, including changes made
  by other apps or the Windows slider. Mute, boost, session/output levels and
  internal DSP gain are unchanged. Driver maximum is not promised unity/0 dB.
  OFF stops correction without restoring a stale prior level.
- Mic choices are stored per endpoint in settings/full backups. Missing fields
  mean OFF; chain-only imports preserve existing choices. Strict validation
  precedes mutation, failed restores recover previous choices, and correction
  stays suspended through initial restore and incomplete rollback.

The exact ID comes from a minimal hash-guarded extension of pinned JUCE 7.0.12,
without default/name/audio-session inference. A guarded official JUCE build
backport also passes the macOS deployment target to its native bootstrap tool,
preventing a host-OS default from overriding the intended 10.15 minimum. The backend verifies active capture
identity before binding and writing. See [patch provenance](../tools/cmake/JuceEndpointIdentity.md)
and the [mic option guide](USER_GUIDE.md#windows-mic-level-hold).

## Local checks

| Check | Observed result |
| --- | --- |
| Windows Release build | App, Receiver VST2/VST3, tests and audio-free startup probe passed |
| Full CTest before the additional CI fixes | 770 registrations: 767 passed, 3 skipped, 0 failed; 347.85 seconds |
| Final engine/gain/state/mic/UI/autosave/backup focused run | 235/235 passed; 23.706 seconds |
| Official GForm VST2 host-graph rendering | 1.2 and 3.1 each passed with explicit DLL paths |
| JUCE endpoint extension guard | Clean, idempotent, mixed and unknown-source/no-partial-write passed |
| Windows ZIP extraction | All four hashes matched outside the checkout; three PE binaries AMD64/version 4.5.0/unsigned; no portable flag/probe |
| Frozen 4.3.0/4.4.0 installer compatibility | Install-only fixture passed; exact candidate EXE, original backup and settings preserved |
| Stream Deck Node tests | 28/28 passed; Node 20 and Node 22 verified |
| Stream Deck syntax/icons/official validation/pack | Passed; 22 action/category PNG references and 10 action identities retained |
| Full and production npm audits | 0 findings each; no audit suppression |
| API parser/accepted-response regressions | 9/9 passed, including owned ephemeral TCP fixture |
| Version/text/patch/structural checks | 15 version paths; UTF-8 integrity; clean patch; strict Doctor 0 errors/0 warnings |

CTest skipped concurrent-dispatch environment coverage, optional GForm discovery,
and an already-held multi-instance guard. Explicit GForm runs supply its missing
DLL coverage; the other two remain skipped. Mic tests use fake backends/devices
and never write real microphone levels. Failing mic/controller/UI/schema tests
preceded implementation. A premature UI expectation was corrected to wait for
the 50 ms debounce, separately asserting no synchronous UI volume write.

The exact newly built app API is a separate Windows CI gate. Its helper refuses
occupied HTTP/WebSocket ports, verifies both listener PIDs belong to its newly
created process, uses fresh portable fixtures, and only shuts down that process.
HTTP acceptance/resulting state and real WebSocket delivery are checked; protocol
failures fail the gate. Only recorded-sample progression may skip when the exact
candidate reports typed `audio_running=false` (initialized engine and a driver
reporting playback are required for true); control/file/API checks remain
mandatory. Default sample-rate values do not establish active audio. The installed 4.4.0 was rejected as evidence and was not interrupted.

## Update startup and recovery

The launching-process wait is bounded. Replacement, startup confirmation and
cleanup are separate outcomes. Initial restore/preload and saved window/tray
policy must finish without partial/loading/unstable state before acknowledgement.
The installer verifies token, version/path, actual process image, package hash
and 500 ms liveness, then saves ready proof before removing known previous EXEs.
Prior backups precede newest-backup deletion. Unconfirmed starts preserve recovery
files without rollback underneath a potentially running candidate. Finalization
reports startup, cleanup and completion-record failures separately.

Legacy flag-only cleanup uses the same ready boundary, matching version, strict
PE identity/hash and exclusive handles. Locks, unknown/changed files, reparse
paths and receipt/token conflicts prevent deletion. Date-suffixed manual backups
and settings are preserved. Consoleless Win32 probes exercise desktop launch/ACK/
rollback without JUCE/audio and are excluded from packages. See [recovery and
historical comparison](WINDOWS_UPDATE_RECOVERY.md) and [backup diagnostics](BACKUP_RESTORE_DIAGNOSTICS.md).

The first upgrade still uses the installed older updater; the new code cannot
retroactively replace it. Install-only comparison mechanically rendered frozen
4.3.0 code and confirmed relevant 4.4.0 functions match, without launching the
old app. The owner's historical restart failure remains unconfirmed. The saved
minimized-to-tray preference was inspected unchanged and does not prove its cause.

## Additional CI-discovered corrections

The first actual Windows API run exposed an obsolete test route, two gain-bound
failures, zero recording samples without a device, and indefinitely deferred
settings saves. Relative input gain now clamps to the documented 0..2 interval;
NaN/infinity retain the prior valid value and UI sync reflects the accepted gain.
Idle, unprepared audio now saves non-plugin controls/app preferences by merging
with a complete saved chain or using the recovery sidecar, while retaining a
pending full-chain save. Loading transitions still defer and existing plugin
states are preserved. Eight new native regressions first failed, then passed.
The master-bypass test now uses the documented toggle route and exact acceptance
proof. Only physical sample progression skips when explicit runtime state says
there is no playing audio; control, persistence and WAV creation stay mandatory.

macOS14 runner retirement required macOS15. Its SDK exposed missing deployment-
target forwarding to juceaide, repaired with the guarded upstream backport and
an inner-cache10.15 CI assertion. Linux dependency installation encountered
Azure mirror DNS failures, so CI uses official Ubuntu mirrors and bounded,
fail-on-index-error APT operations. Exact-source CI must independently revalidate
these build changes; a passed local check is not that result.

## Stream Deck dependencies

SDK 2.0.1, Stream Deck 6.9 minimum, Node 20 runtime, action identities and artwork
are preserved. Maintained esbuild replaces the old bundler; syntax checks preserve
the former zero-rule lint contract. A complete MIT utils 0.4.1 source copy has a
documented maintained Zod dependency and two explicit optional-field markers
preserving prior RPC error-data/response-result acceptance. Comparison tests
cover acceptance and bounded errors. Runtime license notices are packaged.
No finding is waived/hidden; Marketplace submission/review is separate.

## Local artifacts and evidence limits

| Local preparation artifact | SHA-256 |
| --- | --- |
| Windows ZIP, 12,600,506 bytes | 09f0603e71ff5068b173f390bff8c3f9852af92c10c1159ba737728f81b205ce |
| Host EXE, 13,660,672 bytes | 8e2f2642c7c4fb1d5c90a8941ff1bc80a5ed16d14c8a04e79fed843a3920cfec |
| Stream Deck package, 154,426 bytes | e01ec5e1a7e5de46faa4f8ccf970dfe7f464e4da1ad2209d540e84068feca447 |

Raw evidence: `build/release-4.5.0-publication-evidence/` and
`dist/release-4.5.0-publication-20261111/`; previous checkpoints are retained.
Local packages originate from reviewed release preparation based on 599ef9e
and subsequent scoped fixes; they are not
claimed byte-identical to hosted builds. SDK-generated VST3 moduleinfo has
trailing commas; version fields were checked as text, not strict JSON.

Tests/CI do not establish real-device mic-level correction, every driver/plugin,
listening quality, native backup chooser/private-backup acceptance, OBS frontend,
protected-folder interactive UAC, macOS/Linux hardware or owner acceptance.
Windows packages are unsigned unless the workflow records trusted signing.
Previous releases/tags/assets remain outside this change. Independent public
download/checksum/identity verification precedes the requested discussion reply.
