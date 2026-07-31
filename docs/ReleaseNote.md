# DirectPipe Release Notes

> This is a user-facing release summary. For detailed developer change history, see [CHANGELOG.md](../CHANGELOG.md).

## DirectPipe v4.2.8

v4.2.8 is a focused Windows device-restoration reliability update. It keeps
the last saved driver, input, and output across reboot/startup and silent
endpoint fallback instead of adopting the first enumerated or system-default
device.

v4.2.8은 Windows 장치 복원 신뢰성에 집중한 업데이트입니다. 재부팅·시작
및 오류 문자열 없이 발생하는 endpoint fallback 뒤에도 목록 첫 장치나
시스템 기본 장치를 새 설정으로 수용하지 않고 마지막으로 저장한 드라이버,
입력, 출력을 복원합니다.

v4.2.8 is the current public stable release after exact-tag CI run
`30640495424` completed successfully.

v4.2.8은 정확 태그 CI run `30640495424`를 통과한 현재 공개
안정판입니다.

### Highlights / 주요 변경

- **Restore the actual last selection**: Driver, input, and output identities
  are restored independently; no device name such as VB-Cable is hard-coded.
- **실제 마지막 선택 복원**: 드라이버·입력·출력 신원을 각각 복원하며
  VB-Cable을 포함한 특정 장치명을 고정하지 않습니다.
- **Reject silent fallback**: A callback that starts on a different endpoint
  or driver is treated as recovery state even when JUCE reports no error.
- **무음 fallback 거부**: JUCE가 오류를 반환하지 않더라도 저장값과 다른
  endpoint나 드라이버가 시작되면 복구 대상으로 처리합니다.
- **Manual choices win**: Generation-gated deferred work cannot overwrite a
  later manual device selection, `Output: None`, or restored startup settings.
- **최신 사용자 선택 우선**: 세대 번호로 보호된 지연 작업은 이후의 수동
  장치 선택, `Output: None`, 시작 복원 설정을 덮어쓰지 못합니다.
- **Fail closed on unusable starts**: Invalid rate/buffer/device identity keeps
  output muted and recovery pending rather than adopting an unsafe stream.
- **사용 불가 시작은 안전 중단**: rate·buffer·장치 신원이 유효하지 않으면
  잘못 열린 스트림을 수용하지 않고 출력 mute와 복구 대기를 유지합니다.

### Compatibility / 호환성

- IPC protocol v1 and the 192-byte shared-memory header are unchanged.
- IPC protocol v1과 192-byte shared-memory header는 변경되지 않았습니다.
- Receiver, Stream Deck action/state schemas, presets, and settings remain
  compatible. The saved device names already stored by each user remain the
  source of truth.
- Receiver, Stream Deck action/state schema, preset, settings는 호환되며 각
  사용자가 저장한 장치 이름이 복원 기준입니다.

### Validation / 검증

- Local Windows Release validation completed all 582 CTest registrations:
  580 passed, 2 environment-dependent tests skipped, and 0 failed
  (59 core + 521 host + 2 focused endpoint).
- 로컬 Windows Release 검증은 CTest 582개 중 580개 통과, 환경 의존 2개
  건너뜀, 실패 0개로 완료했습니다(59 core + 521 host + 2 focused
  endpoint).
- Exact-tag CI run `30640495424` passed Windows, macOS, Linux, Stream Deck,
  executable-identity, unsigned-state, checksum, and publication jobs. All five
  public assets and four payload hashes were independently verified.
- 정확 태그 CI run `30640495424`는 Windows, macOS, Linux, Stream Deck,
  실행 파일 신원, unsigned 상태, checksum, 게시 작업을 통과했습니다.
  공개 자산 5개와 payload 해시 4개도 독립 확인했습니다.
- No trusted Windows code-signing certificate is configured. The Windows
  package is unsigned and may trigger browser/SmartScreen reputation warnings;
  verify the official ZIP with `checksums.sha256`.
- 신뢰된 Windows 코드 서명 인증서가 없어 Windows 패키지는 unsigned이며
  브라우저/SmartScreen 평판 경고가 나타날 수 있습니다. 공식 ZIP을
  `checksums.sha256`으로 검증하세요.
- Real-device audio, third-party VST crash containment, and macOS/Linux hardware
  behavior were not verified for this release.
- 실기기 오디오, 제3자 VST crash containment, macOS/Linux 하드웨어 동작은
  이번 릴리즈에서 검증하지 않았습니다.

### Downloads / 다운로드

- `DirectPipe-v4.2.8-Windows.zip`
- `DirectPipe-v4.2.8-macOS.dmg`
- `DirectPipe-v4.2.8-Linux.tar.gz`
- `com.directpipe.directpipe.streamDeckPlugin`
- `checksums.sha256`

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.7...v4.2.8

**전체 변경 비교**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.7...v4.2.8

**Release**: https://github.com/LiveTrack-X/DirectPipe/releases/tag/v4.2.8

**Release commit**: `0d3f35edde118ffb4c3d7440694e1e930d5e8330`

---

## DirectPipe v4.2.7

v4.2.7 replaces the withdrawn v4.2.6 build. It validates automatic ASIO
candidates before accepting them, restores the intended input-front dual-mono
route, synchronizes Stream Deck plug-in controls with actual values and
defaults, and strengthens queued HTTP plus Windows updater behavior.

v4.2.7은 회수된 v4.2.6 빌드를 대체합니다. 자동 ASIO 후보를 수락하기
전에 검증하고, 의도한 입력단 dual-mono 경로를 복원하며, Stream Deck
플러그인 제어를 실제 값·기본값과 동기화하고 비동기 HTTP와 Windows
updater 동작을 강화합니다.

The v4.2.6 source tag and assets remain available for provenance, but its
GitHub Release is marked withdrawn/prerelease and is not recommended. v4.2.7
is the current public stable release after exact-tag CI run `30571612012`
completed successfully.

v4.2.6 소스 태그와 자산은 출처 추적용으로 유지하지만 GitHub Release는
withdrawn/prerelease로 표시하며 설치를 권장하지 않습니다. v4.2.7은
정확 태그 CI run `30571612012`를 통과한 현재 공개 안정판입니다.

### Highlights / 주요 변경

- **Validated ASIO fallback**: Each automatic candidate must expose a valid
  device, rate, buffer, and active duplex channels. An unusable candidate is
  skipped so the next device can be tried.
- **검증된 ASIO fallback**: 자동 후보마다 유효한 장치, rate, buffer,
  활성 duplex 채널을 확인하며 잘못 열린 후보는 건너뛰고 다음 장치를
  시도합니다.
- **Input-front dual mono**: Mono opens the selected input pair, averages L+R
  before the VST chain, duplicates the mono signal to internal L/R, and sends
  the processed two-channel result to the selected output pair.
- **입력단 dual mono**: Mono는 선택한 입력 L/R의 평균을 VST 체인 전에 만들고
  같은 신호를 내부 L/R로 복제한 뒤 처리된 2채널 결과를 선택한 출력 쌍에
  보냅니다.
- **Legacy mask repair**: A v4.2.6 one-channel Mono mask expands to its pair on
  restore. Unknown channel-name layouts retain bounded saved indices until
  apply, while genuine one-channel input remains on its driver-default route.
- **기존 mask 복구**: v4.2.6의 1채널 Mono mask는 복원 과정에서 2채널
  쌍으로 확장합니다. 채널 이름을 일시적으로 알 수 없어도 저장된 범위
  내 인덱스를 적용 시점까지 보존하고, 실제 1채널 입력은 드라이버 기본
  경로를 유지합니다.
- **Reliable Stream Deck dials**: Plug-in values initialize and refresh from
  DirectPipe, Push resets to the plug-in-reported default, and simultaneous
  plug-in/volume controls keep independent throttle state.
- **신뢰 가능한 Stream Deck 다이얼**: 파라미터 값은 DirectPipe에서
  초기화·갱신하며 Push는 플러그인이 보고한 기본값으로 reset되고, 동시
  플러그인·볼륨 조작은 독립된 throttle 상태를 사용합니다.
- **Honest control contracts**: Queued HTTP mutations return `202 Accepted`
  with backward-compatible `ok: true` plus explicit `accepted: true`;
  synchronous reads retain `200 OK`, manifests omit unimplemented gestures,
  and local Stream Deck packaging uses the official CLI.
- **정확한 제어 계약**: 비동기 HTTP 변경은 `202 Accepted`, 동기 조회는
  `200 OK`를 사용하며 manifest는 미구현 제스처를 빼고 로컬 Stream Deck
  패키징은 공식 CLI를 사용합니다.
