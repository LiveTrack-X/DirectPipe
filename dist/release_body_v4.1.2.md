## DirectPipe v4.1.2

v4.1.2 is a host-side audio-device recovery and save/restore hardening hotfix after v4.1.1. It focuses on microphone recovery after Windows endpoint restarts, including sound-property changes such as toggling microphone enhancement features, strict ASIO startup restore, and preventing partial restore states from being saved as clean presets.

v4.1.2는 v4.1.1 이후의 호스트 쪽 오디오 장치 복구 및 저장/복원 안정화 핫픽스입니다. 마이크 향상 기능 끄기/켜기 같은 Windows 소리 속성 변경으로 endpoint가 재시작된 뒤의 마이크 복구, 엄격한 ASIO 시작 복원, 부분 복원 상태가 정상 preset으로 저장되지 않도록 하는 데 집중합니다.

This release does not change the control API, preset schema, Stream Deck action schema, or Receiver IPC protocol.

이번 릴리즈는 control API, preset schema, Stream Deck action schema, Receiver IPC protocol을 변경하지 않습니다.

---

### Highlights / 주요 변경

#### 1) Same-device microphone restart recovery / 같은 장치 마이크 재시작 복구
- Windows can restart the same microphone endpoint after a sound-property change without changing the device name.
- Windows는 소리 속성 변경 후 장치 이름을 바꾸지 않은 채 같은 마이크 endpoint를 재시작할 수 있습니다.
- DirectPipe now keeps that external same-device restart in a loss-pending state until it re-opens the same device setup after a short settle delay.
- DirectPipe는 이제 외부 same-device restart를 짧은 안정화 후 같은 장치 설정을 다시 열기 전까지 손실 대기 상태로 유지합니다.
- This replaces the manual workaround of switching to another input device and switching back.
- 이로써 다른 입력 장치로 갔다가 다시 돌아오는 수동 복구 동작을 코드가 대신 수행합니다.
- Input stays silent while the saved target is pending, preventing fallback mic capture and noisy stale input.
- 저장된 target이 대기 중인 동안 입력은 무음 처리되어 fallback 마이크 캡처와 꼬인 입력 노이즈를 막습니다.

#### 2) Faster reconnect after device-list changes / 장치 목록 변경 후 더 빠른 재연결
- Device-list change notifications now bypass the 3-second reconnect polling cooldown.
- 장치 목록 변경 알림은 이제 3초 재연결 폴링 쿨다운을 우회합니다.
- DirectPipe retries immediately when a lost microphone or output endpoint becomes visible again.
- 손실된 마이크 또는 출력 endpoint가 다시 보이면 DirectPipe가 즉시 재시도합니다.

#### 3) Safer stop/error handling / 더 안전한 stop/error 처리
- External audio device stop/error events now mark input lost, auto-mute output, and clear input/output meters immediately.
- 외부 오디오 장치 stop/error 이벤트는 이제 입력 손실, 출력 자동 뮤트, 입출력 meter 0 처리를 즉시 수행합니다.
- Reconnection no longer accepts current or fallback devices while startup restore, input loss, or output auto-mute has an explicit target pending.
- 시작 복원, 입력 손실, 출력 자동 뮤트가 명시 target을 기다리는 동안 재연결은 현재 장치나 fallback 장치를 성공으로 수락하지 않습니다.
- Manual input/output selection now clears only the matching loss state and preserves the opposite side's pending target.
- 수동 입력/출력 선택은 이제 해당 방향의 손실 상태만 해제하고 반대쪽의 대기 중인 target은 보존합니다.
- Delayed same-device re-open callbacks are canceled when the user changes driver/device type or ASIO device.
- 지연된 same-device 재오픈 callback은 사용자가 driver/device type 또는 ASIO 장치를 변경하면 취소됩니다.
- Device-list reconnect notifications are ignored while an intentional device reconfiguration is already in progress.
- 의도적인 장치 재설정이 이미 진행 중이면 장치 목록 재연결 알림은 무시됩니다.

