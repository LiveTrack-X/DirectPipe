## DirectPipe v4.3.1

**LOCAL CANDIDATE — NOT PUBLISHED / 로컬 후보 — 미공개**

This is a draft for local validation, not a public release announcement.
The published baseline remains v4.3.0. No v4.3.1 public assets or exact-tag CI
results are claimed here. / 로컬 검증용 초안이며 공개 릴리즈가 아닙니다.

### Changes / 변경 내용

- Preset state restoration and graph changes now exclude concurrent audio
  processing using an atomic admission gate and control-thread drain. Existing
  preloading remains. A silence interval is still possible; no fixed switch
  time or gap-free guarantee is made.
- Receiver trimming keeps enough data for the actual callback, fully available
  low-water blocks are not shortened, and larger callbacks use preallocated
  chunks. Local Mute drains old speech. Underrun/trim/reconnect transitions join
  the last emitted sample over 64 samples.
- Windows **Settings > Update Receiver...** finds installed VST2/VST3 copies,
  supports a custom folder through **Choose Folder...**, and shows exact paths
  and versions before updating. Same/newer copies are retained.
- When OBS or another app holds a target, close it manually and choose
  **Retry**, or select **Later**. The updater does not kill other apps.
  Receiver-only upkeep leaves DirectPipe running and adds no startup alerts.
- Package checksum and staged identity checks, full VST3 bundle replacement,
  backups and rollback protect the install. Protected folders can require UAC.

프리셋 상태 변경과 오디오 처리의 겹침을 막고 Receiver의 불필요한 무음·데이터
잘림·뮤트 후 이전 음성 재생을 수정했습니다. Windows Settings에서 설치된 Receiver의
정확한 경로를 확인하고 갱신할 수 있습니다. OBS는 직접 종료한 뒤 Retry로 이어가며,
Receiver만 갱신할 때 DirectPipe는 계속 실행됩니다. 다운그레이드는 하지 않습니다.

### Compatibility / 호환성

A-E/Auto, saved settings/presets, plugin and parameter IDs, Receiver buffer
choices/latency and IPC v1 are unchanged. Use matching sample rates and one
Receiver reader per IPC stream. Resampling, a new reader-ownership protocol and
dual-graph crossfade are outside this candidate.

### Validation status / 검증 상태

The final Windows Release build succeeded. CTest registered 618 tests:
616 passed, 2 skipped, 0 failed, including 28 preset/VSTChain, 11 actual Receiver
processor and 15 Receiver discovery/update tests. Processor tests use isolated
memory; updater fixtures use temporary installations. The two skips are
`ActionDispatcherTest.ConcurrentDispatchFromMultipleThreads` and
`PlatformTest.MultiInstanceAlreadyHeld`.

Stream Deck tests passed 22/22; full and production npm audits report zero
findings. Official plugin validation/packing and all 14 version checks passed.
Both candidate archives passed CRC checks and their binaries match the final
build. These are local candidate results, not published or exact-tag CI results.

Real-device listening, long-duration OBS use, installed plugin reload and
interactive protected-folder/UAC acceptance are still pending. Successful file
installation alone does not prove live audio acceptance.

See `docs/RECEIVER_RELIABILITY_UPDATE.md` and `CHANGELOG.md` for scope and
validation details. / 실제 장치·OBS·UAC 검증은 아직 별도 확인이 필요합니다.