- **Verified updater payload**: Windows updates require exactly one
  `DirectPipe.exe` with matching FileVersion/ProductVersion and retain the
  previous executable backup until the next update.
- **검증된 updater payload**: Windows 업데이트는 정확히 하나의
  `DirectPipe.exe`와 일치하는 FileVersion/ProductVersion을 요구하며,
  이전 실행 파일 backup을 다음 업데이트까지 보존합니다.
- **Fail-fast missing install**: If the installed executable is already
  missing, the updater now fails before extraction or PowerShell candidate
  validation and leaves any existing backup untouched.
- **빠른 설치 누락 실패**: 설치된 실행 파일이 이미 없으면 updater는
  압축 해제나 PowerShell 후보 검증 전에 실패하고 기존 backup을 그대로
  보존합니다.

### Compatibility / 호환성

- IPC protocol v1 and the 192-byte shared-memory header are unchanged.
- IPC protocol v1과 192-byte shared-memory header는 변경되지 않았습니다.
- Receiver identity, WebSocket action/state schemas, presets, and settings
  remain compatible.
- Receiver 신원, WebSocket action/state schema, preset과 settings는
  호환됩니다.
- HTTP endpoint paths, request payloads, and `ok: true` remain compatible;
  queued mutation responses add `accepted: true` and intentionally use `202`.
- HTTP endpoint 경로와 요청 payload는 유지하며 비동기 변경 응답만
  의도적으로 `202`를 사용합니다.

### Validation / 검증

- Local Windows Release validation completed all 568 CTest registrations:
  566 passed, 2 environment-dependent tests skipped, and 0 failed
  (59 core + 507 host + 2 focused endpoint). Stream Deck tests passed 22/22;
  lint, dependency audit, production bundle, official validation, and official
  packaging also passed.
- 로컬 Windows Release 검증은 CTest 568개 중 566개 통과, 환경 의존
  2개 건너뜀, 실패 0개로 완료했습니다(59 core + 507 host + 2 focused
  endpoint). Stream Deck 테스트 22/22과 lint, dependency audit, production
  bundle, 공식 validation·packaging도 통과했습니다.
- Exact-tag CI run `30571612012` passed cross-platform builds, all registered
  tests, Stream Deck audit/build/validation/official packaging, Windows
  executable identity and signature-state checks, checksum generation, and
  release publication. Public downloads and their hashes were independently
  verified against `checksums.sha256`.
- 정확 태그 CI run `30571612012`는 전체 플랫폼 빌드, 등록 테스트,
  Stream Deck audit/build/검증/공식 패키징, Windows 실행 파일 신원·서명
  상태, checksum 생성과 릴리즈 게시를 모두 통과했습니다. 공개 다운로드와
  해시는 `checksums.sha256`을 기준으로 독립 확인했습니다.
- No trusted Windows code-signing certificate is configured, so the Windows
  package is explicitly unsigned and may still trigger browser/SmartScreen
  reputation warnings. Verify `checksums.sha256`.
- 신뢰된 Windows 코드 서명 인증서가 없어 Windows 패키지는 명시적으로
  unsigned이며 브라우저/SmartScreen 평판 경고가 나타날 수 있습니다.
  `checksums.sha256`으로 검증하세요.
- Real-device audio, third-party VST crash containment, and macOS/Linux hardware
  behavior were not verified.
- 실기기 오디오, 제3자 VST crash containment, macOS/Linux 하드웨어 동작은
  검증하지 않았습니다.

### Downloads / 다운로드

- `DirectPipe-v4.2.7-Windows.zip`
- `DirectPipe-v4.2.7-macOS.dmg`
- `DirectPipe-v4.2.7-Linux.tar.gz`
- `com.directpipe.directpipe.streamDeckPlugin`
- `checksums.sha256`

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.3...v4.2.7

**전체 변경 비교**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.3...v4.2.7

**Release**: https://github.com/LiveTrack-X/DirectPipe/releases/tag/v4.2.7

**Release commit**: `c96415d4e7ff906460f501c786163a05dcb128a5`

---

## DirectPipe v4.2.6 (withdrawn)

v4.2.6 is the replacement for the withdrawn v4.2.5 release. It
prevents failed driver or endpoint changes from silently switching to arbitrary
Windows defaults, keeps the selected `CABLE Input`/output snapshot across input
loss and rollback, and makes the Windows package's signed or unsigned state
explicit in exact-tag CI.

v4.2.6은 회수된 v4.2.5를 대체합니다. 드라이버·endpoint 전환 실패
시 임의 Windows 기본 장치로 바뀌는 것을 막고, 입력 장치 손실과 rollback
과정에서도 선택한 `CABLE Input`/출력 스냅샷을 유지합니다. 현재 신뢰된
인증서가 없으므로 정확 태그 CI는 Windows 패키지를 unsigned로 명시합니다.

The v4.2.5 GitHub Release was removed after Chrome/Brave reputation warnings
were reported. Local Microsoft Defender scans found no threat, but the package
was unsigned. Its immutable source tag remains for provenance. v4.2.6 was also
withdrawn after the v4.2.7 hotfix audit found additional release blockers.

Chrome/Brave 신뢰 경고 제보 뒤 v4.2.5 GitHub Release를 내렸습니다. 로컬
Microsoft Defender 검사에서는 위협이 발견되지 않았지만 패키지가
미서명 상태였습니다. 출처 추적을 위해 불변 소스 태그는 유지합니다.
v4.2.7 핫픽스 점검에서 추가 릴리스 차단 결함이 확인되어 v4.2.6도
회수했습니다.

### Highlights / 주요 변경

- **Exact driver rollback**: Failed ASIO/Windows driver switches restore the
  saved input, output, sample rate, buffer size, and channel masks. An
  unavailable snapshot fails closed instead of accepting an unrelated default.
- **정확한 드라이버 rollback**: ASIO/Windows 전환 실패 시 저장된 입력,
  출력, sample rate, buffer size, channel mask를 복원합니다. 스냅샷이
  불가능하면 무관한 기본 장치를 수용하지 않고 안전하게 중단합니다.
- **Transactional endpoint selection**: Failed input or output selection
  recreates the previous duplex setup. A failed VB-Cable output choice no
  longer leaves an input-only or arbitrary-default stream.
- **원자적 endpoint 선택**: 입력·출력 선택 실패 시 이전 duplex 설정을
  재생성합니다. VB-Cable 출력 선택 실패가 입력 전용 또는 임의 기본 장치
  스트림을 남기지 않습니다.
- **Windows-mode endpoint preservation**: The first shared/low-latency/exclusive
  transition carries the current endpoints and cancels if they are unavailable
  in the target mode.
- **Windows 모드 endpoint 보존**: shared/low-latency/exclusive 첫 전환에서
  현재 endpoint를 이어 가며 대상 모드에 없으면 전환을 취소합니다.
- **Explicit same-device recovery**: A disabled `Device (Reconnect)`
  placeholder keeps the real same-name item clickable for one-step recovery.
  Windows Audio marks only the lost direction; ASIO marks both sides.
- **명시적 동일 장치 복구**: 비활성 `장치명 (Reconnect)` placeholder와
  실제 같은 이름의 항목을 분리해 한 번의 클릭으로 다시 열 수 있습니다.
  Windows Audio는 유실 방향만, ASIO는 양쪽을 복구 상태로 표시합니다.
- **Durable shutdown settings**: Non-plugin settings changed during a loading
  or partial VST chain are merged with the last complete chain. If none exists,
  a recovery sidecar restores them on next start without replacing plugin or
  slot files; Factory Reset removes the sidecar family.
- **종료 설정 내구성**: VST chain loading/partial 중 바뀐 비플러그인 설정은
  마지막 완전 chain과 병합합니다. 완전 chain이 없으면 recovery sidecar가
  plugin·slot 파일을 대체하지 않고 다음 실행에 설정을 복원하며 Factory
  Reset은 sidecar 파일군을 삭제합니다.
- **Bounded monitor recovery**: Missing devices keep three-second polling;
  enumerated endpoints that repeatedly fail to open back off
  3→10→20→30 seconds. Automatic diagnostics are emitted on the first and every
  twentieth attempt.
- **제한된 모니터 복구**: 누락 장치는 3초 polling을 유지하고, 열거되지만
  반복해서 열리지 않는 endpoint는 3→10→20→30초로 backoff합니다. 자동
  진단은 첫 시도와 매 20번째 시도에 남깁니다.
