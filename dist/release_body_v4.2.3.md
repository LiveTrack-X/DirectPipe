## DirectPipe v4.2.3

v4.2.3 is a latency-reporting reliability hotfix. Plug-ins that publish their
active PDC after being un-bypassed now update DirectPipe's existing `Latency:`
total and control API values without requiring another device or chain change.

v4.2.3은 레이턴시 보고 신뢰성 핫픽스입니다. bypass를 해제한 뒤 활성 PDC를
늦게 보고하는 플러그인도 추가 장치·체인 변경 없이 기존 `Latency:` 총합과
control API 값에 반영됩니다.

The IPC wire ABI remains protocol v1 with the existing 192-byte header. Preset
schema, Stream Deck action UUIDs/request payloads, plug-in identities, and
existing control-state fields remain compatible with v4.2.x.

IPC wire ABI는 기존 192-byte header의 protocol v1을 유지합니다. preset
schema, Stream Deck action UUID/request payload, 플러그인 식별자, 기존
control-state field는 v4.2.x 호환성을 유지합니다.

---

### Highlights / 주요 변경

#### 1) Late PDC refresh / 지연 PDC 갱신

- DirectPipe now observes latency-change reports from every live processor.
- DirectPipe가 모든 live processor의 latency-change 보고를 관찰합니다.
- A plug-in that reports its active latency only after un-bypass updates the
  graph PDC on the next 30 Hz message-thread status tick.
- un-bypass 뒤에 활성 지연을 늦게 보고하는 플러그인도 다음 30 Hz
  message-thread 상태 tick에서 graph PDC를 갱신합니다.
- The plug-in callback only sets a lock-free pending flag. Graph mutation stays
  off the real-time audio thread.
- 플러그인 callback은 lock-free pending flag만 설정하며 graph 변경은
  real-time audio thread 밖에서 수행합니다.

#### 2) No redundant graph rebuilds / 불필요한 graph 재빌드 방지

- JUCE's default `updateHostDisplay()` flags include `latencyChanged`, even when
  the reported latency did not change.
- JUCE의 기본 `updateHostDisplay()` flag는 실제 보고 지연이 같아도
  `latencyChanged`를 포함합니다.
- DirectPipe now rebuilds the render sequence only when the active serial
  chain's reported PDC total differs from the graph total.
- DirectPipe는 활성 직렬 체인의 보고 PDC 총합이 graph 총합과 다를 때만
  render sequence를 다시 만듭니다.

#### 3) Queued recovery lifetime / 복구 작업 수명주기

- Destroying `AudioEngine` now invalidates pending message-thread recovery work
  even when initialization never reached the running state.
- 초기화가 실행 상태에 도달하지 못했더라도 `AudioEngine` 소멸 시 대기 중인
  message-thread 복구 작업을 무효화합니다.
- This prevents stale device-recovery callbacks from accessing a released
  engine after partial startup or test/device teardown.
- 부분 시작 또는 test/device 종료 뒤 오래된 장치 복구 callback이 해제된
  엔진에 접근하는 문제를 막습니다.

#### 4) One consistent latency total / 일관된 레이턴시 총합

- The bottom label remains `Latency:` and continues to combine estimated
  input/output buffers, measured callback execution time, and active
  plug-in-reported PDC.
- 하단 표기는 `Latency:`를 유지하며 입력/출력 버퍼 추정치, 측정된 callback
  실행시간, 활성 플러그인이 보고한 PDC를 계속 합산합니다.
- WebSocket `state.data.latency_ms`, HTTP `/api/perf` `latencyMs`,
  `chain_pdc_samples`, and `chain_pdc_ms` use the same refreshed graph state.
- WebSocket `state.data.latency_ms`, HTTP `/api/perf` `latencyMs`,
  `chain_pdc_samples`, `chain_pdc_ms`가 같은 갱신 graph 상태를 사용합니다.
- Bypassed plug-ins remain excluded from the chain total.
- bypass된 플러그인은 체인 총합에서 계속 제외됩니다.

#### 5) Documentation and test synchronization / 문서·테스트 동기화

- Host, bundled Receiver, Stream Deck metadata, release notes, current
  documentation, and the registered-test inventory are aligned to v4.2.3.
- Host, 번들 Receiver, Stream Deck metadata, 릴리즈 노트, 현재 문서,
  등록 테스트 인벤토리를 v4.2.3으로 맞췄습니다.
- Regression coverage includes late PDC publication after un-bypass, unchanged
  host-display notifications, preloaded-chain listener registration, and
  queued recovery invalidation during engine teardown.
- un-bypass 후 지연 PDC 보고, 변경 없는 host-display 알림, preloaded chain
  listener 등록, 엔진 종료 시 대기 중인 복구 작업 무효화를 회귀 검사에
  포함했습니다.

---

### Upgrade Notes / 업그레이드 안내

- Update the DirectPipe host to receive the late-PDC refresh fix.
- 지연 PDC 갱신 수정은 DirectPipe host 업데이트가 필요합니다.
- Receiver/VST replacement is not required for IPC compatibility. The bundled
  Receiver is version-aligned to 4.2.3 only for package consistency.
- IPC 호환을 위해 Receiver/VST를 교체할 필요는 없습니다. 번들 Receiver는
  패키지 정합성을 위해서만 4.2.3으로 맞췄습니다.
- Existing Stream Deck packages remain compatible because no action, request,
  discovery, or state schema changed. The 4.2.3 package is recommended only for
  version alignment.
- action, request, discovery, state schema가 바뀌지 않아 기존 Stream Deck
  패키지도 호환됩니다. 4.2.3 패키지는 버전 정합성을 위해서만 권장합니다.
- Existing v4.2.x presets and settings remain compatible.
- 기존 v4.2.x preset과 설정은 호환됩니다.

---

### Measurement Boundary / 측정 범위

- `Latency:` remains a software estimate, not a hardware loopback measurement.
- `Latency:`는 계속 소프트웨어 추정값이며 하드웨어 loopback 실측값이
  아닙니다.
- Hidden converter/driver latency, Receiver/OBS buffering, operating-system
  scheduling, and downstream application buffering are not included.
- 숨은 converter/driver 지연, Receiver/OBS buffering, 운영체제 scheduling,
  후단 application buffering은 포함하지 않습니다.
- A plug-in that reports incorrect PDC can make the estimate differ from actual
  end-to-end latency.
- 플러그인이 잘못된 PDC를 보고하면 추정치와 실제 end-to-end 지연이 다를 수
  있습니다.

---

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

---

### Downloads / 다운로드

- `DirectPipe-v4.2.3-Windows.zip` — Windows stable artifact, CI-built.
- `DirectPipe-v4.2.3-macOS.dmg` — macOS beta artifact, CI-built.
- `DirectPipe-v4.2.3-Linux.tar.gz` — Linux experimental artifact, CI-built.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck 4.2.3 package.
- `checksums.sha256` — SHA-256 manifest generated after all artifacts are built.

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.2...v4.2.3

**전체 변경 비교**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.2...v4.2.3
