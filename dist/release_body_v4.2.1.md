## DirectPipe v4.2.1

v4.2.1 is a reliability hotfix for recording, real-time audio lifecycle,
settings and preset durability, Receiver reconnection, update integrity, and the
release pipeline.

v4.2.1은 녹음, 실시간 오디오 수명 주기, 설정·프리셋 내구성, Receiver
재연결, 업데이트 무결성, 릴리즈 파이프라인을 보강하는 안정성 핫픽스입니다.

The IPC wire ABI remains protocol v1: existing field offsets and the 192-byte
header are unchanged, while `producer_generation` uses previously reserved
bytes. Control API payloads, preset schema, Stream Deck action UUIDs/request
payloads, and plug-in identities remain compatible with the v4.2.x lane.

IPC wire ABI는 protocol v1을 유지합니다. 기존 필드 offset과 192-byte header는
그대로이며 `producer_generation`은 예약 영역을 사용합니다. control API payload,
preset schema, Stream Deck action UUID/request payload, 플러그인 식별자는
v4.2.x 호환성을 유지합니다.

---

### Highlights / 주요 변경

#### 1) Reliable recording and playback / 안정적인 녹음 및 재생

- A first recording now remains available to Play even when recording stops
  through an audio-device restart or loss path.
- 첫 녹음이 오디오 장치 재시작·손실 경로로 종료되어도 Play 대상으로
  유지됩니다.
- Playback follows the currently selected recording folder and no longer jumps
  back to a completed file from the previous folder.
- 재생 대상은 현재 선택한 녹음 폴더를 따르며 이전 폴더의 완료 파일로
  되돌아가지 않습니다.
- Recorder start/stop publication is serialized, dropped writer blocks no longer
  inflate duration, and rapid restarts create collision-free filenames.
- 녹음 start/stop 상태를 직렬화하고 writer가 받지 못한 블록은 시간에
  더하지 않으며 빠른 재녹음도 서로 다른 파일명을 사용합니다.

#### 2) Audio and VST lifecycle correctness / 오디오 및 VST 수명 주기 정합성

- Built-in noise removal now uses a callback-size-aware FIFO, preserves the
  declared 480-sample delay through large buffers, and reports zero latency when
  it is bypassed at unsupported sample rates.
- 내장 노이즈 제거 FIFO를 callback 크기에 맞게 구성해 큰 버퍼에서도
  선언한 480-sample 지연을 지키며, 미지원 sample rate passthrough에서는
  지연을 0으로 보고합니다.
- VST graph prepare/release and structural changes share one control-side
  synchronization boundary while the real-time render path remains lock-free.
- VST graph prepare/release와 구조 변경을 하나의 control-side 동기화 경계로
  보호하면서 실시간 render 경로는 lock-free로 유지합니다.

#### 3) Transactional reset, slots, and backups / 트랜잭션형 초기화·슬롯·백업

- Factory Reset and Clear invalidate stale asynchronous plug-in loads, leave the
  loading state on every exit, reset the live recording folder, and report
  filesystem failures.
- Factory Reset/Clear가 오래된 비동기 플러그인 로드를 무효화하고 모든
  종료 경로에서 loading 상태를 해제하며 현재 녹음 폴더와 파일 삭제 오류를
  올바르게 반영합니다.
- Autosave retains dirty state after a write failure. Slot switch, copy, export,
  rename, and active-slot import no longer continue after an unsafe save/replace
  failure; imports retain rollback state until the load succeeds.
- 자동 저장 실패 시 dirty 상태를 유지하며 슬롯 전환·복사·내보내기·이름
  변경·활성 슬롯 가져오기는 안전한 저장/교체가 실패하면 중단하고 성공할
  때까지 롤백 상태를 보존합니다.
- Full Backup includes recording-folder configuration, and import/restore errors
  are surfaced instead of being reported as complete success.
- Full Backup에 녹음 폴더 설정을 포함하고 import/restore 오류를 성공으로
  숨기지 않습니다.
- Preset and full-backup restore prepare every target plug-in chain before live
  state changes, then commit external state and swap the graph once with rollback
  protection.
- 프리셋과 전체 백업 복원은 모든 대상 플러그인 체인을 먼저 준비한 뒤 외부
  상태를 커밋하고 그래프를 한 번만 교체하며, 실패 시 롤백합니다.

#### 4) Receiver, state API, and updater hardening / Receiver·상태 API·업데이터 강화

- Receiver detects a replaced POSIX shared-memory object or producer generation,
  so an already loaded Receiver can reconnect without an IPC ABI change.
- Receiver가 교체된 POSIX 공유 메모리 객체 또는 producer generation을
  감지하므로 IPC ABI 변경 없이 이미 로드된 Receiver가 재연결할 수 있습니다.
- Mapping lifecycle and host latency notifications are moved out of the Receiver
  audio callback with an in-flight drain before unmap.
- Receiver audio callback 밖에서 mapping 수명과 host latency 알림을 처리하고
  unmap 전에 진행 중 callback을 배출합니다.