- **Driver-aware latency estimate**: Bottom `Latency` and control state use
  driver-reported input/output latency (one-buffer fallback per missing
  direction) plus active-chain PDC. Callback time remains a CPU/XRun diagnostic.
- **드라이버 기반 레이턴시 추정**: 하단 `Latency`와 control state는 드라이버
  보고 입·출력 지연(미보고 방향은 1버퍼 대체) + 활성 chain PDC를 사용하며,
  callback 시간은 CPU/XRun 진단으로만 유지합니다.
- **Correct Monitor route**: `Mon` is main input latency + PDC + adaptive
  monitor queue target + monitor output-device latency, excluding the main
  output device. An unopened Monitor is shown as unavailable.
- **정확한 Monitor 경로**: `Mon`은 메인 입력 지연 + PDC + adaptive monitor
  queue 목표량 + 모니터 출력 장치 지연이며, 무관한 메인 출력 지연은 제외하고
  열리지 않은 Monitor는 unavailable로 표시합니다.
- **Explicit signature state**: Exact-tag CI signs and verifies Windows
  binaries when both trusted PFX secrets are configured. With no certificate it
  requires every staged binary to be `NotSigned` and publishes an explicitly
  unsigned package; partial configuration or a mismatched signature state
  still fails.
- **명시적 서명 상태**: 신뢰된 PFX secret 두 개가 모두 있으면 Windows
  바이너리를 서명·검증하고, 현재처럼 인증서가 없으면 unsigned 패키지임을
  확인할 수 있도록 모든 staged 바이너리가 `NotSigned`여야 합니다. 불완전
  구성이나 서명 상태 불일치는 실패합니다.

### Compatibility / 호환성

- IPC protocol v1 and the 192-byte shared-memory header are unchanged.
- IPC protocol v1과 192-byte shared-memory header는 변경되지 않았습니다.
- Existing v4.2.x Receiver VST, Stream Deck actions/profiles, presets, and
  settings remain compatible. Bundled components are version-aligned only.
- 기존 v4.2.x Receiver VST, Stream Deck action/profile, preset, settings는
  호환됩니다. 번들 구성요소는 패키지 버전 정합성만 맞춥니다.

### Validation / 검증

- Local Windows Release validation completed all 546 CTest registrations:
  544 passed, 2 environment-dependent tests skipped, and 0 failed
  (59 core + 485 host + 2 focused endpoint).
- 로컬 Windows Release 검증은 CTest 546개 중 544개 통과, 환경 의존
  2개 건너뜀, 실패 0개로 완료했습니다(59 core + 485 host + 2 focused
  endpoint).
- Stream Deck tests passed 5/5; dependency audit reported 0 vulnerabilities,
  and the production bundle build and package validation succeeded.
- Stream Deck 테스트 5/5, 의존성 취약점 0건, 프로덕션 번들 빌드와 패키지
  검증이 통과했습니다.
- Publication remains blocked until exact-tag cross-platform CI, package
  validation, and checksums succeed. Authenticode is optional until a trusted
  certificate is available.
- 정확 태그 전체 플랫폼 CI, 패키지 검증, checksum 성공 전에는 게시하지
  않습니다. 신뢰된 인증서를 확보하기 전 Authenticode는 선택 사항입니다.
- Real audio hardware, third-party VST crash-containment, and macOS/Linux
  hardware behavior were not verified for this candidate.
- 이 후보는 실기기 오디오, 제3자 VST crash-containment, macOS/Linux
  하드웨어 동작을 검증하지 않았습니다.

### Planned downloads / 예정 다운로드

- `DirectPipe-v4.2.6-Windows.zip` — CI-built Windows stable artifact; unsigned
  unless trusted signing secrets are configured.
- `DirectPipe-v4.2.6-macOS.dmg` — macOS beta artifact, CI-built.
- `DirectPipe-v4.2.6-Linux.tar.gz` — Linux experimental artifact, CI-built.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck 4.2.6 package.
- `checksums.sha256` — SHA-256 manifest for all payload artifacts.

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.3...v4.2.6

**전체 변경 비교**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.3...v4.2.6

---

## DirectPipe v4.2.3

v4.2.3 is a latency-reporting reliability hotfix. Plug-ins that publish their
active PDC after being un-bypassed now update DirectPipe's existing `Latency:`
total and control API values without requiring another device or chain change.

v4.2.3은 레이턴시 보고 신뢰성 핫픽스입니다. bypass를 해제한 뒤 활성 PDC를
늦게 보고하는 플러그인도 추가 장치·체인 변경 없이 기존 `Latency:` 총합과
control API 값에 반영됩니다.

The displayed value remains a software estimate assembled from input/output
buffers, measured callback execution time, and active plug-in-reported PDC. It
is not a hardware loopback measurement.

표시값은 입력/출력 버퍼, 측정된 callback 실행시간, 활성 플러그인이 보고한
PDC를 합친 소프트웨어 추정값이며 하드웨어 loopback 실측값은 아닙니다.

The IPC wire ABI remains protocol v1 with the existing 192-byte header. Preset
schema, Stream Deck actions/request payloads, plug-in identities, and existing
control-state fields remain compatible with v4.2.x.

IPC wire ABI는 기존 192-byte header의 protocol v1을 유지합니다. preset
schema, Stream Deck action/request payload, 플러그인 식별자, 기존
control-state field는 v4.2.x 호환성을 유지합니다.

### Highlights / 주요 변경

- **Late PDC refresh**: DirectPipe observes latency changes from every live
  processor. The plug-in callback only sets a lock-free pending flag; the 30 Hz
  message-thread status update safely refreshes the graph before reading PDC.
- **지연 PDC 갱신**: 모든 live processor의 지연 변경을 관찰합니다. 플러그인
  callback은 lock-free pending flag만 설정하고 30 Hz message-thread 상태
  갱신이 PDC를 읽기 전에 graph를 안전하게 갱신합니다.
- **No redundant graph work**: Default host-display notifications are ignored
  when the active serial chain's reported PDC total did not change.
- **불필요한 graph 작업 방지**: 활성 직렬 체인의 보고 PDC 총합이 같으면
  default host-display 알림으로 graph를 다시 만들지 않습니다.
- **Queued recovery lifetime**: Pending message-thread recovery work is now
  invalidated whenever `AudioEngine` is destroyed, including partial-start
  paths that never reached the running state.
- **복구 작업 수명주기**: 실행 상태에 도달하지 못한 부분 시작 경로를
  포함해 `AudioEngine` 소멸 시 대기 중인 message-thread 복구 작업을
  무효화합니다.
- **One consistent total**: The bottom `Latency:` value, WebSocket
  `state.data.latency_ms`, HTTP `/api/perf` `latencyMs`, and chain PDC fields
  update from the same refreshed graph state.
- **일관된 총합**: 하단 `Latency:`, WebSocket `state.data.latency_ms`, HTTP
  `/api/perf` `latencyMs`, chain PDC field가 같은 갱신 graph 상태를 사용합니다.

### Upgrade Notes / 업그레이드 안내

- Update the DirectPipe host to receive the late-PDC refresh fix.
- 지연 PDC 갱신 수정은 DirectPipe host 업데이트가 필요합니다.
- Receiver/VST replacement is not required for IPC compatibility. The bundled
  Receiver is version-aligned to 4.2.3 only for package consistency.
- IPC 호환을 위해 Receiver/VST를 교체할 필요는 없습니다. 번들 Receiver는
  패키지 정합성을 위해서만 4.2.3으로 맞췄습니다.
- Existing Stream Deck packages remain compatible because no action, request,
  discovery, or state schema changed.
- action, request, discovery, state schema가 바뀌지 않아 기존 Stream Deck
  패키지도 호환됩니다.
- Existing v4.2.x presets and settings remain compatible.
- 기존 v4.2.x preset과 설정은 호환됩니다.

### Validation / 검증

- The local Windows Release run completed all 492 CTest registrations: 490
  passed, 2 environment-dependent tests skipped, and 0 failed. This includes
  all 18 `VSTChainTest` regressions and the queued-recovery lifetime check.
- 로컬 Windows Release CTest 492개를 실행해 490개 통과, 환경 의존 2개
  skip, 실패 0개를 확인했습니다. `VSTChainTest` 18개와 대기 중인 복구
  작업 수명주기 검사도 포함됩니다.
- The Release host, Receiver VST2, and Receiver VST3 targets built
  successfully; their Windows file/product versions report 4.2.3.
- Release host, Receiver VST2, Receiver VST3 target 빌드에 성공했으며
  Windows file/product version은 모두 4.2.3을 보고합니다.
