## DirectPipe v4.2.4

> **Unpublished tag / 미게시 태그**: v4.2.4 failed the macOS/Linux CI gate
> and was never published as a GitHub Release. Its source tag remains for
> provenance; v4.2.3 remained the public stable release.
>
> v4.2.4는 macOS/Linux CI 게이트에서 실패해 GitHub Release로 게시되지
> 않았습니다. 출처 추적을 위해 소스 태그만 유지하며 공개 안정판은
> v4.2.3으로 남았습니다.

v4.2.4 hardens audio-device recovery. Failed main and monitor recovery is
rate-limited correctly, the configured monitor output is opened directly, and
an exclusive main-output transition releases a potentially conflicting monitor
before attempting the exclusive open.

v4.2.4는 오디오 장치 복구를 안정화합니다. 메인·모니터 복구 실패 시 올바른
재시도 간격을 유지하고, 선택한 모니터 출력을 직접 열며, 독점 메인 출력
전환 전에 충돌 가능 모니터를 먼저 해제합니다.

### Highlights / 주요 변경

- **Bounded failed recovery**: Failed zero-active-channel recovery for both the
  main engine and monitor output now waits the full three-second cooldown
  instead of retrying on every 30 Hz status update.
- **복구 실패 제한**: 메인 엔진과 모니터 출력의 zero-active-channel 복구가
  실패하면 30 Hz 상태 갱신마다 반복하지 않고 전체 3초 쿨다운을 기다립니다.
- **Selected monitor first**: The monitor manager opens the configured
  shared-mode output directly. A blocked system default no longer prevents a
  different valid selected output from opening.
- **선택 모니터 우선**: monitor manager가 설정된 shared-mode 출력을 직접
  엽니다. 시스템 기본 장치가 막혀 있어도 다른 유효 선택 장치를 열 수
  있습니다.
- **Exclusive transition preflight**: A monitor that could block an exclusive
  main output is suspended before the main open. It is restored on rollback or
  when the actual endpoints differ, and remains disabled only for a confirmed
  same-endpoint conflict.
- **독점 전환 사전 처리**: 독점 메인 출력을 막을 수 있는 모니터를 main
  open 전에 중지합니다. rollback 또는 서로 다른 endpoint면 복원하고,
  동일 endpoint 충돌이 확인될 때만 비활성 상태를 유지합니다.
- **Endpoint-event stability**: Self-generated Windows endpoint events are
  ignored for a bounded settle window, and Windows Audio Exclusive Mode is
  classified consistently as exclusive.
- **Endpoint 이벤트 안정화**: DirectPipe 자체가 만든 Windows endpoint
  이벤트는 제한된 안정화 구간 동안 무시하며 Windows Audio Exclusive
  Mode를 일관되게 독점 드라이버로 분류합니다.

### Compatibility / 호환성

- IPC wire ABI remains protocol v1 with the existing 192-byte shared-memory
  header. Receiver/VST replacement is not required for compatibility.
- IPC wire ABI는 기존 192-byte shared-memory header의 protocol v1을
  유지합니다. 호환성 때문에 Receiver/VST를 교체할 필요는 없습니다.
- Stream Deck action IDs, request/state schemas, discovery, and profiles are
  unchanged. Existing v4.2.x packages remain compatible; 4.2.4 is bundled only
  for package alignment.
- Stream Deck action ID, request/state schema, discovery, profile은 변경되지
  않았습니다. 기존 v4.2.x 패키지는 계속 호환되며 4.2.4는 패키지 버전
  정합성을 위해 함께 제공합니다.
- Existing v4.2.x presets and settings remain compatible.
- 기존 v4.2.x preset과 settings는 호환됩니다.

### Validation / 검증

- The local Windows Release run completed all 500 CTest registrations: 498
  passed, 2 environment-dependent tests skipped, and 0 failed. Focused
  regressions for all three blocker paths and related Windows
  classification/event handling pass.
- 로컬 Windows Release CTest 500개를 실행해 498개 통과, 환경 의존 2개
  skip, 실패 0개를 확인했습니다. 세 차단 경로와 관련 Windows 분류·이벤트
  처리 집중 회귀 테스트가 통과했습니다.
- Exact-tag cross-platform CI gates publication.
- 정확 태그 전체 플랫폼 CI가 공개를 게이트합니다.
- Real audio hardware, third-party VST crash-containment, and macOS/Linux
  hardware behavior were not verified for this update.
- 이번 업데이트에서는 실기기 오디오, 제3자 VST crash-containment,
  macOS/Linux 하드웨어 동작을 검증하지 않았습니다.

### Planned assets (not published) / 예정 자산 (미게시)

- `DirectPipe-v4.2.4-Windows.zip` — Windows stable artifact, CI-built.
- `DirectPipe-v4.2.4-macOS.dmg` — macOS beta artifact, CI-built.
- `DirectPipe-v4.2.4-Linux.tar.gz` — Linux experimental artifact, CI-built.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck 4.2.4 package.
- `checksums.sha256` — SHA-256 manifest for all payload artifacts.

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.3...v4.2.4

**전체 변경 비교**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.3...v4.2.4
