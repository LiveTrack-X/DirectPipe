## DirectPipe v4.2.2

v4.2.2 makes the latency shown by DirectPipe a complete main-path estimate:
input/output buffers, measured callback execution time, and the active plug-in
chain's reported PDC are now combined in the bottom status bar and control APIs.
It also aligns the initial control state and current documentation with runtime
behavior.

v4.2.2는 DirectPipe가 표시하는 레이턴시를 메인 경로의 총 추정값으로
개선합니다. 입력/출력 버퍼, 측정된 callback 실행시간, 활성 플러그인 체인이
보고한 PDC를 합산해 하단 상태바와 control API에 동일하게 제공합니다. 초기
control state와 현재 문서도 실제 동작에 맞췄습니다.

The IPC wire ABI remains protocol v1. Existing field offsets and the 192-byte
header are unchanged. Preset schema, Stream Deck action UUIDs/request payloads,
and plug-in identities remain compatible with the v4.2.x lane. Existing
control-state fields retain their schema.

IPC wire ABI는 protocol v1을 유지하며 기존 필드 offset과 192-byte header는
변경되지 않았습니다. preset schema, Stream Deck action UUID/request payload,
플러그인 식별자는 v4.2.x 호환성을 유지하며 기존 control-state field schema도
그대로입니다.

---

### Highlights / 주요 변경

#### 1) Total estimated latency / 총 추정 레이턴시

- The bottom-left status value now combines the estimated input/output buffers,
  callback execution time, and reported PDC from the active plug-in chain.
- 하단 왼쪽 상태값은 입력/출력 버퍼 추정치, callback 실행시간, 활성 플러그인
  체인이 보고한 PDC를 합산합니다.
- Bypassed plug-ins are excluded from the chain total. Changing the chain or its
  bypass state updates the displayed total.
- bypass된 플러그인은 체인 합계에서 제외되며 체인 또는 bypass 상태가 바뀌면
  표시값도 갱신됩니다.
- WebSocket `state.data.latency_ms` and HTTP `/api/perf` `latencyMs` expose the
  same main-path total used by the status bar.
- WebSocket `state.data.latency_ms`와 HTTP `/api/perf` `latencyMs`는 상태바와
  같은 메인 경로 총합을 제공합니다.

#### 2) Inspectable plug-in PDC / 확인 가능한 플러그인 PDC

- Existing control-state `chain_pdc_samples` and `chain_pdc_ms` fields expose the
  chain breakdown; each plug-in entry reports its own `latency_samples`.
- 기존 control-state `chain_pdc_samples`, `chain_pdc_ms` 필드로 체인 내역을
  확인할 수 있고 각 플러그인 항목은 자체 `latency_samples`를 제공합니다.
- Existing clients that ignore unknown JSON fields remain compatible.
- 알 수 없는 JSON 필드를 무시하는 기존 client는 그대로 호환됩니다.

#### 3) Control-state correctness / 제어 상태 정합성

- The first state snapshot now reports Stereo (`channel_mode: 2`), matching the
  AudioEngine default before the first runtime refresh.
- 첫 state snapshot이 AudioEngine 기본값과 일치하도록 Stereo
  (`channel_mode: 2`)를 보고합니다.
- Regression coverage now checks active/bypassed chain latency, action mapping,
  initial state defaults, and WebSocket latency fields.
- 활성/bypass 체인 지연, action mapping, 초기 state 기본값, WebSocket
  레이턴시 필드에 대한 회귀 검사를 보강했습니다.

#### 4) Documentation and maintainability / 문서·유지보수성

- API/state counts, default hotkeys, slot filenames, Stream Deck metadata,
  latency definitions, and current UI behavior were synchronized with source.
- API/state 개수, 기본 단축키, slot 파일명, Stream Deck metadata, 레이턴시
  정의, 현재 UI 동작을 소스와 동기화했습니다.
- Source-comment guidance now favors intent, invariants, ownership, and RT/thread
  constraints while removing stale implementation narration.