- Exact-tag CI must pass Windows, macOS, Linux, Stream Deck, packaging, and
  checksum gates before publishing the release.
- 정확 태그 CI가 Windows, macOS, Linux, Stream Deck, packaging, checksum
  게이트를 통과한 뒤에만 릴리즈를 공개합니다.
- Real-device audio, third-party VST crash containment, and macOS/Linux hardware
  checks were not run.
- 실기기 오디오, 제3자 VST crash containment, macOS/Linux 하드웨어 검증은
  수행하지 않았습니다.

### Downloads / 다운로드

- `DirectPipe-v4.2.3-Windows.zip` — Windows stable artifact, CI-built.
- `DirectPipe-v4.2.3-macOS.dmg` — macOS beta artifact, CI-built.
- `DirectPipe-v4.2.3-Linux.tar.gz` — Linux experimental artifact, CI-built.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck 4.2.3 package.
- `checksums.sha256` — SHA-256 manifest generated after all artifacts are built.

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.2...v4.2.3

---

## DirectPipe v4.2.2

v4.2.2 makes the latency shown by DirectPipe a complete main-path estimate:
input/output buffers, measured callback execution time, and the active plug-in
chain's reported PDC are now combined in the bottom status bar and control APIs.

v4.2.2는 DirectPipe가 표시하는 레이턴시를 메인 경로의 총 추정값으로
개선합니다. 입력/출력 버퍼, 측정된 callback 실행시간, 활성 플러그인 체인이
보고한 PDC를 합산해 하단 상태바와 control API에 동일하게 제공합니다.

The IPC wire ABI remains protocol v1. Preset schema, Stream Deck action
UUIDs/request payloads, and plug-in identities remain compatible with v4.2.x.
Existing control-state fields retain their schema.

IPC wire ABI는 protocol v1을 유지합니다. preset schema, Stream Deck action
UUID/request payload, 플러그인 식별자는 v4.2.x 호환성을 유지하며 기존
control-state field schema도 그대로입니다.

### Highlights / 주요 변경

- **Total estimated latency**: The bottom-left status value, WebSocket
  `state.data.latency_ms`, and HTTP `/api/perf` `latencyMs` now combine estimated
  input/output buffers, callback execution time, and active-chain reported PDC.
  Bypassed plug-ins are excluded.
- **총 추정 레이턴시**: 하단 왼쪽 상태값, WebSocket
  `state.data.latency_ms`, HTTP `/api/perf` `latencyMs`가 입력/출력 버퍼
  추정치, callback 실행시간, 활성 체인 보고 PDC를 합산합니다. bypass된
  플러그인은 제외합니다.
- **Inspectable PDC**: Existing `chain_pdc_samples` and `chain_pdc_ms` fields
  expose the chain total while each plug-in reports its own `latency_samples`.
- **확인 가능한 PDC**: 기존 `chain_pdc_samples`, `chain_pdc_ms` 필드가 체인
  합계를 제공하고 각 플러그인은 자체 `latency_samples`를 보고합니다.
- **Initial state correctness**: The first snapshot now reports Stereo
  (`channel_mode: 2`), matching the AudioEngine default.
- **초기 상태 정합성**: 첫 snapshot이 AudioEngine 기본값과 같은 Stereo
  (`channel_mode: 2`)를 보고합니다.
- **Documentation sync**: API/state counts, hotkeys, slot filenames, Stream Deck
  metadata, latency definitions, known DSP limitations, and comment rules were
  synchronized with the implementation.
- **문서 동기화**: API/state 개수, 단축키, slot 파일명, Stream Deck metadata,
  레이턴시 정의, 알려진 DSP 제한, 주석 원칙을 구현과 동기화했습니다.

### Upgrade Notes / 업그레이드 안내

- Update the DirectPipe host to receive the new total-latency display and
  `latency_ms` calculation.
  Receiver/VST replacement is not required for IPC compatibility because the
  protocol and shared-memory layout are unchanged.
- 새 총 레이턴시 표시와 `latency_ms` 계산을 사용하려면 DirectPipe host를
  업데이트하세요.
  IPC protocol과 공유 메모리 layout은 그대로이므로 호환을 위해 Receiver/VST를
  반드시 교체할 필요는 없습니다.
- Existing Stream Deck packages continue to receive `latency_ms` without an
  action/request-schema change. The 4.2.2 package is recommended for version
  alignment, not required by a breaking protocol change.
- 기존 Stream Deck 패키지도 action/request schema 변경 없이 `latency_ms`를
  계속 받습니다. 4.2.2 패키지는 버전 정합성을 위해 권장하지만 breaking
  protocol 변경 때문에 필수인 것은 아닙니다.
- Existing v4.2.x presets and settings remain compatible.
- 기존 v4.2.x preset과 설정은 호환됩니다.

### Measurement Boundary / 측정 범위

- The displayed value is a software estimate, not hardware loopback
  measurement. Hidden converter/driver latency, Receiver/OBS buffering,
  scheduling, and downstream buffering are excluded.
- 표시값은 소프트웨어 추정치이며 하드웨어 loopback 실측값이 아닙니다. 숨은
  converter/driver 지연, Receiver/OBS buffering, scheduling, 후단 buffering은
  포함하지 않습니다.
- Incorrect latency reported by a plug-in can make the estimate differ from
  actual end-to-end latency.
- 플러그인이 잘못된 지연을 보고하면 추정치와 실제 end-to-end 지연이 다를 수
  있습니다.

### Validation / 검증

- The local Windows Release host-test target compiled successfully, focused
  latency/control regressions passed, and 490 CTest registrations were
  discovered.
- 로컬 Windows Release host-test target compile, 집중 레이턴시/control 회귀
  검사, CTest 490개 등록 확인을 완료했습니다.
- Stream Deck dependency audits reported 0 vulnerabilities; tests passed 5/5,
  and the production bundle build and package validation succeeded before
  tagging.
- 태그 전 Stream Deck dependency audit 취약점 0건, 테스트 5/5, production
  bundle build, package validation을 확인했습니다.
- Exact-tag CI must pass Windows, macOS, Linux, and Stream Deck build/test/package
  gates before publishing this release.
- 정확 태그 CI가 Windows, macOS, Linux, Stream Deck 빌드·테스트·패키지
  게이트를 통과한 뒤에만 이 릴리즈를 공개합니다.
- Real-device audio, third-party VST crash containment, and macOS/Linux hardware
  checks were not run.
- 실기기 오디오, 제3자 VST crash containment, macOS/Linux 하드웨어 검증은
  수행하지 않았습니다.

### Downloads / 다운로드

- `DirectPipe-v4.2.2-Windows.zip` — Windows stable artifact, CI-built.
- `DirectPipe-v4.2.2-macOS.dmg` — macOS beta artifact, CI-built.
- `DirectPipe-v4.2.2-Linux.tar.gz` — Linux experimental artifact, CI-built.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck 4.2.2 package.
- `checksums.sha256` — SHA-256 manifest generated after all artifacts are built.

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.1...v4.2.2

---

## DirectPipe v4.2.1

v4.2.1 is a reliability hotfix for recording, real-time audio lifecycle,
settings and preset durability, Receiver reconnection, update integrity, and the
release pipeline.

v4.2.1은 녹음, 실시간 오디오 수명 주기, 설정·프리셋 내구성, Receiver
재연결, 업데이트 무결성, 릴리즈 파이프라인을 보강하는 안정성 핫픽스입니다.

This release preserves the v4.2.x shared-memory layout, control API payloads,
preset schema, Stream Deck actions, and plug-in identities.

이번 릴리즈는 v4.2.x 공유 메모리 레이아웃, control API payload, preset
schema, Stream Deck action, 플러그인 식별자 호환성을 유지합니다.

### Highlights / 주요 변경

#### 1) Recording and playback / 녹음 및 재생

- Completed recordings remain playable across direct stop and audio-device
  stop/restart paths, and Play follows the currently selected folder.
- 직접 정지와 오디오 장치 정지·재시작 경로 모두 완료 녹음을 유지하며 Play는
  현재 선택한 폴더만 따릅니다.
- Start/stop lifecycle state is serialized, dropped writer blocks do not inflate
  duration, and rapid restarts use collision-free filenames.
- start/stop 수명 상태를 직렬화하고 drop된 writer 블록은 시간에 더하지 않으며
  빠른 재녹음은 충돌 없는 파일명을 사용합니다.

#### 2) Audio and VST lifecycle / 오디오 및 VST 수명 주기

