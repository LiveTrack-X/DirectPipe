## DirectPipe v4.2.6

> [!WARNING]
> **Withdrawn build:** v4.2.6 is retained for provenance but is no longer
> recommended. Use v4.2.3 until the corrective v4.2.7 exact-tag release is
> published.

v4.2.6 replaces the withdrawn v4.2.5 release. It preserves the selected
Windows input/output endpoints across failed driver changes and input-device
loss, prevents a failed `CABLE Input` selection from leaving an unintended
input-only/default stream, and makes the Windows package's signed or unsigned
state explicit in exact-tag CI.

v4.2.6은 회수된 v4.2.5를 대체합니다. 드라이버 전환 실패와 입력 장치 손실
과정에서 선택한 Windows 입·출력 endpoint를 유지하고, `CABLE Input` 선택
실패가 의도하지 않은 입력 전용/기본 장치 스트림을 남기지 않게 하며,
현재 인증서가 없는 Windows 패키지는 unsigned임을 명시합니다.

### Highlights / 주요 변경

- **Exact driver rollback**: A failed ASIO/Windows transition restores the
  saved input, output, sample rate, buffer size, and channel masks. It fails
  closed when that snapshot cannot be restored.
- **정확한 드라이버 rollback**: ASIO/Windows 전환 실패 시 저장된 입력,
  출력, sample rate, buffer size, channel mask를 복원하며 불가능하면
  임의 기본 장치로 진행하지 않습니다.
- **Transactional endpoint selection**: Failed input/output changes recreate
  the previous duplex setup before reporting the error.
- **원자적 endpoint 선택**: 입·출력 변경 실패 시 오류를 반환하기 전에
  이전 duplex 설정을 재생성합니다.
- **Windows-mode endpoint preservation**: The first transition among shared,
  low-latency, and exclusive modes carries the current endpoint selection and
  cancels if those endpoints are unavailable.
- **Windows 모드 endpoint 보존**: shared, low-latency, exclusive 모드의 첫
  전환에서 현재 endpoint를 이어 가며 대상 모드에 없으면 취소합니다.
- **Explicit same-device recovery**: A disabled `Device (Reconnect)`
  placeholder keeps the real same-name item clickable for one-step recovery.
  ASIO applies the recovery state to both sides of its duplex device.
- **명시적 동일 장치 복구**: 비활성 `장치명 (Reconnect)` placeholder와
  실제 같은 이름의 항목을 분리해 한 번의 클릭으로 다시 열 수 있습니다.
  ASIO는 duplex 장치 양쪽에 복구 상태를 적용합니다.
- **Durable shutdown settings**: Non-plugin changes made during a loading or
  partial VST chain are merged with the last complete chain, or saved to a
  recovery sidecar when no complete chain exists. Plugin/slot files are never
  replaced by the sidecar.
- **종료 설정 내구성**: VST chain loading/partial 중의 비플러그인 변경은
  마지막 완전 chain과 병합하며, 완전 chain이 없으면 recovery sidecar에
  저장합니다. sidecar는 plugin/slot 파일을 대체하지 않습니다.
- **Bounded monitor recovery**: Missing devices keep three-second polling;
  enumerated endpoints that repeatedly fail to open back off
  3→10→20→30 seconds. Diagnostics are emitted on the first and every twentieth
  automatic attempt.
- **제한된 모니터 복구**: 누락 장치는 3초 polling을 유지하고, 열거되지만
  반복해서 열리지 않는 endpoint는 3→10→20→30초로 backoff합니다. 자동
  진단은 첫 시도와 매 20번째 시도에 남깁니다.
- **Driver-aware latency estimate**: Bottom `Latency` uses driver-reported
  input/output latency (one-buffer fallback per missing direction) plus active
  plug-in PDC. Callback time remains a CPU/XRun diagnostic.
- **드라이버 기반 레이턴시 추정**: 하단 `Latency`는 드라이버 보고 입·출력
  지연(미보고 방향은 1버퍼 대체) + 활성 plug-in PDC를 사용하며 callback
  시간은 CPU/XRun 진단으로만 유지합니다.
- **Correct Monitor route**: `Mon` uses main input latency + PDC + adaptive
  monitor queue target + monitor output-device latency; unavailable Monitor
  state no longer shows a false number.
- **정확한 Monitor 경로**: `Mon`은 메인 입력 지연 + PDC + adaptive monitor
  queue 목표량 + 모니터 출력 장치 지연을 사용하며, unavailable 상태에는
  허위 숫자를 표시하지 않습니다.
- **Explicit signature state**: Exact-tag CI signs and verifies Windows
  binaries when both trusted PFX secrets exist. Without a certificate it
  requires every staged binary to be `NotSigned` and publishes an explicitly
  unsigned package; partial configuration or any signature-state mismatch
  fails.
- **명시적 서명 상태**: PFX secret 두 개가 모두 있으면 Windows 바이너리를
  서명·검증하고, 현재처럼 인증서가 없으면 모든 staged 바이너리가
  `NotSigned`인지 확인한 뒤 unsigned 패키지로 명시합니다. 불완전 구성이나
  서명 상태 불일치는 실패합니다.

### Compatibility / 호환성

- IPC protocol v1 and the 192-byte shared-memory header are unchanged.
- IPC protocol v1과 192-byte shared-memory header는 변경되지 않았습니다.
- Existing v4.2.x Receiver VST, Stream Deck actions/profiles, presets, and
  settings remain compatible. Bundled copies are version-aligned only.
- 기존 v4.2.x Receiver VST, Stream Deck action/profile, preset, settings는
  호환됩니다. 번들 사본은 패키지 버전 정합성만 맞춥니다.

### Validation / 검증

- Local Windows Release validation: 546 CTest registrations; 544 passed,
  2 environment-dependent tests skipped, and 0 failed
  (59 core + 485 host + 2 focused endpoint).
- 로컬 Windows Release 검증: CTest 546개 중 544개 통과, 환경 의존
  2개 건너뜀, 실패 0개(59 core + 485 host + 2 focused endpoint).
- Stream Deck unit tests, Rollup build, and official CLI validation passed.
- Stream Deck 단위 테스트, Rollup 빌드, 공식 CLI 검증 통과.
- The release is published only after exact-tag cross-platform CI, package
  validation, and checksum generation pass. Authenticode remains optional until
  a trusted certificate is available.
- 정확 태그 전체 플랫폼 CI, 패키지 검증, checksum 생성이 통과한 경우에만
  릴리즈를 게시합니다. 신뢰된 인증서를 확보하기 전 Authenticode는 선택
  사항입니다.
- Real-device audio and third-party VST crash-containment were not verified.
- 실기기 오디오와 제3자 VST crash-containment는 검증하지 않았습니다.

### Downloads / 다운로드

- `DirectPipe-v4.2.6-Windows.zip`
- `DirectPipe-v4.2.6-macOS.dmg`
- `DirectPipe-v4.2.6-Linux.tar.gz`
- `com.directpipe.directpipe.streamDeckPlugin`
- `checksums.sha256`

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.3...v4.2.6

**전체 변경 비교**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.3...v4.2.6