#### 4) Strict ASIO startup restore / 엄격한 ASIO 시작 복원
- Saved ASIO restore now treats input/output as one duplex ASIO driver.
- 저장된 ASIO 복원은 이제 입력/출력을 하나의 duplex ASIO 드라이버로 취급합니다.
- If a preset contains different ASIO input/output names, DirectPipe normalizes them to one ASIO device instead of applying a crossed pair.
- preset에 서로 다른 ASIO input/output 이름이 들어 있어도 DirectPipe는 교차 조합을 적용하지 않고 하나의 ASIO 장치로 정규화합니다.
- A saved ASIO target no longer falls through to other ASIO drivers such as FL Studio ASIO or Realtek ASIO when the saved device is missing at startup.
- 시작 시 저장된 ASIO 장치가 없을 때 저장 target이 FL Studio ASIO나 Realtek ASIO 같은 다른 ASIO 드라이버로 넘어가지 않습니다.
- During reconnect, DirectPipe scans the desired ASIO driver type first and switches back only after the saved ASIO device is visible.
- 재연결 중 DirectPipe는 먼저 원하는 ASIO 드라이버 타입을 스캔하고 저장된 ASIO 장치가 보일 때만 다시 전환합니다.
- ASIO sample-rate, buffer-size, and channel-mask restore are delayed until the saved ASIO device is actually active.
- ASIO sample-rate, buffer-size, channel-mask 복원은 저장된 ASIO 장치가 실제로 활성화된 뒤에만 적용됩니다.

#### 5) Save/restore partial-failure guards / 저장 및 복원 부분 실패 보호
- Plugin-chain import now returns failure when requested VST entries cannot be loaded, so partial restores are not treated as clean preset loads.
- Plugin-chain import는 요청된 VST 항목을 로드할 수 없으면 실패를 반환하므로, 부분 복원이 정상 preset load처럼 처리되지 않습니다.
- Malformed plugin-chain JSON is rejected before active slot or audio state is applied.
- 잘못된 plugin-chain JSON은 active slot 또는 audio state 적용 전에 거부됩니다.
- Startup autosave now marks partial load after an incomplete settings preset load, preserving quick-slot files from incomplete chain autosaves.
- 시작 autosave는 불완전한 settings preset load 뒤 partial load를 표시하여, 불완전한 chain autosave가 quick-slot 파일을 덮어쓰지 않게 합니다.
- Settings-only and full-backup imports prevalidate audio, control, and slot payloads before applying dependent sections.
- Settings-only 및 full-backup import는 종속 section을 적용하기 전에 audio, control, slot payload를 먼저 검증합니다.
- Full-backup slot-file restore now rolls back touched quick-slot files if the exact clear/write phase fails.
- Full-backup slot-file restore는 정확한 clear/write 단계가 실패하면 건드린 quick-slot 파일을 이전 상태로 롤백합니다.
- Plugin preload cancellation now uses shared-state cleanup with a bounded message-thread wait, avoiding indefinite shutdown waits if a background preload is stuck.
- Plugin preload cancellation은 이제 shared-state cleanup과 제한된 message-thread 대기를 사용하여, background preload가 멈췄을 때 shutdown이 무기한 기다리지 않게 합니다.

#### 6) Windows auto-start Run entry / Windows 자동 시작 Run 등록
- Windows auto-start now writes the HKCU Run command with the executable path quoted.
- Windows 자동 시작은 이제 HKCU Run command에 실행 파일 경로를 따옴표로 감싸서 저장합니다.
- This prevents reboot-start failures when DirectPipe is installed under a path with spaces, such as `Program Files`.
- 이로써 DirectPipe가 `Program Files`처럼 공백이 있는 경로에 설치됐을 때 재부팅 후 시작하지 못하는 문제를 막습니다.

#### 7) Version and docs sync / 버전 및 문서 동기화
- App metadata, bundled Receiver metadata, Stream Deck metadata, README, user guide, product spec, architecture notes, log rules, building docs, changelog, and release body are aligned to v4.1.2.
- 앱 메타데이터, 번들 Receiver 메타데이터, Stream Deck 메타데이터, README, 사용자 가이드, 제품 스펙, 아키텍처 노트, 로그 규칙, 빌드 문서, 체인지로그, 릴리즈 본문을 v4.1.2 기준으로 맞췄습니다.