- Noise removal now preserves its declared 480-sample latency through callback
  sizes up to 4096 without FIFO overwrite and reports zero latency while
  unsupported-rate passthrough is active.
- 노이즈 제거가 4096까지의 callback 크기에서 FIFO overwrite 없이 선언한
  480-sample 지연을 지키며 미지원 rate passthrough에서는 지연 0을 보고합니다.
- VST graph lifecycle and structural mutations share a control-side lock while
  the real-time process path remains lock-free.
- VST graph 수명과 구조 변경은 control-side lock으로 보호하고 실시간 처리
  경로는 lock-free로 유지합니다.

#### 3) Settings, slots, reset, and backup / 설정·슬롯·초기화·백업

- Autosave retains dirty state after failure; slot transitions and active-slot
  imports abort or roll back instead of silently losing the previous state.
- 자동 저장 실패 시 dirty 상태를 유지하며 슬롯 전환과 활성 슬롯 가져오기는
  기존 상태를 조용히 잃지 않고 중단하거나 롤백합니다.
- Reset/Clear invalidate stale asynchronous loads, report filesystem failures,
  reset the live recording folder, and Full Backup carries recording config.
- Reset/Clear가 오래된 비동기 load를 무효화하고 파일 오류를 보고하며 현재
  녹음 폴더를 초기화하고 Full Backup에 녹음 설정을 포함합니다.
- Preset and full-backup restore prepare every target plug-in chain before
  changing live state, then commit external state and swap the graph once with
  rollback protection.
- 프리셋과 전체 백업 복원은 모든 대상 플러그인 체인을 먼저 준비한 뒤 외부
  상태를 커밋하고 그래프를 한 번만 교체하며, 실패 시 롤백합니다.
- Asynchronous menu callbacks and settings/import failure paths now preserve UI
  lifetime and display actionable failures.
- 비동기 메뉴 callback과 설정/import 실패 경로가 UI 수명을 보호하고 사용자가
  조치할 수 있는 오류를 표시합니다.

#### 4) Receiver, control state, and updater / Receiver·제어 상태·업데이터

- Receiver detects a replaced POSIX shared-memory object or producer generation
  and reconnects without changing the IPC ABI. Mapping and latency notifications
  move off the audio callback with in-flight drain protection.
- Receiver가 교체된 POSIX 공유 메모리 객체 또는 producer generation을 감지해
  IPC ABI 변경 없이 재연결합니다. mapping·latency 알림은 in-flight 보호와
  함께 audio callback 밖으로 이동합니다.
- The control state includes the active preset. Windows in-app updater downloads
  for v4.2.0 and later require an exact readable `checksums.sha256` entry and a
  matching SHA-256; missing, unreadable, or mismatched metadata fails closed.
  Manual browser downloads are outside this check, and macOS/Linux open the
  release page.
- control state에 활성 preset을 포함합니다. Windows 인앱 업데이터는 v4.2.0
  이후 릴리즈에서 정확히 일치하는 `checksums.sha256` 항목과 SHA-256을
  필수 확인하며, 없거나 읽지 못하거나 불일치하면 fail-closed로 중단합니다.
  브라우저 수동 다운로드는 이 검사 밖이며 macOS/Linux는 릴리즈 페이지를
  엽니다.
- MIDI learn completion runs on the message thread; superseded, timed-out, or
  shutdown learn sessions cannot apply stale bindings.
- MIDI learn 완료는 message thread에서 처리하며 교체·timeout·shutdown된 세션은
  오래된 binding을 적용할 수 없습니다.

#### 5) Release CI / 릴리즈 CI

- CI validates/builds artifacts before publication and publishes from a draft
  only after assets and checksums are ready.
- CI가 게시 전에 산출물을 검증·빌드하고 asset과 checksum 준비 후 draft를
  공개합니다.
- Advertised Windows VST2/ASIO release support now requires both SDK inputs.
- Windows VST2/ASIO 릴리즈 지원을 표시하려면 두 SDK 입력이 모두 필요합니다.

### Upgrade Notes / 업그레이드 안내

- The IPC wire ABI remains protocol v1: existing field offsets and the 192-byte
  header are unchanged, while `producer_generation` uses previously reserved
  bytes. Existing v4.2.x presets, HTTP/WebSocket clients, and Stream Deck action
  UUIDs/request payloads remain compatible; the existing `state.data.preset`
  field now carries the active slot.
- IPC wire ABI는 protocol v1을 유지합니다. 기존 필드 offset과 192-byte header는
  그대로이며 `producer_generation`은 예약 영역을 사용합니다. 기존 v4.2.x
  preset, HTTP/WebSocket client, Stream Deck action UUID/request payload는
  호환되며 기존 `state.data.preset` 필드에 활성 슬롯 값이 채워집니다.
- Updating the bundled Receiver is recommended for its reconnect/lifecycle fixes;
  older Receivers remain ABI-compatible but do not gain those fixes. DirectPipe
  and Receiver still need matching sample rates.
- 재연결·수명 수정 적용을 위해 번들 Receiver 업데이트를 권장하며 DirectPipe와
  이전 Receiver도 ABI 호환이지만 해당 수정은 적용되지 않습니다. DirectPipe와
  Receiver sample rate는 계속 동일하게 맞춰야 합니다.

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

### Downloads / 다운로드

- `DirectPipe-v4.2.1-Windows.zip` — Windows stable artifact, CI-built.
- `DirectPipe-v4.2.1-macOS.dmg` — macOS beta artifact, CI-built.
- `DirectPipe-v4.2.1-Linux.tar.gz` — Linux experimental artifact, CI-built.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck 4.2.1 package.
- `checksums.sha256` — SHA-256 manifest generated by CI.

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.0...v4.2.1

**전체 변경 비교**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.0...v4.2.1

---

## DirectPipe v4.2.0

v4.2.0 is a substantial reliability release covering Windows endpoint recovery, real-time lifecycle safety, transactional settings and preset handling, control-server shutdown, the Windows updater, and Stream Deck reconnect/discovery behavior.

v4.2.0은 Windows endpoint 복구, 실시간 처리 수명 안전성, 설정 및 프리셋 트랜잭션, 제어 서버 종료, Windows updater, Stream Deck 재연결/검색 동작을 함께 강화한 대규모 안정성 릴리즈입니다.

This release does not change the control API, preset schema, Stream Deck action schema, or Receiver IPC protocol.

이번 릴리즈는 control API, preset schema, Stream Deck action schema, Receiver IPC protocol을 변경하지 않습니다.

---

### Highlights / 주요 변경

#### 1) Windows selected-input endpoint recovery / Windows 선택 입력 endpoint 복구
- DirectPipe now listens for Windows Core Audio property/state notifications for the selected capture endpoint. Changes to **Audio enhancements**, **Voice Clarity**, **Voice focus**, default format, or exclusive mode can trigger a short same-device settle and reopen even when JUCE emits no stop callback.
- DirectPipe는 선택된 capture endpoint의 Windows Core Audio 속성/상태 알림을 감지합니다. **오디오 향상 기능**, **Voice Clarity**, **음성 포커스**, 기본 형식, 독점 모드 변경 시 JUCE stop callback이 없어도 안정화 후 같은 장치를 다시 엽니다.
- Recovery is endpoint-event based, not silence based; physical microphone mute and normal silence do not trigger a reopen.
- 복구는 무음이 아닌 endpoint 이벤트 기반이므로 물리 마이크 mute와 정상 무음은 재오픈을 유발하지 않습니다.

#### 2) Audio and monitor lifecycle safety / 오디오 및 모니터 수명 안전성
- Saved targets remain authoritative during fallback/recovery, manual and automatic loss mutes retain separate ownership, and zero-active-channel setups retry with driver defaults. Output None selection or manual output-loss clearing cannot erase a concurrent input loss from aggregate device state or stop its recovery timer.
- fallback/복구 중에도 저장 target을 유지하고 수동 mute와 자동 loss mute ownership을 분리하며, zero-active-channel setup은 driver default로 재시도합니다. Output None 선택 또는 수동 output-loss 해제가 동시에 발생한 input loss를 aggregate device state에서 지우거나 복구 timer를 멈추지 않습니다.
- Deferred monitor work uses lifecycle generations, and MonitorOutput/SharedMemWriter drain in-flight real-time writes before ring reset or IPC unmap.
- 지연된 모니터 작업은 lifecycle generation을 확인하고, MonitorOutput/SharedMemWriter는 ring reset 또는 IPC unmap 전에 진행 중 RT write를 배출합니다.

