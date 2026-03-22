## DirectPipe v4.0.3

v4.0.2 이후, startup UX 정합성과 오디오 안전 경로를 보강한 릴리즈입니다.
This release improves startup UX consistency and the end-stage audio safety path after v4.0.2.

---

### Highlights / 주요 변경

#### 1) Start Minimized to Tray 옵션 / option
- `Settings > Application`과 트레이 우클릭 메뉴 양쪽에 시작 동작 토글을 추가했고, 앱 설정으로 항상 동기화됩니다.
- Added startup behavior toggle in both Settings and tray menu, synchronized through app settings.

#### 2) 자동 시작 라벨 통일 / cross-platform auto-start wording
- Settings와 Tray 모두 동일한 플랫폼 라벨 소스를 사용합니다 (`Open at Login` on macOS, `Start with System` on Windows/Linux).
- Settings and Tray now use one platform-adaptive label source (`Open at Login` on macOS, `Start with System` on Windows/Linux).

#### 3) Global Safety Guard runtime change / 글로벌 Safety Guard 런타임 변경
- 글로벌 stage를 zero-latency Safety Guard로 전환했습니다 (legacy `SafetyLimiter` class/API/action/state names 유지).
- Global stage now runs as zero-latency Safety Guard (legacy `SafetyLimiter` class/API/action/state names preserved).
- 동작: stereo-linked sample-peak guard + instant attack + smooth release + hard clamp, 기본 ceiling -0.3 dBFS.
- Behavior: stereo-linked sample-peak guard + instant attack + smooth release + hard clamp, default ceiling -0.3 dBFS.

#### 4) BuiltinAutoGain post limiter / BuiltinAutoGain 포스트 리미터 추가
- Auto Gain 내부에 constant-latency post limiter를 추가했습니다.
- Added constant-latency post limiter inside Auto Gain.
- 기본 ceiling -1.0 dBTP, 내부 고정 lookahead 1.0ms / release 50ms, constant delayed path, final hard clamp.
- Default ceiling -1.0 dBTP, fixed internal lookahead 1.0ms / release 50ms, constant delayed path, final hard clamp.
- TP-style detector는 prev/current + 3 linear interpolation points를 사용합니다 (strict EBU true-peak claim 없음).
- TP-style detector uses prev/current + 3 linear interpolation points (no strict EBU true-peak compliance claim).

#### 5) AGC Advanced: Limiter Ceiling only / AGC Advanced: Limiter Ceiling만 노출
- Advanced UI에 `Limiter Ceiling`만 추가했습니다 (`-2.0..0.0`, step `0.1`, `dBTP`, default `-1.0`).
- Added only `Limiter Ceiling` in Advanced UI (`-2.0..0.0`, step `0.1`, `dBTP`, default `-1.0`).
- lookahead/release 슬라이더는 추가하지 않았습니다.
- No lookahead/release sliders were added.

#### 6) ASIO 채널 라우팅 복원 개선 / ASIO channel restore fix
- 드라이버 전환 시 input/output channel mask를 스냅샷으로 보존해 ASIO 채널 선택이 초기화되지 않도록 수정했습니다.
- Preserved input/output channel mask snapshots across driver switches to avoid ASIO channel selection reset.

#### 7) docs/tests consistency updates / 문서·테스트 정합성 업데이트
- README, PRODUCT_SPEC, AUTO_DESIGN, USER_GUIDE, CONTROL_API, ReleaseNote를 현재 런타임 동작과 일치시켰습니다.
- Synced README, PRODUCT_SPEC, AUTO_DESIGN, USER_GUIDE, CONTROL_API, and ReleaseNote with current runtime behavior.
- SafetyLimiter/BuiltinAutoGain 관련 테스트를 최신 동작(Guard zero-latency, post limiter latency/ceiling/state)에 맞게 갱신했습니다.
- Updated SafetyLimiter/BuiltinAutoGain tests to match current behavior (Guard zero-latency, post limiter latency/ceiling/state).

---

### Upgrade Notes / 업그레이드 안내
- **No API/state model break / API·상태 모델 비호환 없음**: This release does not introduce breaking schema changes.
- **적용 시점 / apply timing**: `Start Minimized to Tray`는 다음 프로세스 시작 시 적용됩니다. 트레이에서 실행 중이면 완전 종료 후 재실행해 확인하세요 / applies on next process start.

---

### Downloads / 다운로드
- `DirectPipe-v4.0.3-Windows.zip`
- `DirectPipe-v4.0.3-macOS.dmg`
- `DirectPipe-v4.0.3-Linux.tar.gz`
- `com.directpipe.directpipe.streamDeckPlugin`
- `checksums.sha256`