---

### Upgrade Notes / 업그레이드 안내
- **No API/state model break / API 및 상태 모델 호환 유지**: Stream Deck, HTTP, WebSocket, presets, settings backups, and full backups remain compatible.
- **No API/state model break / API 및 상태 모델 호환 유지**: Stream Deck, HTTP, WebSocket, presets, settings backups, full backups는 계속 호환됩니다.
- **No Receiver VST replacement required for these host-side fixes**: existing Receiver VST installs do not need manual replacement to benefit from the host-side audio-device recovery and save/restore hardening fixes. The bundled Receiver plugin remains version-aligned for new installs.
- **No Receiver VST replacement required for these host-side fixes / 호스트 쪽 수정에 Receiver VST 교체 불필요**: 기존 Receiver VST 설치본은 호스트 쪽 오디오 장치 복구 및 저장/복원 안정화 수정 효과를 얻기 위해 수동 교체할 필요가 없습니다. 새 설치용 번들 Receiver plugin은 버전에 맞춰 유지됩니다.
- **Release assets are built by CI / 릴리즈 산출물은 CI에서 빌드**: this GitHub release is created with notes only; GitHub Actions builds and uploads the Windows, macOS, Linux, Stream Deck, and checksum assets.
- **Release assets are built by CI / 릴리즈 산출물은 CI에서 빌드**: 이 GitHub release는 notes-only로 생성되며, GitHub Actions가 Windows, macOS, Linux, Stream Deck, checksum 산출물을 빌드하고 업로드합니다.

---

### Validation / 검증
- Local Windows Release verification built: `DirectPipe`, `DirectPipeReceiver_VST3`, `DirectPipeReceiver_VST`, `directpipe-tests`, and `directpipe-host-tests`.
- Local Windows Release 검증에서 `DirectPipe`, `DirectPipeReceiver_VST3`, `DirectPipeReceiver_VST`, `directpipe-tests`, `directpipe-host-tests` 빌드를 확인했습니다.
- `directpipe-tests`: 52 passed.
- `directpipe-tests`: 52개 통과.
- `directpipe-host-tests`: 329 tests; 327 passed, 2 environment-dependent tests skipped.
- `directpipe-host-tests`: 총 329개 중 327개 통과, 환경 의존 테스트 2개 skipped.
- `ctest`: 381 registered tests, 0 failed, 2 skipped.
- `ctest`: 381개 등록 테스트, 실패 0개, skipped 2개.
- Text integrity, JSON metadata validation, Stream Deck package manifest validation, version metadata validation, and `git diff --check` passed.
- Text integrity, JSON metadata validation, Stream Deck package manifest validation, version metadata validation, `git diff --check`가 통과했습니다.
- Final release assets are rebuilt by GitHub Actions after the release is created.
- 최종 릴리즈 산출물은 release 생성 후 GitHub Actions에서 다시 빌드됩니다.

---

### Downloads / 다운로드
- `DirectPipe-v4.1.2-Windows.zip` — Windows stable artifact, CI-built.
- `DirectPipe-v4.1.2-Windows.zip` — Windows 안정 버전 산출물이며 CI에서 빌드했습니다.
- `DirectPipe-v4.1.2-macOS.dmg` — macOS beta artifact, CI-built.
- `DirectPipe-v4.1.2-macOS.dmg` — macOS beta 산출물이며 CI에서 빌드했습니다.
- `DirectPipe-v4.1.2-Linux.tar.gz` — Linux experimental artifact, CI-built.
- `DirectPipe-v4.1.2-Linux.tar.gz` — Linux experimental 산출물이며 CI에서 빌드했습니다.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck control package, CI-built.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck 제어 패키지이며 CI에서 빌드했습니다.
- `checksums.sha256` — generated by CI for all uploaded assets.
- `checksums.sha256` — 업로드된 모든 산출물에 대해 CI에서 생성한 checksum 파일입니다.

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.1.1...v4.1.2

**전체 변경 비교**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.1.1...v4.1.2
