## DirectPipe v4.2.7

v4.2.7 is the corrective replacement for the withdrawn v4.2.6 build. It
validates every automatic ASIO candidate before accepting it, keeps Mono as an
input-front L+R mix duplicated to a two-channel VST path, synchronizes Stream
Deck controls with actual plug-in values and defaults, and hardens asynchronous
HTTP and Windows update behavior.

v4.2.7은 회수된 v4.2.6 빌드의 수정 대체판입니다. 자동 ASIO 후보를
수락하기 전에 각각 검증하고, Mono를 입력단 L+R 합산 후 2채널 VST 경로로
복제하는 구조로 유지하며, Stream Deck 제어를 실제 플러그인 값·기본값과
동기화하고 비동기 HTTP 및 Windows 업데이트 동작을 보강합니다.

### Highlights / 주요 변경

- **Validated ASIO fallback**: Automatic selection accepts a candidate only
  after the device, sample rate, buffer size, and usable duplex channels are
  valid. An unusable candidate no longer prevents trying the next device.
- **검증된 ASIO fallback**: 장치, sample rate, buffer size, 사용 가능한
  duplex 채널이 모두 유효한 후보만 수락하며, 잘못 열린 후보 뒤의 정상
  장치를 계속 시도합니다.
- **Correct input-front dual mono**: Mono opens the selected two-channel input
  pair, averages L+R before the VST chain, duplicates that signal to internal
  L/R, then sends the processed two-channel result to the selected output pair.
  A genuine one-channel device remains a bounded fallback.
- **정확한 입력단 dual mono**: Mono는 선택한 2개 입력을 VST 체인 전에
  평균 합산하고 같은 신호를 내부 L/R에 복제한 뒤, 처리된 2채널 결과를 선택한
  출력 쌍으로 보냅니다. 실제 1채널 장치는 제한된 fallback으로 유지합니다.
- **Durable channel restore**: Temporarily unknown channel-name layouts retain
  bounded saved pair indices until apply. Output-device changes and failed
  explicit-pair restores preserve a genuine one-channel input through the
  driver-default route.
- **안전한 채널 복원**: 채널 이름을 일시적으로 알 수 없어도 저장된 범위
  내 채널 쌍을 적용 시점까지 보존합니다. 출력 장치 전환과 명시 쌍 복원
  실패에서도 실제 1채널 입력은 드라이버 기본 경로를 유지합니다.
- **Selectable recovery devices**: Recovery placeholders use their dedicated
  item identity instead of a text suffix, so real device names ending in
  `(Reconnect)` or `(Disconnected)` remain selectable.
- **선택 가능한 복구 장치**: 복구 placeholder를 문자열 접미사가 아닌
  전용 항목 ID로 구분해 같은 접미사의 실제 장치도 선택할 수 있습니다.
- **Synchronized Stream Deck parameters**: Plug-in dials initialize and refresh
  from the actual parameter value, reset to the plug-in-reported default, and
  isolate throttled updates by action and target. Volume controls use the same
  isolation.
- **동기화된 Stream Deck 파라미터**: 다이얼은 실제 파라미터 값으로
  초기화·갱신되고 플러그인이 보고한 기본값으로 reset되며, throttle 상태는
  action·대상별로 분리됩니다. 볼륨 제어도 같은 분리 규칙을 사용합니다.
- **Accurate control contracts**: Mutating HTTP routes now return
  `202 Accepted` with backward-compatible `ok: true` plus explicit
  `accepted: true`; synchronous reads retain `200 OK`. Stream Deck manifests
  no longer advertise unimplemented gestures, and local packaging delegates
  to the official Stream Deck CLI.
- **정확한 제어 계약**: 비동기 변경 HTTP 요청은 `202 Accepted`, 동기
  조회는 `200 OK`를 사용합니다. Stream Deck manifest는 미구현 제스처를
  광고하지 않으며 로컬 패키징도 공식 CLI를 사용합니다.
- **Stricter Windows updater identity**: A ZIP must contain exactly one
  `DirectPipe.exe`; its FileVersion and ProductVersion must match the requested
  release before replacement. The previous executable backup remains available
  until the next update rotation.
- **강화된 Windows updater 신원**: ZIP 안에 `DirectPipe.exe`가 정확히
  하나 있어야 하고 FileVersion·ProductVersion이 요청 버전과 일치해야
  교체합니다. 이전 실행 파일 backup은 다음 업데이트 회전까지 유지합니다.

### Compatibility / 호환성

- IPC protocol v1 and the 192-byte shared-memory header are unchanged.
- IPC protocol v1과 192-byte shared-memory header는 변경되지 않았습니다.
- Receiver identity, WebSocket action/state schemas, existing presets, and
  settings remain compatible. Existing HTTP endpoint paths, request payloads,
  and `ok: true` remain compatible; queued mutations add `accepted: true` and
  intentionally use `202`.
- Receiver 신원, WebSocket action/state schema, 기존 preset과 settings는
  호환됩니다. HTTP endpoint 경로와 요청 payload는 유지하며 비동기 변경
  응답만 의도적으로 `202`를 사용합니다.
- v4.2.6 remains available only for provenance and is not recommended.
- v4.2.6은 출처 추적용으로만 유지하며 설치를 권장하지 않습니다.

### Validation / 검증

- The release is published only after the exact `v4.2.7` tag completes
  cross-platform builds, registered tests, Stream Deck validation and official
  packaging, Windows executable-version/signature-state checks, and checksum
  generation.
- 정확한 `v4.2.7` 태그의 전체 플랫폼 빌드·등록 테스트, Stream Deck
  검증·공식 패키징, Windows 실행 파일 버전·서명 상태, checksum 생성이
  모두 통과한 경우에만 게시합니다.
- DirectPipe has no trusted Windows code-signing certificate. The Windows
  package is therefore explicitly unsigned, and browser/SmartScreen reputation
  warnings may still occur; verify it with `checksums.sha256`.
- 현재 신뢰된 Windows 코드 서명 인증서가 없어 Windows 패키지는 명시적으로
  unsigned입니다. 브라우저/SmartScreen 평판 경고가 나타날 수 있으므로
  `checksums.sha256`으로 검증하세요.
- Real-device audio, third-party VST crash containment, and macOS/Linux hardware
  behavior were not verified for this release.
- 실기기 오디오, 제3자 VST crash containment, macOS/Linux 하드웨어 동작은
  이번 릴리스에서 검증하지 않았습니다.

### Downloads / 다운로드

- `DirectPipe-v4.2.7-Windows.zip`
- `DirectPipe-v4.2.7-macOS.dmg`
- `DirectPipe-v4.2.7-Linux.tar.gz`
- `com.directpipe.directpipe.streamDeckPlugin`
- `checksums.sha256`

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.3...v4.2.7

**전체 변경 비교**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.3...v4.2.7
