## DirectPipe v4.2.0

v4.2.0 is a substantial reliability release covering Windows endpoint recovery, real-time lifecycle safety, transactional settings and preset handling, control-server shutdown, the Windows updater, and Stream Deck reconnect/discovery behavior.

v4.2.0은 Windows endpoint 복구, 실시간 처리 수명 안전성, 설정 및 프리셋 트랜잭션, 제어 서버 종료, Windows updater, Stream Deck 재연결/검색 동작을 함께 강화한 대규모 안정성 릴리즈입니다.

This release does not change the control API, preset schema, Stream Deck action schema, or Receiver IPC protocol.

이번 릴리즈는 control API, preset schema, Stream Deck action schema, Receiver IPC protocol을 변경하지 않습니다.

---

### Highlights / 주요 변경

#### 1) Windows selected-input endpoint recovery / Windows 선택 입력 endpoint 복구
- DirectPipe now listens for Windows Core Audio property/state notifications for the selected capture endpoint.
- DirectPipe는 이제 선택된 capture endpoint의 Windows Core Audio 속성/상태 변경 알림을 감지합니다.
- Changes to **Audio enhancements**, **Voice Clarity**, **Voice focus**, default format, or exclusive-mode properties can trigger a short same-device settle and reopen even when JUCE does not report `audioDeviceStopped()`.
- **오디오 향상 기능**, **Voice Clarity**, **음성 포커스**, 기본 형식, 독점 모드 속성 변경 시 JUCE가 `audioDeviceStopped()`를 보내지 않아도 짧은 안정화 후 같은 장치를 다시 엽니다.
- Recovery is endpoint-event based, not silence based. Physical microphone mute buttons and normal silence do not trigger a reopen.
- 복구는 무음 감지가 아니라 endpoint 이벤트 기반이므로 물리 마이크 뮤트와 정상적인 무음은 재오픈을 유발하지 않습니다.

#### 2) Audio and monitor lifecycle safety / 오디오 및 모니터 수명 안전성
- Saved input/output targets remain authoritative during fallback, same-device restart, zero-active-channel recovery, and ASIO cooldown paths.
- fallback, same-device restart, zero-active-channel 복구, ASIO cooldown 중에도 저장된 입력/출력 target을 기준으로 복구합니다.
- Manual and automatic device-loss mute ownership are tracked separately. Endpoint/panic recovery preserves user mute intent, while clearing manual mute cannot bypass an active automatic loss mute.
- 수동 mute와 장치 손실 자동 mute ownership을 분리해 추적합니다. endpoint/panic 복구는 사용자 mute 의도를 보존하고, 수동 mute를 해제해도 활성 자동 loss mute를 우회하지 않습니다.
- Output None selection or manual output-loss clearing publishes direction-first and rechecks/re-arms recovery, so a concurrent input loss remains visible in aggregate device state and keeps its recovery timer.
- Output None 선택 또는 수동 output-loss 해제는 direction-first로 상태를 게시하고 복구를 다시 확인/예약하므로, 동시에 발생한 input loss가 aggregate device state와 복구 timer에서 유지됩니다.
- Deferred monitor recovery/reinitialize work now uses lifecycle generations, so stale callbacks cannot close or reconfigure a newly selected monitor device.
- 지연된 모니터 복구/재초기화 작업은 lifecycle generation을 검사하여 오래된 callback이 새 모니터 장치를 닫거나 되돌리지 못하게 합니다.
- Monitor and shared-memory writers close real-time admission and drain in-flight operations before resetting ring storage or unmapping IPC memory.
- 모니터와 shared-memory writer는 ring reset 또는 IPC unmap 전에 실시간 write admission을 닫고 진행 중 작업을 모두 배출합니다.

