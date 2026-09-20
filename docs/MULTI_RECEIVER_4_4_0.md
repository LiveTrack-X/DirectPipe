# DirectPipe 4.4.0 — independent Receivers and verification

**4.4.0 implementation and verification / 구현·검증 — 2026-09-20.** This document
describes the changes and separates dated software evidence from hardware
acceptance. Publication uses validated CI artifacts from the tagged source;
the exact CI run, published files and checksums are recorded on the
[release page](https://github.com/LiveTrack-X/DirectPipe/releases);
[Actions](https://github.com/LiveTrack-X/DirectPipe/actions) records CI results.

## User-visible scope

기존 A–E/Auto 슬롯형 UI와 OBS에 저장한 Receiver 설정을 유지합니다.
4.4.0 본체와 4.4.0 Receiver는 처리된 하나의 마이크 음성을 최대 8곳에서
독립적으로 받습니다. 한 Receiver의 뮤트·정지·재연결이 다른 Receiver의
수신 데이터를 소비하거나 막지 않도록 수신 버퍼를 분리합니다.
별도 OBS 확장, 가상 드라이버, 새 파라미터 화면은 추가하지 않습니다.

| Combination | Intended behavior |
| --- | --- |
| 4.4 host + 4.4 Receivers | Up to eight independent receivers using the additive transport, alongside the separate legacy connection |
| 4.4 host + older Receiver | Existing v1 endpoint retained; one older Receiver remains supported |
| Older host + 4.4 Receiver | Legacy fallback; old single-reader restriction still applies |
| New transport full/incompatible | Explain the waiting/error state; never silently join the legacy queue |

Plugin identifiers, parameter IDs (`mute`, `buffer`), saved state, buffer choices
and reported latency remain unchanged. Both ends still require matching sample
rates. Changing the host preset changes the shared processed sound for all
receivers. Independent per-destination effects are outside this update.

## Upgrading from 4.3.0

**The 4.3.0 updater replaces only the host.** After launching 4.4.0, open Windows
**Settings > Update Receiver...** to update existing Receiver installations too.
Review the exact paths and versions; use **Choose Folder...** for custom locations.
Close OBS or another using app yourself when prompted, then select **Retry**, or
defer with **Later**. The updater does not force-close those apps or replace a
same/newer Receiver. Receiver-only updates keep DirectPipe running; protected
destinations may require Windows administrator approval.

**4.3.0 자동 갱신은 본체만 교체합니다.** 4.4.0 실행 후 **Settings > Update
Receiver...**에서 Receiver도 갱신해야 독립 다중 수신을 사용할 수 있습니다.
사용 중인 OBS 등은 직접 종료하고 완료 후 다시 열어 버전과 소리를 확인하세요.
일반·등록 경로에서 발견되지 않는 기존 설치는 **Choose Folder...**로 지정합니다.

Closing a Receiver editor window does not release its slot. Unload the filter or
plugin, or close its containing application. A ninth Receiver retries after a
slot becomes free. The host's VST/IPC switch controls every Receiver; local Mute
controls only that Receiver.

## Transport and lifecycle

The additive mapping contains bounded per-reader SPSC queues. The v1 192-byte
header and its named endpoint stay unchanged. A slow receiver fills only its own
queue; the producer never waits for it or advances its read cursor. New consumers
wait for producer acknowledgement before reading a claimed queue. Dead-process
recovery must establish owner death, never infer death from an idle callback.

After queue overflow, the affected receiver discards stale backlog before resuming
fresh audio; previously recorded speech is not replayed as though it were current.
All mapping, liveness checks and reconnection work occur off the audio thread.

The previously verified 4.3.1 preset/audio/update improvements remain included;
their archived evidence is in [the 4.3.1 report](RECEIVER_RELIABILITY_UPDATE.md).
Those counts do not certify 4.4.0. The 4.3.1 candidate files remain preserved.

## Windows software checkpoint — 2026-09-20, before the path-alias fix

This completed local checkpoint includes the updater staging/path-length fix,
but precedes the later correction for Windows short (8.3) aliases versus long
path names. These results are historical evidence, not final-source validation.
Final-source platform CI results and published-asset evidence are recorded on
the Actions and release pages linked above.

| Check | Result |
| --- | --- |
| Full Windows Release build | Host, VST2/VST3 Receiver and test targets passed |
| Full CTest suite after the staging/path-length fix, before the alias fix | 654 registered: 652 passed, 2 skipped, 0 failed; 295.56 seconds |
| Stream Deck tests | 22/22 passed |
| Full and production npm audits | Zero findings |

The two skips remain `ActionDispatcherTest.ConcurrentDispatchFromMultipleThreads`
(requires a JUCE message loop) and `PlatformTest.MultiInstanceAlreadyHeld`
(the existing user host owns the application lock). Skipped cases are not counted
as passed. The staging/path-length regressions are included in this checkpoint's
full-suite total; the later alias regressions are not. These
software results do not mark physical-device, OBS frontend, listening or
interactive UAC acceptance complete.

## Windows software checkpoint — 2026-09-20, before the staging/path-length fix

The following completed checkpoint precedes the updater staging/path-length fix.
Its results are retained as dated evidence, not substituted for final-source
regression results. Focused counts are subsets of the full suite.

| Check | Result |
| --- | --- |
| Windows Release host, VST2/VST3 Receiver and tests | Build succeeded; binary versions 4.4.0 |
| Full CTest suite | 648 registered: 646 passed, 2 skipped, 0 failed; 155.98 seconds |
| Fan-out core, including separate Windows processes | 13/13 passed |
| Host writer, including legacy compatibility cases | 13/13 passed |
| Actual Receiver processor, old and new transport cases | 19/19 passed |
| Stream Deck | 22/22 tests; build, official validation and packing passed; 4.4.0.0 |
| Full and production npm audits | Zero findings |
| Version consistency, text integrity and diff whitespace | 14 version checks passed; text/diff checks passed |
| Checkpoint archives | ZIP CRC, component SHA-256 against that checkpoint's build, Stream Deck contents verified |

The focused C++ counts are subsets of the full suite, not additional tests.
`ActionDispatcherTest.ConcurrentDispatchFromMultipleThreads` requires a JUCE
message loop and was skipped. `PlatformTest.MultiInstanceAlreadyHeld` was skipped
because the user's running host already owns the application lock.

Coverage includes different callback sizes, mute isolation, a stalled/full
consumer, fresh-audio recovery, all eight slots, no legacy bypass at capacity,
attach/detach/reuse, proven process death, host replacement, the real connection
worker's legacy upgrade and pending acknowledgement, and initial buffer latency.
IPC tests use isolated names and never attach to the production audio mapping.

A separate review harness also passed eight concurrent readers receiving 51,200
identical stereo frames each (read sizes 1–257), idle-consumer overflow isolation,
and 10,000 claim/acknowledge/read/detach cycles during concurrent production.

## Additional isolated runtime checks — 2026-09-20

| Check | Observed result and boundary |
| --- | --- |
| Actual Receiver VST2 binary, independent native host | Eight actual plugin instances/callback threads; 344,696 process calls and 74,733,184 individual channel samples compared exactly, zero mismatches. Includes local mute, one stalled queue, slot reuse, producer replacement and callbacks larger than the prepare hint. Intentional startup/recovery fades were checked separately. |
| Actual OBS 32.0.1 audio core and VST filter | Eight simultaneous receivers, ninth-slot capacity, bypass/resume, unload/reload and saved-state restoration passed. Includes a three-minute continuous interval; 87,723 measured callbacks matched the expected signal/silence classification. |
| Actual DirectPipe scanner | Two portable scans exited successfully; VST2/VST3 identification passed, including the actual VST3 factory reporting 4.4.0 and two output channels. |
| Windows update helper process | Isolated exact-PID wait/restart, Receiver-only update, helper cancellation and late file-use detection cases passed. A separate long-path failure was reproduced and entered the final repair gate. |
| Linux/WSL2 POSIX core | Four isolated cases passed: real process stop/termination/reclaim, overflow isolation and producer replacement. This is core transport evidence, not a full Linux app or audio-device test. |

Receiver endpoints were redirected only inside the test process to unique names;
the on-disk Receiver binary was unchanged. OBS checks loaded its actual audio core
and VST filter, without operating the OBS frontend. Audio was synthetic and
scheduled by the harnesses. The update helper used disposable targets and a
non-audio stub for host wait/restart checks. Installed user software and settings
were not replaced. These results do not certify microphone/driver/speaker behavior,
independently clocked devices, subjective listening quality or interactive UAC.

The scanner's existing `Total scanned` log counter omits the last file of each
format. The resulting plugin XML, Found counts, exit codes and failed-file lists
were checked directly; the misleading counter was not used as evidence.

## Updater path corrections

The additional runtime checks reproduced a valid VST3 installation path becoming
unsupported when a long temporary suffix was appended. The updater now uses
short unique sibling names for staging and rotated prior backups, while retaining
the regular backup path. Before host shutdown or an elevation request, the same
helper runs a path preflight on the verified archive and every planned file and
directory location, including bundle resources and existing backups. Unsupported
paths fail with a shorter-folder message; the installation helper repeats the
checks before replacement. File-use, identity, hash and rollback protections stay
in place.

The updater also normalizes temporary and installation paths consistently when
Windows represents the same directory with a short (8.3) alias or a long name.
This prevents valid archive contents and installation paths from being rejected
because boundary checks compare different spellings of the same location.

Six actual-PowerShell regressions for the earlier staging/path-length fix passed,
including the reproduced 232-character
VST3 module path with an existing backup, and rejection of paths beyond Windows
file-use inspection's supported length. These precede the alias correction and do
not establish its result. These focused results do not replace the
final whole-tree build, regression suite or platform CI checks.

## Bounded transport cost

The default new mapping reserves approximately 1 MiB of PCM storage for eight
16,384-frame stereo queues. Only attached queues receive audio copies. A local
microbenchmark used 21 trials of 250,000 transactions with 128 stereo frames,
1,000 warmups, rotating case order and a retained checksum:

| Transport/readers | Median trial mean, microseconds | 95th percentile of trial means, microseconds |
| --- | ---: | ---: |
| Legacy / 1 | 0.029484 | 0.033764 |
| Independent / 1 | 0.031267 | 0.034458 |
| Independent / 2 | 0.058120 | 0.063176 |
| Independent / 4 | 0.117540 | 0.127875 |
| Independent / 8 | 0.266532 | 0.280371 |

These are in-memory write + all reads + checksum measurements on this PC,
not end-to-end audio latency or individual-callback p95. They exclude host
interleaving, concurrent legacy delivery, scheduling, OBS and audio hardware.
The results show bounded copy cost; they do not establish dropout-free operation.

## Remaining acceptance boundaries

The isolated checks do not replace OBS/DAW frontend and physical-device sessions,
long-duration listening with independent audio clocks, or the normal
close/update/reopen workflow with protected-folder UAC. Sample rates must match.
Platform build/CI results and physical hardware acceptance remain separate;
Windows evidence does not establish macOS/Linux plugin-host behavior.

The existing A–E/Auto workflow, preset/parameter formats and saved OBS instances
remain the compatibility boundary. Preset mutation is protected but may still
briefly produce silence; version 4.4.0 does not claim gap-free preset changes.

The earlier [documentation/comment audit](DOCUMENTATION_AUDIT_4_4_0.md) is a
historical documentation-only checkpoint. Its original counts, local artifact
paths and unchanged-token results are preserved; they do not certify the later
updater change or final release assets.