#### 3) Control servers and Stream Deck / 제어 서버 및 Stream Deck
- WebSocket clients wait for handshake/initial-state completion before broadcasts, queued state is re-snapshotted, strict action parameters are enforced, and failed/dead clients are swept even while idle. HTTP uses validated fields plus stable plugin snapshots.
- WebSocket은 handshake/initial-state 완료 후 broadcast하고 queued state를 다시 snapshot하며 action parameter를 엄격히 검증하고 idle 상태에서도 실패/dead client를 sweep합니다. HTTP는 검증된 필드와 안정된 plugin snapshot을 사용합니다.
- The host announces its actual WebSocket port immediately and every 2 seconds; Stream Deck binds UDP discovery before its first WebSocket attempt, uses that port, and clears stale socket state across close/error/reconnect paths.
- host는 실제 WebSocket port를 시작 즉시와 2초마다 알리고, Stream Deck은 첫 WebSocket 연결 전에 UDP discovery를 bind하여 해당 port를 사용하며 close/error/reconnect 경로의 stale socket state를 정리합니다.
- Stream Deck production `ws` is updated to 8.21.0 with refreshed transitive dependencies; full and production-only `npm audit` report 0 vulnerabilities.
- Stream Deck production `ws`를 8.21.0으로 올리고 transitive dependency를 갱신했으며 전체/production-only `npm audit` 모두 취약점 0건입니다.

#### 4) Transactional settings and presets / 설정 및 프리셋 트랜잭션
- Settings-only/full-backup imports prevalidate audio, control, MIDI, and slots, then roll back touched state/files on late failure. Restore atomically claims the single load owner and refuses another active load, a partial chain, or an unstable VST transition; full restore requires explicit stable-runtime proof. Full export applies the same runtime guard before it can overwrite a valid slot.
- Settings-only/full-backup import는 audio, control, MIDI, slot을 먼저 검증하고 후반 실패 시 변경한 상태/파일을 롤백합니다. Restore는 단일 load ownership을 원자적으로 획득하고 다른 load, partial chain, unstable VST transition 중에는 시작하지 않으며 full restore는 명시적 stable-runtime proof를 요구합니다. Full export도 유효 slot을 덮어쓰기 전에 같은 runtime guard를 적용합니다.
- Structurally invalid preset objects, including non-canonical Base64 plugin state, no longer overwrite valid slots or block backup recovery even when primary repair-copy fails. Backup-only or locked-primary autosave recovery preserves an explicit `outputMuted` value, including during partial plugin loads. Incomplete full-backup export is reported as failure.
- non-canonical Base64 plugin state 등 구조적으로 잘못된 preset object는 유효 slot을 덮어쓰지 않으며 primary repair-copy가 실패해도 backup 복구를 막지 않습니다. primary가 없거나 잠긴 autosave family를 backup에서 복구할 때도 partial plugin load를 포함해 명시적 `outputMuted` 값을 보존합니다. 불완전한 full-backup export는 실패로 보고합니다.
- Legacy numeric slot families with only `.bak` or `.backup` remaining now migrate to canonical letter slots, keeping them loadable and present in full backups.
- 기본 파일 없이 `.bak` 또는 `.backup`만 남은 구형 숫자 슬롯도 표준 문자 슬롯으로 마이그레이션되어, 불러오기와 전체 백업에서 누락되지 않습니다.

#### 5) Safer Windows updater / 더 안전한 Windows updater
- Replacement files are staged before rotation, a known-good executable is retained for rollback, literal-percent paths are preserved, and release tags must be strict `MAJOR.MINOR.PATCH`.
- 교체 파일을 staging한 뒤 회전하고 known-good executable을 rollback용으로 유지하며, literal `%` path를 보존하고 엄격한 `MAJOR.MINOR.PATCH` tag만 허용합니다.
- The installer waits only for the exact DirectPipe PID that launched it, so another portable/installed instance cannot block the update forever.
- installer는 자신을 실행한 정확한 DirectPipe PID만 기다리므로 다른 portable/installed instance가 업데이트를 무기한 막지 않습니다.
- Finished/failed download workers are reaped so a later Update Now attempt can retry normally; a lifecycle mutex serializes worker reap/start/destruction around the reusable `std::thread`.
- 완료/실패한 download worker를 회수하여 이후 Update Now를 정상적으로 다시 시도할 수 있으며, lifecycle mutex가 재사용되는 `std::thread`의 회수/시작/종료를 직렬화합니다.

#### 6) Release gates and documentation / 릴리즈 게이트 및 문서
- Version gates cover app/Receiver/Stream Deck/package-lock/release bodies; CI runs Stream Deck tests/validation, endpoint tests, and verifies upload tag/source-version identity.
- version gate는 app/Receiver/Stream Deck/package-lock/release body를 확인하고, CI는 Stream Deck 및 endpoint 테스트와 upload tag/source version 일치를 검증합니다.

---

### Upgrade Notes / 업그레이드 안내
- **Windows v4.1.2 → v4.2.0: use the GitHub Windows ZIP once.** The new transactional updater protects updates performed by v4.2.0 and later, but cannot retroactively protect updater code already installed in v4.1.2.
- **Windows v4.1.2 → v4.2.0은 이번 한 번 GitHub Windows ZIP으로 수동 설치를 권장합니다.** 새 updater 보호는 v4.2.0 이후 업데이트부터 적용됩니다.
- **No Receiver VST replacement is required for these host-side fixes.** The control API and Receiver IPC protocol are unchanged; the bundled Receiver remains version-aligned for new installs.
- **호스트 쪽 수정에 Receiver VST 교체는 필요하지 않습니다.** control API와 Receiver IPC protocol은 동일하며 새 설치용 번들은 버전에 맞춰 유지됩니다.
- **Update the Stream Deck plugin to 4.2.0** for discovery/reconnect fixes. Install the GitHub `.streamDeckPlugin` manually until a separate Marketplace update is published.
- **검색/재연결 수정을 적용하려면 Stream Deck plugin을 4.2.0으로 업데이트하세요.** 별도 Marketplace 업데이트 전에는 GitHub `.streamDeckPlugin`을 수동 설치할 수 있습니다.
- Release assets are built by GitHub Actions after a notes-only release is created.
- notes-only release 생성 후 GitHub Actions가 최종 산출물을 빌드합니다.

---

### Validation / 검증
- Local Windows Release build passed for the app, Receiver VST2/VST3, core/host tests, and endpoint-watcher tests.
- 로컬 Windows Release에서 앱, Receiver VST2/VST3, core/host 및 endpoint-watcher 테스트 빌드가 통과했습니다.
- `directpipe-tests`: 52/52 passed.
- `directpipe-host-tests`: 391 total; 389 passed, 2 environment-dependent skips.
- `directpipe-endpoint-watcher-tests`: 2/2 passed.
- CTest: 445 registered, 0 failures, 2 environment-dependent skips.
- Stream Deck tests: 5/5 passed; build, package validation, and full/production-only `npm audit` (0 vulnerabilities) passed.
- version-only metadata, UTF-8 text integrity, JSON validation, and `git diff --check` passed.
- Focused endpoint automation passed; real-device **Audio enhancements**, **Voice Clarity**, **Voice focus**, and default-format changes remain an unexecuted manual field check for this local run.
- endpoint 집중 자동 테스트는 통과했지만 실제 장치의 **오디오 향상 기능**, **Voice Clarity**, **음성 포커스**, 기본 형식 변경은 이번 로컬 실행에서 수행하지 않아 수동 실기기 확인 항목으로 남습니다.
- Windows is the locally validated stable target; macOS remains beta and Linux remains experimental pending release CI and broader hardware validation.
- Windows는 로컬 검증된 stable target이며 macOS는 beta, Linux는 experimental입니다.

---

### Downloads / 다운로드
- `DirectPipe-v4.2.0-Windows.zip` — Windows stable artifact, CI-built.
- `DirectPipe-v4.2.0-macOS.dmg` — macOS beta artifact, CI-built.
- `DirectPipe-v4.2.0-Linux.tar.gz` — Linux experimental artifact, CI-built.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck 4.2.0 package, CI-built.
- `checksums.sha256` — CI-generated integrity hashes for all uploaded assets.

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.1.2...v4.2.0

**전체 변경 비교**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.1.2...v4.2.0

---

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

---

## DirectPipe v4.1.1

v4.1.1 is a host-side hotfix after v4.1.0. It fixes the preset-slot cache crash reported in issue #4, tightens settings backup/restore behavior, and includes audio-device restore hardening for channel fallback, ASIO sample-rate preservation, startup target waiting, and zero-active-channel recovery.