- 소스 주석은 의도, 불변조건, 소유권, RT/thread 제약을 설명하도록 원칙을
  명확히 하고 오래된 구현 설명을 정리했습니다.

---

### Upgrade Notes / 업그레이드 안내

- DirectPipe host update is required to receive the new total-latency display
  and total calculation in `latency_ms`.
- 새 총 레이턴시 표시와 `latency_ms` 총합 계산을 사용하려면 DirectPipe host를
  업데이트해야 합니다.
- Receiver/VST replacement is not required for IPC compatibility: protocol v1
  and the shared-memory layout are unchanged. The bundled Receiver version is
  aligned to 4.2.2 for package consistency.
- IPC 호환을 위해 Receiver/VST를 반드시 교체할 필요는 없습니다. protocol v1과
  공유 메모리 layout은 그대로이며 번들 Receiver 버전만 패키지 정합성을 위해
  4.2.2로 맞췄습니다.
- Existing Stream Deck plug-ins continue to receive `latency_ms` without an
  action or request-schema change. Installing the 4.2.2 package is recommended
  for version alignment, not required by a breaking protocol change.
- 기존 Stream Deck 플러그인도 action/request schema 변경 없이
  `latency_ms`를 계속 받습니다. 4.2.2 패키지 설치는 버전 정합성을 위해
  권장하지만 breaking protocol 변경 때문에 필수인 것은 아닙니다.
- Existing v4.2.x presets and settings remain compatible.
- 기존 v4.2.x preset과 설정은 호환됩니다.

---

### Measurement Boundary / 측정 범위

- This value is an estimate assembled from software-visible timing and
  plug-in-reported latency. It is not a hardware loopback measurement.
- 이 값은 소프트웨어에서 확인 가능한 timing과 플러그인 보고 지연을 합친
  추정치이며 하드웨어 loopback 실측값은 아닙니다.
- It does not include hidden converter/driver latency, Receiver/OBS buffering,
  operating-system scheduling, or downstream application buffering.
- 숨은 converter/driver 지연, Receiver/OBS buffering, 운영체제 scheduling,
  후단 application buffering은 포함하지 않습니다.
- A plug-in that reports incorrect PDC can make the total differ from actual
  end-to-end latency.
- 플러그인이 잘못된 PDC를 보고하면 총합과 실제 end-to-end 지연이 다를 수
  있습니다.

---

### Validation / 검증

- The local Windows Release host-test target compiled successfully, the focused
  latency/control regressions passed, and 490 CTest registrations were
  discovered.
- 로컬 Windows Release host-test target compile, 집중 레이턴시/control 회귀
  검사, CTest 490개 등록 확인을 완료했습니다.
- Stream Deck dependency audits reported 0 vulnerabilities; tests passed 5/5,
  and the production bundle build and package validation succeeded before
  tagging.
- 태그 전 Stream Deck dependency audit 취약점 0건, 테스트 5/5, production
  bundle build, package validation을 확인했습니다.
- Exact-tag CI must build/test Windows, macOS, Linux, and Stream Deck artifacts
  before it publishes the GitHub Release.
- 정확 태그 CI가 Windows, macOS, Linux, Stream Deck 산출물을 빌드·검사한
  뒤에만 GitHub Release를 공개합니다.
- Real-device audio, third-party VST crash containment, and macOS/Linux hardware
  checks were not run.
- 실기기 오디오, 제3자 VST crash containment, macOS/Linux 하드웨어 검증은
  수행하지 않았습니다.

---

### Downloads / 다운로드

- `DirectPipe-v4.2.2-Windows.zip` — Windows stable artifact, CI-built.
- `DirectPipe-v4.2.2-macOS.dmg` — macOS beta artifact, CI-built.
- `DirectPipe-v4.2.2-Linux.tar.gz` — Linux experimental artifact, CI-built.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck 4.2.2 package.
- `checksums.sha256` — SHA-256 manifest generated after all artifacts are built.

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.1...v4.2.2

**전체 변경 비교**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.1...v4.2.2
