## DirectPipe v4.0.7

v4.0.6 이후, 모니터 출력 샘플레이트 불일치 상황에서 GUI가 즉시 멈출 수 있던 재연결 루프를 수정한 핫픽스 릴리스입니다.
This v4.0.7 hotfix prevents a monitor-output sample-rate mismatch from driving a tight reconnection loop that could freeze the GUI after v4.0.6.

---

### Highlights / 주요 변경
#### 1) Monitor sample-rate mismatch freeze fix / 모니터 샘플레이트 불일치 프리즈 수정
- 모니터 장치가 메인 오디오와 다른 샘플레이트로 열릴 때, 이를 반복 재연결할 수 있는 장치 lost 상태가 아니라 사용자가 설정을 바꿀 때까지 비활성화되는 구성 불일치 상태로 처리합니다.
- Monitor devices that open at a different sample rate from the main audio device are now treated as a disabled configuration mismatch, not a retryable device-loss loop.
- GitHub issue [#2](https://github.com/LiveTrack-X/DirectPipe/issues/2)에 보고된 v4.0.6 GUI freeze 회귀를 해결합니다.
- Addresses the v4.0.6 GUI freeze regression reported in GitHub issue [#2](https://github.com/LiveTrack-X/DirectPipe/issues/2).

#### 2) Reconnection success accuracy / 재연결 성공 판정 정합성
- 모니터 출력이 실제 `Active` 상태가 되었을 때만 재연결 성공 로그와 알림을 표시합니다.
- Reconnection success logs and notifications are now emitted only when monitor output actually reaches the `Active` state.

#### 3) Internal monitor restart guard / 내부 모니터 재시작 보호
- 내부 재초기화와 종료 과정에서 발생하는 monitor callback을 외부 장치 lost로 기록하지 않도록 보호했습니다.
- Internal monitor restarts and teardowns no longer report their own callback sequence as an external device-lost event.

#### 4) Version and release docs sync / 버전 및 릴리스 문서 동기화
- 앱, Receiver 플러그인, Stream Deck 플러그인, README, 사용자 가이드, 아키텍처/스펙 문서, 릴리스 본문의 v4.0.7 표기를 맞췄습니다.
- Updated app, Receiver plugin, Stream Deck plugin, README, user guide, architecture/spec docs, and release body references for v4.0.7.

#### 5) Sample-rate state alignment / 샘플레이트 상태 정합성
- 장치가 요청한 샘플레이트와 다른 실제 값으로 열릴 때, 내부 desired/current 샘플레이트 상태를 실제 런타임 값에 맞춥니다.
- When a device opens at a different sample rate than requested, desired/current sample-rate state is aligned to the actual runtime value.

#### 6) Tray Panic Mute toggle / 트레이 Panic Mute 토글
- 트레이/메뉴 바 우클릭 메뉴의 **Show Window** 바로 아래에 체크 상태가 표시되는 **Panic Mute** 항목을 추가했습니다.
- The tray/menu bar right-click menu now includes a checked **Panic Mute** item directly under **Show Window**.
- 메인 창 버튼, 단축키, Stream Deck, HTTP, WebSocket과 같은 restore-safe Panic Mute 경로를 사용합니다.
- This uses the same restore-safe Panic Mute path as the main window button, hotkey, Stream Deck, HTTP, and WebSocket controls.

#### 7) Failed settings apply guards / 실패한 설정 적용 보호
- 실패한 드라이버/장치/채널/모니터 설정 변경은 더 이상 성공한 것처럼 저장되지 않습니다.
- Failed driver/device/channel/monitor setting changes no longer get saved as if they succeeded.
- 런타임 desired target은 장치 변경이 실제로 수락된 뒤에만 갱신되어 stale retry와 복원 상태 오염을 줄입니다.
- Desired runtime targets are updated only after the device change is accepted, reducing stale retry and restore-state issues.

---

### Upgrade Notes / 업그레이드 안내
- **No API/state model break / API·상태 모델 비호환 없음**: This release does not introduce breaking control API or state schema changes.
- **Monitor mismatch**: If the monitor status shows a sample-rate mismatch, align the monitor device sample rate with the main DirectPipe sample rate or reselect the monitor after changing the main sample rate. / 모니터 샘플레이트 불일치가 표시되면 모니터 장치 샘플레이트를 DirectPipe 메인 샘플레이트와 맞추거나 메인 샘플레이트 변경 후 모니터를 다시 선택하세요.
- **Windows unblock / Windows 차단 해제**: 다운로드한 실행 파일이 차단되면 `DirectPipe.exe` 우클릭 -> **속성** -> **일반** -> **보안** -> **차단 해제** -> **적용/확인**을 누르세요.

---

### Downloads / 다운로드
- `DirectPipe-v4.0.7-Windows.zip`
- `DirectPipe-v4.0.7-macOS.dmg`
- `DirectPipe-v4.0.7-Linux.tar.gz`
- `com.directpipe.directpipe.streamDeckPlugin`
- `checksums.sha256`

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.0.6...v4.0.7