v4.1.1은 v4.1.0 이후의 호스트 쪽 핫픽스입니다. issue #4에서 보고된 프리셋 슬롯 캐시 크래시를 수정하고, 설정 백업/복원 동작을 더 정확하게 만들었으며, 채널 fallback, ASIO 샘플레이트 보존, 시작 시 저장 장치 대기, zero-active-channel 복구를 포함한 오디오 장치 복원 안정성을 강화했습니다.

This release does not change the control API, preset schema, Stream Deck action schema, or Receiver IPC protocol.

이번 릴리즈는 control API, preset schema, Stream Deck action schema, Receiver IPC protocol을 변경하지 않습니다.

---

### Highlights / 주요 변경

#### 1) Preset-slot cache crash fix / 프리셋 슬롯 캐시 크래시 수정
- Fixed a cache-hit preset switch crash when a slot contains duplicate VST plugins with the same name/path.
- 동일한 이름/경로의 VST 플러그인이 한 슬롯에 중복 포함된 경우 cache-hit 프리셋 전환 중 발생하던 크래시를 수정했습니다.
- Cache-hit state refresh now consumes the fresh slot-file state by plugin index, so duplicate entries no longer steal the first matching plugin's moved-out state block.
- Cache-hit 상태 갱신은 이제 새 슬롯 파일 상태를 플러그인 인덱스 기준으로 사용하므로, 중복 항목이 첫 번째 매칭 플러그인의 이동된 상태 블록을 잘못 가져가지 않습니다.
- Cache reuse now validates plugin descriptor identity (`uniqueId`, `fileOrIdentifier`, and format) when descriptor data is available.
- 캐시 재사용 시 descriptor 데이터가 있으면 플러그인 식별 정보(`uniqueId`, `fileOrIdentifier`, format)를 검증합니다.
- If a cached graph swap cannot be completed, DirectPipe keeps the old chain alive, discards the bad cache, and falls back to the normal async load path.
- 캐시된 그래프 교체를 완료할 수 없으면 DirectPipe는 기존 체인을 유지하고 잘못된 캐시를 버린 뒤 일반 async load 경로로 fallback합니다.
- Fresh slot-file state is copied into the cached-swap request instead of moved out before swap success, so the normal async fallback keeps saved plugin state intact.
- 새 슬롯 파일 상태는 swap 성공 전에 이동되지 않고 cached-swap 요청으로 복사되므로, 일반 async fallback에서도 저장된 플러그인 상태가 보존됩니다.

#### 2) Save/restore correctness / 저장 및 복원 정확도
- Settings-only `.dpbackup` restore no longer imports plugin chain state or changes `activeSlot`, preventing the next autosave from accidentally overwriting slot A or another quick slot.
- Settings-only `.dpbackup` 복원은 더 이상 플러그인 체인 상태를 가져오거나 `activeSlot`을 변경하지 않으므로, 다음 autosave가 slot A 또는 다른 quick slot을 의도치 않게 덮어쓰는 일을 방지합니다.
- Full backup export saves the active quick-slot file before collecting slot files, so the full backup matches the current chain state.
- Full backup export는 슬롯 파일을 수집하기 전에 활성 quick-slot 파일을 저장하므로, 전체 백업이 현재 체인 상태와 일치합니다.
- Full backup export now uses the same `.bak` fallback recovery as normal preset loading, so a recoverable slot is included even if the main slot file is corrupt.
- Full backup export는 일반 프리셋 로딩과 같은 `.bak` fallback 복구를 사용하므로, 메인 슬롯 파일이 손상되어도 복구 가능한 슬롯은 백업에 포함됩니다.
- Full restore is now exact: slots missing from the backup are removed from disk instead of leaving stale local slots behind.
- Full restore는 이제 정확 복원 방식입니다. 백업에 없는 슬롯은 오래된 로컬 슬롯으로 남지 않고 디스크에서 제거됩니다.
- Full restore clears stale `.bak`, `.backup`, `.tmp`, and legacy numeric slot families after successful slot restore and refreshes slot occupancy, slot names, and preload cache state.
- Full restore는 슬롯 복원이 성공하면 오래된 `.bak`, `.backup`, `.tmp`, legacy numeric slot 계열을 정리하고 slot occupancy, slot name, preload cache 상태를 갱신합니다.
- Settings/full-backup import now reports audio-settings import failure instead of silently returning success after a partial restore.
- Settings/full-backup import는 부분 복원 후 조용히 성공을 반환하지 않고 audio-settings import 실패를 보고합니다.

#### 3) Audio device restore hardening / 오디오 장치 복원 강화
- Saved channel masks now fall back to driver-default channel layouts when an explicit mask is rejected, rather than forcing channel 0 and accidentally downgrading stereo devices to mono.
- 저장된 channel mask가 거부되면 channel 0을 강제로 사용해 stereo 장치를 mono로 낮추는 대신 driver-default channel layout으로 fallback합니다.
- Explicit saved channel masks must intersect the runtime active channel mask before a device is considered ready.
- 명시적으로 저장된 channel mask는 runtime active channel mask와 교집합이 있어야 장치가 ready 상태로 인정됩니다.
- Startup, restore, driver switch, and reconnection paths detect devices that open with zero active input/output channels and retry with driver-default channels.
- 시작, 복원, 드라이버 전환, 재연결 경로는 active input/output channel이 0개로 열린 장치를 감지하고 driver-default channel로 다시 시도합니다.
- Startup restore pending now clears only after the saved device targets are actually active, so a fallback device does not masquerade as a completed restore.
- Startup restore pending은 저장된 장치 target이 실제로 활성화된 뒤에만 해제되므로, fallback 장치가 복원 완료로 잘못 처리되지 않습니다.
- ASIO restore accepts the actual device buffer size, but preserves an explicit requested sample rate when the driver reports a mismatched value.
- ASIO restore는 실제 장치 buffer size는 수용하지만, 드라이버가 다른 값을 보고해도 명시적으로 요청된 sample rate는 보존합니다.
- XRun diagnostics now include throttled device/SR/BS and active-channel snapshots to make restore and routing failures easier to trace.
- XRun diagnostics에는 throttled device/SR/BS 및 active-channel snapshot이 포함되어 복원 및 라우팅 실패를 더 쉽게 추적할 수 있습니다.

#### 4) Monitor output robustness / 모니터 출력 안정성
- Monitor output initialization now retries with driver-default output channels and, as a last resort, driver buffer defaults.
- Monitor output 초기화는 driver-default output channel로 재시도하며, 마지막 fallback으로 driver buffer default도 사용합니다.
- Monitor devices that open with zero active output channels are treated as not ready and schedule recovery instead of reporting Active.
- active output channel이 0개로 열린 monitor 장치는 Active로 보고되지 않고 not ready로 처리되어 복구를 예약합니다.

#### 5) Slot transition diagnostics / 슬롯 전환 진단
- Active slot transitions are now logged consistently across save, load, async load, cache hit, import, delete, clear, and GUI slot clicks.
- Active slot 전환은 save, load, async load, cache hit, import, delete, clear, GUI slot click 경로 전반에서 일관되게 로그로 남습니다.

---

### Upgrade Notes / 업그레이드 안내
- **No API/state model break / API 및 상태 모델 호환 유지**: Stream Deck, HTTP, WebSocket, presets, settings backups, and full backups remain compatible.
- **No API/state model break / API 및 상태 모델 호환 유지**: Stream Deck, HTTP, WebSocket, presets, settings backups, full backups는 계속 호환됩니다.
- **No Receiver VST replacement required for these host-side fixes**: existing Receiver VST installs do not need manual replacement to benefit from the preset-cache, backup/restore, and device-restore fixes. The bundled Receiver plugin remains version-aligned for new installs.
- **No Receiver VST replacement required for these host-side fixes / 호스트 쪽 수정에 Receiver VST 교체 불필요**: 기존 Receiver VST 설치본은 preset-cache, backup/restore, device-restore 수정 효과를 얻기 위해 수동 교체할 필요가 없습니다. 새 설치용 번들 Receiver plugin은 버전에 맞춰 유지됩니다.
- **Full restore behavior is stricter / 전체 복원 동작 강화**: `.dpfullbackup` restore now removes local quick slots that are not present in the backup.
- **Full restore behavior is stricter / 전체 복원 동작 강화**: `.dpfullbackup` 복원은 백업에 없는 로컬 quick slot을 제거합니다.
- **Release assets are built by CI / 릴리즈 산출물은 CI에서 빌드**: this GitHub release is created with notes only; GitHub Actions builds and uploads the Windows, macOS, Linux, Stream Deck, and checksum assets.
- **Release assets are built by CI / 릴리즈 산출물은 CI에서 빌드**: 이 GitHub release는 notes-only로 생성되며, GitHub Actions가 Windows, macOS, Linux, Stream Deck, checksum 산출물을 빌드하고 업로드합니다.