#### 3) Control-server and shutdown hardening / 제어 서버 및 종료 안정화
- WebSocket clients become broadcast-visible only after handshake/initial-state completion; queued state is re-snapshotted before send, and action parameters are type/range-strict.
- WebSocket client는 handshake/initial-state 완료 후에만 broadcast 대상이 되며, queued state를 전송 전에 다시 snapshot하고 action parameter 타입/범위를 엄격히 검사합니다.
- Failed handshakes and dead clients are swept even while idle; shared connection ownership and close-before-join shutdown make reclamation deterministic.
- 실패한 handshake와 dead client는 idle 상태에서도 sweep하며, shared connection ownership과 close-before-join 종료로 확실히 회수합니다.
- HTTP client sockets use shared lifetime ownership during shutdown/restart, request fields are validated before integer conversion, and plugin state responses use a stable snapshot.
- HTTP client socket은 종료/재시작 중 shared lifetime을 사용하며, request 숫자 필드를 변환 전에 검증하고 plugin state 응답은 안정된 snapshot을 사용합니다.
- The host announces its actual WebSocket port immediately and every 2 seconds; Stream Deck binds UDP discovery before its first WebSocket attempt, records that port, clears stale socket state, and avoids reconnect races after close/error events.
- host는 실제 WebSocket port를 시작 즉시와 2초마다 알리고, Stream Deck은 첫 WebSocket 연결 전에 UDP discovery를 bind하여 해당 port를 사용하며 stale socket state와 close/error 이후 재연결 경쟁을 정리합니다.
- The Stream Deck package updates production `ws` to 8.21.0 and refreshes transitive dependencies; full and production-only `npm audit` report 0 vulnerabilities.
- Stream Deck package의 production `ws`를 8.21.0으로 올리고 transitive dependency를 갱신했으며, 전체/production-only `npm audit` 모두 취약점 0건입니다.

#### 4) Transactional settings, presets, and full backups / 설정·프리셋·전체 백업 트랜잭션
- Settings-only and full-backup imports prevalidate audio, control, MIDI, and slot payloads before mutation, then roll back touched state/files on late failure. Restore atomically claims the single load owner and rejects another active load, a partial chain, or an unstable VST transition; full restore requires explicit stable-runtime proof.
- Settings-only 및 full-backup import는 변경 전에 audio, control, MIDI, slot payload를 검증하고, 후반 실패 시 변경한 상태와 파일을 롤백합니다. Restore는 단일 load ownership을 원자적으로 획득하고 다른 load, partial chain, unstable VST transition 중에는 시작하지 않으며 full restore는 명시적 stable-runtime proof를 요구합니다.
- Parseable but structurally invalid slot JSON, including non-canonical Base64 plugin state, no longer overwrites valid slots or blocks recovery from a valid `.bak`/legacy backup; a valid backup can still load when primary repair-copy fails. Backup-only or locked-primary autosave recovery preserves an explicit `outputMuted` value, including during partial plugin loads.
- parse는 가능하지만 non-canonical Base64 plugin state 등 구조적으로 잘못된 slot JSON은 유효 slot을 덮어쓰거나 `.bak`/legacy backup 복구를 막지 않으며, primary repair-copy가 실패해도 유효 backup을 불러옵니다. primary가 없거나 잠긴 autosave family를 backup에서 복구할 때도 partial plugin load를 포함해 명시적 `outputMuted` 값을 보존합니다.
- Legacy numeric slot families with only `.bak` or `.backup` remaining now migrate to canonical letter slots, keeping them loadable and present in full backups.
- 기본 파일 없이 `.bak` 또는 `.backup`만 남은 구형 숫자 슬롯도 표준 문자 슬롯으로 마이그레이션되어, 불러오기와 전체 백업에서 누락되지 않습니다.
- Full-backup export refuses a loading/partial/unstable active chain and reports active-slot, control staging, or existing-slot corruption failures instead of overwriting a valid slot or silently producing an incomplete backup.
- full-backup export는 loading/partial/unstable active chain을 거부하고 active slot 저장, control staging, 기존 slot 손상 실패를 보고하여 유효 slot을 덮어쓰거나 불완전한 백업을 만들지 않습니다.
- Invalid optional action fields, actions, MIDI mapping types/ranges, ports, and boolean fields are rejected or skipped instead of becoming dangerous default actions or dead mappings.
- 잘못된 optional action field, action, MIDI mapping type/range, port, boolean 필드는 위험한 기본 action이나 동작하지 않는 mapping으로 바뀌지 않고 거부 또는 제외됩니다.

