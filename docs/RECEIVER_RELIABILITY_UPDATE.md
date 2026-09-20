# v4.3.1 Receiver reliability and update candidate

> Archived 4.3.1 checkpoint. The subsequent 4.4.0 implementation is recorded in
> [its own report](MULTI_RECEIVER_4_4_0.md). Version, scope and test statements
> below describe the preserved 4.3.1 candidate, not the later source tree.

**Archived status: local candidate, not published.** At this checkpoint the source version was 4.3.1;
the published baseline at that time was [v4.3.0](https://github.com/LiveTrack-X/DirectPipe/releases/tag/v4.3.0).
This document records the scope and evidence for the candidate. It does not
announce a release or imply that the running installation has been replaced.

**보존된 당시 상태: 공개되지 않은 로컬 후보.** 당시 소스 버전은 4.3.1이며 공개 기준판은
v4.3.0입니다. 현재 실행 중인 DirectPipe나 OBS 설치를 변경했다는 의미가 아닙니다.

기존 결정 기록과 설계·변경 이력을 확인한 결과, A–E/Auto, 최신 버전에서는 조용한
시작, 다운그레이드 금지, 같은 샘플레이트 사용은 유지할 의도가 있는 정책입니다.
반면 데이터가 충분한데 Receiver가 무음을 만드는 경우와 프리셋 상태 복원 중
오디오 처리가 겹치는 경우는 재현 테스트로 확인한 결함이어서 수정했습니다.
모든 과거 설계 선택을 사용자의 직접 지시로 간주하지는 않았습니다.

## Existing intent and scope

The review checked earlier implementation intent before changing behavior:

- Preset preloading and **Keep-Old-Until-Ready** already existed. The old chain
  still processes audio while external plugins are constructed in the background.
  This update fixes the exclusion around state restoration and graph changes;
  it does not introduce a new preset system or dual-graph crossfade.
- The earlier round-two audit correctly identified JUCE suspension as a boolean,
  not a counter. Repeated public `suspendProcessing(true)` calls remain
  idempotent. Nesting belongs to a separate scoped guard, so an inner rebuild
  cannot prematurely reopen processing during an outer state restore.
- Earlier Receiver work aimed to reduce drift-related clicks and retain stale
  connection recovery. The candidate preserves those purposes while correcting
  full-buffer reads that could be shortened, callback truncation, and stale audio
  replay after local mute.
- Routine startup remains quiet when the host is current or ahead of the public
  release. Receiver upkeep is an explicit Settings action. No downgrade is
  permitted.

A-E/Auto slots, preset/settings schemas, plugin and parameter IDs, Receiver
buffer choices and reported latency, IPC v1, and the existing multiple-reader
warning/allow behavior remain unchanged. The IPC ring buffer is still SPSC:
use one Receiver reader per stream. Multiple simultaneous readers remain a
known limitation. Sample rates must still match; resampling is outside this work.

## Changed behavior

| Area | Candidate behavior |
| --- | --- |
| Preset/state safety | An atomic admission gate excludes new render calls and the control thread drains admitted calls before state restoration or structural/lifecycle changes. Rejected callbacks emit silence; the audio callback does not wait on an ordinary mutex. Scoped cleanup resumes processing after normal and exceptional exits. |
| Receiver reads | High-water trimming retains at least the actual callback length. Low-water handling never shortens a callback when enough frames are available. Callbacks larger than the preparation hint use preallocated scratch chunks. |
| Receiver mute | Local Mute immediately outputs silence while continuing to drain queued audio, so unmute does not replay speech from the muted interval. |
| Receiver transitions | Partial/full underruns, trimming and reconnect use 64-sample transitions joined to the last emitted sample. Transition state persists across short callbacks. Normal steady-state samples remain unchanged. |
| Windows Receiver upkeep | **Settings > Update Receiver...** checks existing VST2/VST3 installations, accepts a custom folder, and previews exact destination paths and versions before installation. Same/newer versions are retained. |
| In-use plugins | If OBS or another app holds a target, the dialog offers **Retry / Later**. Close the indicated app manually before retrying. The updater never kills OBS or another plugin host. |
| Installation | The installer checks the package checksum and staged product/version/format identities, replaces complete VST3 bundles, and retains backups with rollback on failure. Protected paths may require Windows administrator approval. Receiver-only upkeep leaves DirectPipe running. |

프리셋 변경 중에는 처리 중인 callback이 끝날 때까지 제어 스레드에서 기다린 뒤
상태를 복원합니다. 이 구간의 새 callback은 무음을 출력하므로 **무중단 전환을
보장하지 않습니다**. 플러그인이 callback에서 영구적으로 멈추면 이 대기도 끝나지
않을 수 있습니다. Receiver의 실제 데이터 부족이나 서로 다른 장치의 clock drift도
이 변경만으로 사라지지는 않습니다.

## Windows update steps

1. Open **Settings > Update Receiver...**. Standard and registered plugin paths
   are inspected. Use **Choose Folder...** for a custom installation location.
2. Review the exact Receiver paths and installed-to-release versions. Copies in
   build/package output directories are excluded from automatic discovery;
   missing, unknown or uninspectable installations are reported rather than
   guessed. Renamed plugin files may require manual maintenance.
3. Choose **Update**. If an in-use warning appears, finish the current session,
   close OBS or the identified app yourself, then choose **Retry**. **Later**
   leaves the installation unchanged.
4. Allow the Windows administrator prompt if a protected destination requires it.
   A Receiver-only operation keeps DirectPipe running. A combined host update
   follows the existing host restart flow.
5. Reopen the plugin host and check the Receiver version and audio. File-install
   completion does not by itself verify that OBS loaded the intended copy or that
   audio works with the chosen devices.

Settings에서 명시적으로 시작하며, 시작할 때 Receiver 경고를 추가하지 않습니다.
OBS를 자동 종료하지 않고, 사용자가 종료한 뒤 Retry로 이어갑니다. 호스트가 최신이어도
오래된 Receiver만 갱신할 수 있으며, 더 최신인 설치본은 덮어쓰지 않습니다.

## Candidate evidence and remaining checks

| Check | Evidence/status |
| --- | --- |
| Preset regression before fix | The actual `VSTChain::processBlock` path processed a stub plugin while suspended and overlapped processing with state restoration. New regressions failed against the prior implementation. No audio device was opened. |
| Preset regression after fix | 28/28 VSTChain tests passed in a standalone build linked with the current source, including suspension/resume, in-flight drain, nested scopes, boolean compatibility, exception/Windows SEH cleanup and lifecycle cases. |
| Receiver regression | 11/11 actual-processor tests passed, including reconnect during a fade and channel/chunk boundaries. The first nine reproduced eight failures before the fix. These tests use isolated memory, never the production named IPC stream. |
| Integrated candidate validation | Final Windows Release build succeeded. CTest registered 618 tests: 616 passed, 2 skipped, 0 failed. This includes all 28 VSTChain, 11 Receiver processor and 15 Receiver discovery/update tests. Updater fixtures exercise Receiver-only and paired installs, in-use targets, complete VST3 rollback and changed/tampered files in temporary directories. |
| Stream Deck and package | 22/22 Stream Deck tests passed; full and production npm audits each report zero findings. Build, official plugin validation/packing and all 14 version checks passed. Both candidate archives passed CRC checks; packaged binaries and runtime bundle match the final build. |
| Manual acceptance | Real-device listening, long-running OBS sessions, installed-plugin reload, protected-directory/UAC interaction and cancellation remain to be exercised on an agreed test setup. No running user audio setup was stopped for these tests. |

The two skipped cases are `ActionDispatcherTest.ConcurrentDispatchFromMultipleThreads`
and `PlatformTest.MultiInstanceAlreadyHeld`; the latter cannot claim the production
single-instance lock while the user's DirectPipe is running. They are not counted
as passed. Raw results are retained in the local candidate's `evidence/` directory.

The candidate is in `dist/release-4.3.1-local-20260920/`. Its Windows archive
SHA-256 is `F263073FB7895681CB6435A85077DF9160FF4220FBC109AEB9F47538F6A40240`.
`LOCAL-INSTALL.md` describes the prepared, unexecuted installer for this PC.
The running host was verified as 4.3.0 despite its `DirectPipe-v3.7.0-win64`
folder name; the standard installed VST2 Receiver was verified as 4.2.1.
Their installed file hashes remain unchanged. Online updates still use the
published release, so they cannot fetch this unpublished 4.3.1 candidate.

This evidence supports the specific software fixes. It does not establish
gap-free preset changes, elimination of genuine underruns, third-party plugin
hang/crash containment, or hardware acceptance on Windows, macOS or Linux.

Mute freshness assumes the plugin host continues calling the Receiver. A host
that suspends callbacks entirely without releasing/preparing the plugin is a
separate backlog case. File verification and rollback also do not claim an
atomic transaction against every possible concurrent third-party installer.

Development-only lockfile updates address the published
[brace-expansion advisory](https://github.com/advisories/GHSA-rgw5-rvv9-x895) and
[sharp advisory](https://github.com/advisories/GHSA-rgj7-g3m4-5g8c). They do not
change the generated Stream Deck runtime bundle. Full/production npm audits
both report zero findings after the refresh.

## Implementation references

- [VSTChain](../host/Source/Audio/VSTChain.cpp),
  [PresetManager](../host/Source/UI/PresetManager.cpp),
  [VSTChain regressions](../tests/test_vst_chain.cpp)
- [Receiver processor](../plugins/receiver/Source/PluginProcessor.cpp),
  [actual Receiver regressions](../tests/test_receiver_processor.cpp)
- [UpdateChecker](../host/Source/UI/UpdateChecker.cpp),
  [Receiver discovery/support](../host/Source/UI/ReceiverUpdateSupport.cpp),
  [Windows update script](../host/Source/UI/UpdateScript.cpp)
- [User guide](USER_GUIDE.md), [build/test guide](BUILDING.md),
  [historical changelog](../CHANGELOG.md)