---

### Validation / 검증
- Local Windows Release verification built: `DirectPipe`, `DirectPipeReceiver_VST3`, `DirectPipeReceiver_VST`, `directpipe-tests`, and `directpipe-host-tests`.
- Local Windows Release 검증에서 `DirectPipe`, `DirectPipeReceiver_VST3`, `DirectPipeReceiver_VST`, `directpipe-tests`, `directpipe-host-tests` 빌드를 확인했습니다.
- `directpipe-tests`: 52 passed.
- `directpipe-tests`: 52개 통과.
- `directpipe-host-tests`: 310 tests; 308 passed, 2 environment-dependent tests skipped.
- `directpipe-host-tests`: 총 310개 중 308개 통과, 환경 의존 테스트 2개 skipped.
- `ctest`: 362 registered tests, 0 failed, 2 skipped.
- `ctest`: 등록 테스트 362개, 실패 0개, skipped 2개.
- Stream Deck `manifest.json`, `package.json`, and `package-lock.json` parsed successfully at version `4.1.1` / `4.1.1.0`.
- Stream Deck `manifest.json`, `package.json`, `package-lock.json`은 version `4.1.1` / `4.1.1.0` 기준으로 정상 parsing됐습니다.
- Text integrity, JSON metadata validation, Stream Deck package manifest validation, version metadata validation, and `git diff --check` passed.
- Text integrity, JSON metadata validation, Stream Deck package manifest validation, version metadata validation, `git diff --check`가 통과했습니다.
- Final release assets are rebuilt by GitHub Actions after the release is created.
- 최종 릴리즈 산출물은 release 생성 후 GitHub Actions에서 다시 빌드됩니다.

---

### Downloads / 다운로드
- `DirectPipe-v4.1.1-Windows.zip` — Windows stable artifact, CI-built.
- `DirectPipe-v4.1.1-Windows.zip` — Windows 안정 버전 산출물이며 CI에서 빌드했습니다.
- `DirectPipe-v4.1.1-macOS.dmg` — macOS beta artifact, CI-built.
- `DirectPipe-v4.1.1-macOS.dmg` — macOS beta 산출물이며 CI에서 빌드했습니다.
- `DirectPipe-v4.1.1-Linux.tar.gz` — Linux experimental artifact, CI-built.
- `DirectPipe-v4.1.1-Linux.tar.gz` — Linux experimental 산출물이며 CI에서 빌드했습니다.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck control package, CI-built.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck 제어 패키지이며 CI에서 빌드했습니다.
- `checksums.sha256` — generated by CI for all uploaded assets.
- `checksums.sha256` — 업로드된 모든 산출물에 대해 CI에서 생성한 checksum 파일입니다.

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.1.0...v4.1.1

**전체 변경 비교**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.1.0...v4.1.1

---

## DirectPipe v4.1.0

v4.1.0 is the next public release after v4.0.9. It includes the startup/tray device-restore and Audit Mode work prepared after v4.0.9, plus the adaptive PLL monitor-output bridge.

This release targets the real crackle/cutout and OUT XRun increase seen when the main OUT path and a separate monitor device run on independent WASAPI hardware clocks. Monitor output still receives the already processed post-VST/post-Safety buffer; it does not run the VST chain a second time.

v4.0.9 이후 준비했던 시작/트레이 장치 복원, Audit Mode 확장, adaptive PLL 기반 모니터 출력 브리지를 포함한 릴리즈입니다. 별도 모니터 장치 사용 시 발생하던 실제 끊김/지지직 및 OUT XRun 증가 문제를 줄이는 데 초점을 맞췄습니다.

---

### Highlights / 주요 변경

#### 1) Adaptive PLL monitor bridge / 적응형 PLL 모니터 브리지
- Monitor output now uses fractional read playback with linear interpolation on the monitor consumer side.
- Normal independent-device clock drift is absorbed by tiny playback-ratio corrections instead of frame discard.
- Runtime producer/consumer block-size changes re-prime the monitor bridge so the new target fill is honored before playback resumes.
- Partial monitor underruns re-prime before playback resumes, and repeated underruns gradually raise the target fill.
- Near-overflow emergency trim remains a safety fallback only and gets a short fade-in.
- This is not double processing: the VST chain still runs once in the main audio path.

#### 2) Startup/tray WASAPI restore / 시작 및 트레이 WASAPI 복원
- Settings restore now remembers saved input/output device targets even when Windows has not finished enumerating devices at login or tray startup.
- If the saved mic or output device is not available yet, DirectPipe keeps retrying for that explicit target instead of accepting an unintended fallback device as a successful restore.
- Output auto-mute remains active while the saved output target is missing, reducing wrong-device output risk.

#### 3) Main OUT XRun protection / 메인 OUT XRun 보호
- The main RT callback only pushes monitor audio when the separate monitor output is actually active.
- Monitor RMS level is cleared when monitor routing is disabled or inactive, keeping Audit Mode and UI meters from showing stale monitor audio.
- On Windows, the monitor callback remains below the main OUT callback priority by using MMCSS `Pro Audio` normal priority.
- The 60-second XRun rolling window now advances by every elapsed second after UI/device stalls, so stale buckets age out on time.
- Audit Mode `xrunDelta` now comes from a monotonic XRun event counter instead of comparing rolling-window totals.

#### 4) Audit diagnostics / Audit 진단 확장
- Audit Mode now emits rate-limited `AUDIO` and `MONITOR` snapshots from the message thread.
- `AUDIO` snapshots include desired/actual devices, SR/BS, CPU/proc time, XRun and callback-overrun deltas, mute/lost/IPC/limiter flags.
- `MONITOR` snapshots include fill/target/PLL state, block sizes, dropped/underrun/trim deltas, sample rate, ring capacity, and priming state.
- RT callbacks only update atomics/counters; logging stays on the message thread.

#### 5) Platform support / OS별 지원
- **Windows 10/11 x64**: stable release target. v4.1.0 local pre-release verification completed.
- **macOS 10.15+ universal**: beta. Source/CMake/CI release path exists; real-hardware audio validation is limited.
- **Linux x86_64**: experimental. Source/CMake/CI release path exists; behavior can vary by distribution, desktop environment, and audio server.
- **Stream Deck plugin**: separate cross-platform package targeting Windows 10+, macOS 10.15+, and Stream Deck 6.9+.

---

### Upgrade Notes / 업그레이드 안내
- **No API/state model break / API 및 상태 모델 호환 유지**: Stream Deck, HTTP, WebSocket, presets, and backups remain compatible.
- **No Receiver VST replacement required for these host-side fixes**: existing Receiver VST installs do not need manual replacement to benefit from the startup, monitor-output, and diagnostics fixes. The bundled Receiver plugin remains version-aligned for new installs.
- **Audit Mode / Audit 모드**: enable it only while reproducing monitor or XRun issues, then compare monitor-off vs monitor-on runs.
- **Release assets are built by CI / 릴리즈 산출물은 CI에서 빌드**: this GitHub release is created with notes only; GitHub Actions builds and uploads the Windows, macOS, Linux, Stream Deck, and checksum assets.

---

### Validation / 검증
- Local Windows Release verification built: `DirectPipe`, `DirectPipeReceiver_VST3`, `DirectPipeReceiver_VST`, `directpipe-tests`, and `directpipe-host-tests`.
- `directpipe-tests`: 52 passed.
- `directpipe-host-tests`: 291 passed, 2 environment-dependent tests skipped.
- Text integrity, JSON metadata validation, Stream Deck package manifest validation, and `git diff --check` passed.
- Final release assets are rebuilt by GitHub Actions after the release is created.

---

### Downloads / 다운로드
- `DirectPipe-v4.1.0-Windows.zip` — Windows stable artifact, CI-built.
- `DirectPipe-v4.1.0-macOS.dmg` — macOS beta artifact, CI-built.
- `DirectPipe-v4.1.0-Linux.tar.gz` — Linux experimental artifact, CI-built.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck control package, CI-built.
- `checksums.sha256` — generated by CI for all uploaded assets.

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.0.9...v4.1.0

For v4.0.9 and earlier history, see [CHANGELOG.md](../CHANGELOG.md).