#### 5) Safer Windows updater / 더 안전한 Windows updater
- The updater stages and verifies the replacement before rotating the current executable, keeps a known-good backup until successful startup, and rolls back failed replacement moves.
- updater는 교체 파일을 staging/확인한 뒤 현재 실행 파일을 교체하고, 성공적인 다음 실행까지 known-good backup을 유지하며, 교체 이동 실패 시 롤백합니다.
- Generated batch paths preserve literal percent characters, release tags must be strict `MAJOR.MINOR.PATCH`, and the installer waits only for the exact DirectPipe process that launched it.
- 생성된 batch path의 literal `%`를 보존하고 release tag는 엄격한 `MAJOR.MINOR.PATCH`만 허용하며, installer는 자신을 실행한 정확한 DirectPipe PID만 기다립니다.
- Completed or failed download workers are reaped, so a later Update Now attempt can retry instead of remaining permanently "in progress"; a lifecycle mutex serializes worker reap/start/destruction around the reusable `std::thread`.
- 완료되거나 실패한 download worker를 회수하여 이후 Update Now를 영구적인 "진행 중" 상태 없이 다시 시도할 수 있으며, lifecycle mutex가 재사용되는 `std::thread`의 회수/시작/종료를 직렬화합니다.

#### 6) Release and validation gates / 릴리즈 및 검증 게이트
- Version checks now cover the app, bundled Receiver, Stream Deck manifest/package/package-lock, current changelog/release notes, and identical current/versioned release bodies.
- 버전 검사는 앱, 번들 Receiver, Stream Deck manifest/package/package-lock, 현재 changelog/release notes, current/versioned release body 동일성까지 확인합니다.
- CI runs Stream Deck tests and package validation, builds the dedicated endpoint-watcher tests, and verifies that an upload tag exactly matches the canonical source version.
- CI는 Stream Deck 테스트/패키지 검증과 endpoint-watcher 전용 테스트를 실행하고, upload tag가 canonical source version과 정확히 같은지 확인합니다.
- Manual release-tag dispatch checks out the requested tag, preventing artifacts from an unrelated branch/ref from being uploaded under that tag.
- 수동 release-tag dispatch는 요청 tag를 checkout하여 다른 branch/ref 산출물이 해당 tag 이름으로 업로드되는 일을 막습니다.

---

### Upgrade Notes / 업그레이드 안내
- **Windows v4.1.2 → v4.2.0: use the GitHub Windows ZIP for this upgrade.** The new transactional updater protects updates performed by v4.2.0 and later, but it cannot retroactively protect the updater code already installed in v4.1.2.
- **Windows v4.1.2 → v4.2.0은 이번 한 번 GitHub Windows ZIP으로 수동 설치를 권장합니다.** 새 transactional updater는 v4.2.0 이후 업데이트부터 보호하며, 이미 설치된 v4.1.2 updater 동작을 소급 변경할 수는 없습니다.
- **No Receiver VST replacement required for the host-side fixes**: the control API and Receiver IPC protocol are unchanged. The bundled Receiver remains version-aligned for new installs.
- **호스트 쪽 수정에 Receiver VST 교체 불필요**: control API와 Receiver IPC protocol은 동일합니다. 새 설치용 번들 Receiver는 버전에 맞춰 유지됩니다.
- **Update the Stream Deck plugin to 4.2.0** to receive the discovery/reconnect fixes. The GitHub `.streamDeckPlugin` asset can be installed manually; Marketplace distribution is a separate submission step.
- **검색/재연결 수정을 적용하려면 Stream Deck plugin을 4.2.0으로 업데이트하세요.** GitHub의 `.streamDeckPlugin` asset을 수동 설치할 수 있으며 Marketplace 배포는 별도 제출 단계입니다.
- **Release assets are built by CI**: the GitHub release is created notes-only, then GitHub Actions builds/uploads Windows, macOS, Linux, Stream Deck, and checksum assets.
- **릴리즈 산출물은 CI에서 빌드**: GitHub release는 notes-only로 만든 뒤 GitHub Actions가 Windows, macOS, Linux, Stream Deck, checksum 산출물을 빌드/업로드합니다.