- Control state now publishes the active preset. Windows in-app updater downloads
  for v4.2.0 and later require an exact readable `checksums.sha256` entry and a
  matching SHA-256; missing, unreadable, or mismatched metadata fails closed.
  Manual browser downloads are outside this check, and macOS/Linux open the
  release page. Network/API failures are diagnosable.
- control state가 활성 preset을 게시합니다. Windows 인앱 업데이터는 v4.2.0
  이후 릴리즈에서 정확히 일치하는 `checksums.sha256` 항목과 SHA-256을
  필수 확인하며, 없거나 읽지 못하거나 불일치하면 fail-closed로 중단합니다.
  브라우저 수동 다운로드는 이 검사 밖이며 macOS/Linux는 릴리즈 페이지를
  엽니다. 네트워크/API 실패도 진단할 수 있습니다.
- MIDI learn completion runs on the message thread; superseded, timed-out, or
  shutdown learn sessions cannot apply stale bindings.
- MIDI learn 완료는 message thread에서 처리하며 교체·timeout·shutdown된 세션은
  오래된 binding을 적용할 수 없습니다.

#### 5) Release pipeline safety / 릴리즈 파이프라인 안전성

- CI validates and builds all artifacts before making a GitHub Release public.
  A failure leaves no assetless public latest release.
- CI가 모든 산출물을 검증·빌드한 뒤 GitHub Release를 공개하므로 실패 시
  asset 없는 public latest 릴리즈가 남지 않습니다.
- Windows release jobs fail when the required VST2 or ASIO SDK material is absent
  instead of publishing a package that lacks advertised compatibility.
- Windows 릴리즈 job은 필수 VST2/ASIO SDK가 없으면 실패하며 광고한 호환성이
  빠진 패키지를 게시하지 않습니다.

---

### Upgrade Notes / 업그레이드 안내

- Existing v4.2.x presets and control clients remain schema compatible. Stream
  Deck action UUIDs and request payloads are unchanged; the existing
  `state.data.preset` field now carries the active slot. Older Receivers remain
  IPC ABI-compatible but do not gain the v4.2.1 reconnect/lifecycle fixes.
- 기존 v4.2.x preset과 control client는 schema 호환을 유지합니다. Stream Deck
  action UUID와 request payload는 그대로이며 기존 `state.data.preset` 필드에
  활성 슬롯 값이 채워집니다. 이전 Receiver도 IPC ABI 호환이지만 v4.2.1의
  재연결·수명 수정은 적용되지 않습니다.
- Updating the bundled Receiver is recommended to receive the reconnect and
  audio-thread lifecycle fixes, but the IPC layout is unchanged.
- 재연결 및 audio-thread 수명 수정 적용을 위해 번들 Receiver 업데이트를
  권장하지만 IPC 레이아웃은 변경되지 않았습니다.
- DirectPipe and Receiver still need the same sample rate in v4.2.x; real-time
  sample-rate conversion is not part of this hotfix.
- v4.2.x에서는 DirectPipe와 Receiver sample rate를 동일하게 맞춰야 하며
  실시간 sample-rate 변환은 이번 핫픽스 범위가 아닙니다.

---

### Validation / 검증

- The local Windows Release build passed for the app, Receiver VST2/VST3, and
  all three test executables.
- 로컬 Windows Release에서 앱, Receiver VST2/VST3, 테스트 실행 파일 3개 빌드가
  통과했습니다.
- CTest registered 484 tests: core 59/59 passed; host 421 passed and 2
  environment-dependent tests skipped out of 423; focused endpoint 2/2 passed;
  0 failed.
- CTest 등록 484개 중 core 59/59, host 423개 중 421개, endpoint 2/2가
  통과했고 host 환경 의존 테스트 2개가 skip되었으며 실패는 0개입니다.
- The event-signaled IPC regression passed 100/100 stress iterations. Stream
  Deck tests passed 5/5; its bundle build and package validation succeeded.
- event-signaled IPC 회귀 테스트는 100/100회 통과했습니다. Stream Deck은
  테스트 5/5, bundle build, package validation이 통과했습니다.
- Version-only metadata, UTF-8 text integrity, JSON/YAML parsing,
  `git diff --check`, and SDAD v3.2.2 strict Doctor gates passed.
- version-only metadata, UTF-8 text integrity, JSON/YAML parse,
  `git diff --check`, SDAD v3.2.2 strict Doctor가 통과했습니다.
- Real-device audio, third-party VST crash containment, macOS hardware, and
  Linux hardware checks were not run and remain separate evidence gates.
- 실기기 오디오, 제3자 VST crash containment, macOS/Linux 하드웨어 검증은
  수행하지 않았으며 별도 증거 게이트로 남습니다.

---

### Downloads / 다운로드

- `DirectPipe-v4.2.1-Windows.zip` — Windows stable artifact, CI-built.
- `DirectPipe-v4.2.1-macOS.dmg` — macOS beta artifact, CI-built.
- `DirectPipe-v4.2.1-Linux.tar.gz` — Linux experimental artifact, CI-built.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck 4.2.1 package.
- `checksums.sha256` — SHA-256 manifest generated after all artifacts are built.

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.0...v4.2.1

**전체 변경 비교**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.0...v4.2.1
