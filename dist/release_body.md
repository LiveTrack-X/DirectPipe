## DirectPipe v4.0.8

v4.0.7 이후, 방송 전 오디오 테스트/Full Backup 복원 후 오디오 장치가 다시 잡히지 않는 문제와 macOS 모니터 지연 누적 문제를 보강한 핫픽스 릴리스입니다.
This v4.0.8 hotfix hardens Full Backup/preset restore and bounds macOS monitor latency drift.

---

### Highlights / 주요 변경

#### 1) Full Restore audio refresh / 전체 복원 오디오 재연결 보강
- Full Backup 또는 프리셋 복원 후 현재 장치가 `SR=0` 같은 invalid/stopped 상태를 보고하면, 저장된 장치명이 같더라도 장치를 다시 열어 실제 오디오 경로를 복구합니다.
- Full Backup and preset restore now reopen the current audio device when the driver reports an invalid or stopped state such as `SR=0`, even when the saved device name matches the current setup.
- ASIO뿐 아니라 WASAPI에서도 같은 복원 경로를 사용하므로, 두 번째 복원이나 수동 버퍼/장치 변경 없이 오디오가 다시 잡히도록 보강했습니다.
- The same recovery path covers ASIO and WASAPI, reducing cases where audio only comes back after a second restore or manual buffer/device change.

#### 2) Monitor latency drift compensation / 모니터 지연 누적 보정
- macOS/CoreAudio처럼 입력 장치와 모니터 출력 장치가 서로 다른 클럭으로 움직이는 구성에서 ring buffer가 계속 쌓이면 오래된 프레임을 자동으로 정리해 모니터 지연이 선형으로 증가하지 않게 했습니다.
- When separate input and monitor output devices drift on independent clocks, DirectPipe now trims stale monitor ring-buffer frames so monitoring latency stays bounded instead of growing linearly (#3).
- 모니터 OFF/ON으로 지연을 초기화해야 하던 상황을 줄입니다.
- This reduces cases where toggling monitoring OFF/ON is needed to reset accumulated latency.

#### 3) Invalid device-state guards / 잘못된 장치 상태 방어
- 장치 시작, 드라이버 전환, 샘플레이트 적용, IPC 출력 활성화 경로에서 invalid `SR=0`/buffer 값이 내부 desired/current 상태로 저장되지 않도록 방어했습니다.
- Invalid `SR=0` or buffer values are no longer allowed to pollute desired/current runtime state during device start, driver switching, sample-rate apply, or IPC enable.
- 아직 `initialize()`되지 않은 엔진에서 드라이버 타입만 저장해야 하는 경우 실제 오디오 장치를 열지 않도록 해, 테스트 및 설정 로드 경계에서 callback 누수를 막았습니다.
- Driver type changes on a non-initialized engine now store intent without opening a real audio device, preventing callback lifetime issues in isolated setup/load paths.

#### 4) Restore-state accuracy / 복원 상태 정합성
- 프리셋 복원의 채널 마스크 적용도 AudioEngine의 intentional device-change 경로를 사용해, 정상 복원 중 발생하는 장치 stop/start가 외부 장치 분실로 오인되지 않게 했습니다.
- Channel-mask restore now goes through AudioEngine's intentional device-change path so normal restore stop/start cycles are not mistaken for external device loss.

#### 5) Version and release docs sync / 버전 및 릴리스 문서 동기화
- 앱, Receiver 플러그인, Stream Deck 플러그인, README, 사용자 가이드, 아키텍처/스펙 문서, 릴리스 본문의 v4.0.8 표기를 맞췄습니다.
- Updated app, Receiver plugin, Stream Deck plugin, README, user guide, architecture/spec docs, and release body references for v4.0.8.

#### 6) Issue trace / 이슈 추적
- GitHub issue [#3](https://github.com/LiveTrack-X/DirectPipe/issues/3)의 macOS incremental monitoring latency drift를 완화합니다.
- Mitigates the macOS incremental monitoring latency drift reported in GitHub issue [#3](https://github.com/LiveTrack-X/DirectPipe/issues/3).
- GitHub issue [#2](https://github.com/LiveTrack-X/DirectPipe/issues/2)의 모니터 샘플레이트 불일치 프리즈 수정은 v4.0.7 이력으로 유지되어 있으며, 이번 릴리스는 이후 접수된 Full Restore 장치 복구 문제를 추가로 보강합니다.
- The monitor sample-rate mismatch freeze fix for GitHub issue [#2](https://github.com/LiveTrack-X/DirectPipe/issues/2) remains recorded in v4.0.7; this release adds the later Full Restore device recovery hardening.

---

### Upgrade Notes / 업그레이드 안내
- **No API/state model break / API 및 상태 모델 호환 유지**: 이 릴리스는 control API 또는 state schema의 breaking change를 포함하지 않습니다.
- **Full Restore / 전체 복원**: 복원 직후 장치가 stopped/invalid 상태로 보고되면 DirectPipe가 한 번 더 장치 설정을 적용해 오디오 경로를 복구합니다. 장치 자체가 분리되어 있거나 드라이버가 실패한 경우에는 로그에 복구 실패 이유가 남습니다.
- **Monitor drift / 모니터 지연 누적**: 서로 다른 물리 장치를 입력과 모니터 출력으로 쓰는 경우 clock drift 자체는 발생할 수 있지만, DirectPipe가 오래된 monitor buffer를 정리해 누적 지연을 제한합니다.
- **Windows unblock / Windows 차단 해제**: 다운로드한 실행 파일이 차단되면 `DirectPipe.exe` 우클릭 -> **속성** -> **일반** -> **보안** -> **차단 해제** -> **적용/확인**을 누르세요.

---

### Downloads / 다운로드
- `DirectPipe-v4.0.8-Windows.zip`
- `DirectPipe-v4.0.8-macOS.dmg`
- `DirectPipe-v4.0.8-Linux.tar.gz`
- `com.directpipe.directpipe.streamDeckPlugin`
- `checksums.sha256`

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.0.7...v4.0.8