---

### Validation / 검증
- Local Windows Release verification built the app, Receiver VST2/VST3, core tests, host tests, and the dedicated endpoint-watcher tests.
- 로컬 Windows Release 검증에서 앱, Receiver VST2/VST3, core/host 테스트, endpoint-watcher 전용 테스트를 빌드했습니다.
- `directpipe-tests`: 52 passed.
- `directpipe-tests`: 52개 통과.
- `directpipe-host-tests`: 391 tests; 389 passed, 2 environment-dependent tests skipped.
- `directpipe-host-tests`: 총 391개 중 389개 통과, 환경 의존 테스트 2개 skipped.
- `directpipe-endpoint-watcher-tests`: 2 passed.
- `directpipe-endpoint-watcher-tests`: 2개 통과.
- CTest registered 445 tests: 0 failed, 2 environment-dependent tests skipped.
- CTest 등록 테스트 445개 중 실패 0개, 환경 의존 테스트 2개가 skipped 되었습니다.
- Stream Deck tests (5/5), build, package validation, full/production-only `npm audit` (0 vulnerabilities), version-only metadata checks, UTF-8 text integrity, JSON validation, and `git diff --check` passed.
- Stream Deck 테스트(5/5), build, package validation, 전체/production-only `npm audit`(취약점 0건), version-only metadata, UTF-8 text integrity, JSON 검증, `git diff --check`가 통과했습니다.
- Focused endpoint automation passed; real-device **Audio enhancements**, **Voice Clarity**, **Voice focus**, and default-format changes were not executed in this local run and remain a manual field check.
- endpoint 집중 자동 테스트는 통과했지만 실제 장치의 **오디오 향상 기능**, **Voice Clarity**, **음성 포커스**, 기본 형식 변경은 이번 로컬 실행에서 수행하지 않아 수동 실기기 확인 항목으로 남습니다.
- Windows is the locally validated stable target. macOS remains beta and Linux remains experimental pending the release CI run and real-hardware coverage.
- Windows는 로컬 검증된 stable target입니다. macOS는 beta, Linux는 experimental이며 release CI와 실기기 검증 범위가 제한적입니다.

---

### Downloads / 다운로드
- `DirectPipe-v4.2.0-Windows.zip` — Windows stable artifact, CI-built.
- `DirectPipe-v4.2.0-Windows.zip` — Windows 안정 버전 산출물이며 CI에서 빌드합니다.
- `DirectPipe-v4.2.0-macOS.dmg` — macOS beta artifact, CI-built.
- `DirectPipe-v4.2.0-macOS.dmg` — macOS beta 산출물이며 CI에서 빌드합니다.
- `DirectPipe-v4.2.0-Linux.tar.gz` — Linux experimental artifact, CI-built.
- `DirectPipe-v4.2.0-Linux.tar.gz` — Linux experimental 산출물이며 CI에서 빌드합니다.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck 4.2.0 control package, CI-built.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck 4.2.0 제어 패키지이며 CI에서 빌드합니다.
- `checksums.sha256` — generated by CI for all uploaded assets.
- `checksums.sha256` — 업로드된 모든 산출물에 대해 CI에서 생성하는 checksum 파일입니다.

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.1.2...v4.2.0

**전체 변경 비교**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.1.2...v4.2.0
