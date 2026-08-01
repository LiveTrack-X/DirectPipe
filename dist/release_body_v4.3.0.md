## DirectPipe v4.3.0

v4.3.0 restores the v4.2.0 default-device routing rule. When Windows starts a
valid input/output route without first reporting device loss, DirectPipe keeps
that platform-selected default instead of reopening an older saved endpoint.

v4.3.0은 v4.2.0의 기본 장치 라우팅 규칙을 복원합니다. Windows가 장치
손실을 먼저 보고하지 않고 유효한 입력/출력 경로를 시작하면, DirectPipe는
이전 저장 endpoint를 다시 여는 대신 플랫폼이 선택한 기본 장치를 유지합니다.

### Highlights / 주요 변경

- **Default route is authoritative on a clean start**: A valid error-free
  start with no preceding loss adopts the actual driver, input, and output.
- **정상 시작은 기본 경로 채택**: 선행 장치 손실이 없는 유효한 시작은
  실제 드라이버·입력·출력을 현재 경로로 채택합니다.
- **Real loss still restores the selected target**: Disconnect/error recovery
  retains per-direction desired-device restoration and automatic output mute.
- **실제 손실 복구 유지**: 분리·오류 뒤에는 방향별 저장 장치 복원과 자동
  출력 mute 동작을 그대로 유지합니다.
- **Later choices still win**: Deferred device callbacks cannot overwrite a
  settings restore, manual device selection, or `Output: None` made afterward.
- **최신 선택 우선 유지**: 지연된 장치 callback은 이후의 설정 복원, 수동
  장치 선택, `Output: None`을 덮어쓰지 못합니다.
- **Later safety fixes stay in place**: Transactional rollback, invalid-rate
  and zero-active-channel fail-closed handling, monitor recovery/backoff, ASIO
  validation, and updater hardening are not rolled back.
- **후속 안전 수정 유지**: transactional rollback, 잘못된 rate 및
  zero-active-channel 안전 중단, monitor 복구/backoff, ASIO 검증, updater
  hardening은 되돌리지 않습니다.

### Compatibility / 호환성

- IPC protocol v1 and the 192-byte shared-memory header are unchanged.
- IPC protocol v1과 192-byte shared-memory header는 변경되지 않았습니다.
- Receiver identity, Stream Deck actions, HTTP/WebSocket schemas, presets,
  settings, and plug-in format support remain compatible.
- Receiver 신원, Stream Deck action, HTTP/WebSocket schema, preset, settings,
  plug-in format 지원은 호환됩니다.

### Validation / 검증

- The new regression failed against v4.2.8 behavior and passes with this fix.
  Five focused default-route, real-loss, and stale-callback scenarios pass.
- 새 회귀 테스트는 v4.2.8 동작에서 실패했고 수정 뒤 통과합니다. 기본
  라우팅·실제 손실·stale callback 집중 시나리오 5개가 통과했습니다.
- The local Windows Release gate registered 582 tests: 580 passed, 2
  environment-dependent tests were skipped, and 0 failed. The Stream Deck
  suite passed 22/22 tests and produced a validated v4.3.0.0 package.
- 로컬 Windows Release 게이트는 582개 테스트를 등록해 580개 통과,
  환경 의존 2개 건너뜀, 실패 0개를 기록했습니다. Stream Deck은 22/22
  테스트와 v4.3.0.0 패키지 검증을 통과했습니다.
- The locally built Windows host, VST2 Receiver, and inner VST3 Receiver all
  report file/product version 4.3.0 and are explicitly unsigned.
- 로컬 Windows host, VST2 Receiver, 내부 VST3 Receiver는 모두
  file/product version 4.3.0이며 명시적으로 unsigned 상태입니다.
- Publication requires the exact `v4.3.0` tag to pass Windows, macOS, Linux,
  Stream Deck, executable-identity, signature-state, and checksum gates.
- 정확한 `v4.3.0` 태그가 Windows, macOS, Linux, Stream Deck, 실행 파일
  신원, 서명 상태, checksum 게이트를 통과한 경우에만 게시합니다.
- No trusted Windows code-signing certificate is configured. The Windows
  package is expected to be unsigned and may trigger reputation warnings.
- 신뢰된 Windows 코드 서명 인증서가 없어 Windows 패키지는 unsigned로
  배포되며 브라우저/SmartScreen 평판 경고가 나타날 수 있습니다.
- Real-device audio, third-party VST crash containment, and macOS/Linux hardware
  behavior are outside this software-only release evidence.
- 실기기 오디오, 제3자 VST crash containment, macOS/Linux 하드웨어 동작은
  이번 software-only 릴리스 증거 범위에 포함하지 않습니다.

### Downloads / 다운로드

- `DirectPipe-v4.3.0-Windows.zip`
- `DirectPipe-v4.3.0-macOS.dmg`
- `DirectPipe-v4.3.0-Linux.tar.gz`
- `com.directpipe.directpipe.streamDeckPlugin`
- `checksums.sha256`

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.8...v4.3.0

**전체 변경 비교**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.8...v4.3.0
