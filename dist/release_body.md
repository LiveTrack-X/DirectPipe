## DirectPipe v4.0.0 — Cross-Platform + Built-in Processors + Enhanced Control

> **v3에서 대규모 아키텍처 리팩토링 + 크로스플랫폼 확장 + 내장 프로세서 + 외부 제어 강화.**
> MainComponent를 7개 모듈로 분할, 294+ 자동 테스트, 19개 통합 액션, 10개 Stream Deck 액션.
>
> **Major architecture refactoring from v3 + cross-platform + built-in processors + enhanced external control.**
> MainComponent split into 7 modules, 294+ automated tests, 19 unified actions, 10 Stream Deck actions.

---

### 🎤 처음 사용자라면: [Auto] 버튼 하나로 시작하세요 / New? Start with one click

> **VST 플러그인을 모르셔도 괜찮습니다.** DirectPipe를 실행하고 왼쪽 하단의 초록색 **[Auto]** 버튼을 누르세요. 3가지 내장 프로세서가 자동으로 구성됩니다:
>
> 1. **배경 소음 제거** — 키보드, 에어컨, 팬 소리를 AI가 실시간 제거
> 2. **음량 자동 조절** — 작게 말하면 올리고, 크게 말하면 줄여서 일정한 볼륨 유지
> 3. **저주파 필터** — 마이크 험이나 진동 제거
>
> 추가 설정 없이 바로 방송/통화 품질이 개선됩니다. 나중에 세부 조정이 필요하면 각 프로세서를 클릭해서 파라미터를 변경할 수 있습니다.

> **Don't know VST plugins? No problem.** Launch DirectPipe and click the green **[Auto]** button at the bottom-left. 3 built-in processors are configured automatically:
>
> 1. **Noise Removal** — AI removes keyboard, AC, and fan noise in real-time
> 2. **Auto Gain** — Boosts quiet speech, reduces loud speech for consistent volume
> 3. **Low-cut Filter** — Removes microphone rumble and vibration
>
> Your broadcast/call quality improves immediately with zero configuration. Adjust individual parameters later by clicking each processor in the chain.

---

### Built-in Processors (내장 프로세서) — NEW

**[Auto] 버튼** 원클릭으로 3개 프로세서가 자동 구성됩니다. 개별 추가도 가능 (Add Plugin → Built-in 메뉴).
One-click **[Auto] button** configures all 3 processors. Can also be added individually via Add Plugin → Built-in menu.

- **Filter** — HPF (60Hz ON) + LPF (16kHz OFF). 에어컨 소음, 저주파 험 제거. 프리셋 또는 Custom 슬라이더 / Remove rumble, AC noise. Presets or custom slider
- **Noise Removal** — RNNoise AI 노이즈 제거. 3단계 강도 (Light/Standard/Aggressive). 48kHz 전용 / AI noise suppression with 3 strength levels. 48kHz only
- **Auto Gain** — LUFS 기반 자동 볼륨 레벨러. 작은 목소리 증폭 + 큰 소리 억제. 타겟 LUFS, Freeze Level, Max Gain 조절 가능 / LUFS-based automatic leveler. Configurable target, freeze, max gain

### Safety Limiter — NEW

VST 체인 후 모든 출력 경로에 적용되는 피드포워드 리미터. 플러그인 파라미터 변경이나 프리셋 전환 시 클리핑을 방지합니다.
Feed-forward limiter applied after VST chain, before all outputs. Prevents clipping from plugin changes or preset switches.

### Cross-Platform Support / 크로스 플랫폼 지원

- **Windows (Stable)** — WASAPI (Shared/Low Latency/Exclusive) + ASIO + DirectSound
- **macOS (Beta)** — CoreAudio, CGEventTap 글로벌 핫키, LaunchAgent 자동 시작 / CoreAudio, global hotkeys, LaunchAgent
- **Linux (Experimental)** — ALSA + JACK (PipeWire 호환), XDG 자동 시작 / ALSA + JACK, XDG autostart

### Stream Deck / 스트림 덱 — 10 Actions

기존 7개 + 3개 새 액션. SD+ 다이얼과 터치스크린 지원.
7 original + 3 new actions. SD+ dial and touchscreen support.

- **Performance Monitor** — CPU/레이턴시/XRun 실시간 표시 / Real-time CPU/latency/XRun display
- **Plugin Parameter (SD+)** — 다이얼로 VST 파라미터 직접 조절 / Direct VST parameter control via dial
- **Preset Bar (SD+)** — 터치스크린에 A-E 프리셋 바 표시 / Touchscreen preset bar

### DPC Latency Countermeasures / DPC 레이턴시 대책 — NEW

- **MMCSS "Pro Audio"** — 오디오 스레드에 AVRT_PRIORITY_HIGH 등록 (WASAPI + ASIO 모두 적용) / Audio thread registered at HIGH priority
- **IPC 최적화** — 데이터 전송 시에만 커널 시그널 (매 콜백 syscall 제거) / Signal only when data written
- **콜백 오버런 감지** — 처리 시간이 버퍼 주기를 초과하면 카운트 / Callback overrun detection

### Architecture / 아키텍처

- **MainComponent** — 1,835줄 → 729줄 (7개 모듈 추출) / Split into 7 focused modules
- **19 통합 액션** — 모든 외부 제어 (핫키/MIDI/SD/HTTP/WebSocket)가 ActionDispatcher를 통해 통합 / All external controls unified
- **294+ 자동 테스트** — GTest (코어 + 호스트), VSTChain, AGC, NR, Safety Limiter 등 / Comprehensive test suite
- **ActionResult** 패턴 — 구조화된 ok/fail 에러 처리 / Structured error handling

### Data Safety / 데이터 안전

- **Cross-OS 백업 보호** — 다른 OS에서 복원 시 차단 / Cross-OS restore blocked
- **SHA-256 체크섬** — 자동 업데이터 다운로드 무결성 검증 / Auto-updater integrity check
- **IPC 다중 연결 감지** — Receiver VST 2개 동시 연결 시 경고 / SPSC violation warning

### Upgrade Notes / 업그레이드 안내

> **Receiver VST 업데이트 권장**: OBS/DAW에서 사용 중인 DirectPipe Receiver를 v4.0.0으로 교체하세요. 구 버전도 동작하지만, 다중 연결 경고 등 새 기능은 v4.0.0 Receiver에서만 지원됩니다. ZIP/DMG 안에 포함된 Receiver 파일을 기존 VST 폴더에 덮어쓰세요.
>
> **Receiver VST update recommended**: Replace your DirectPipe Receiver in OBS/DAW with the v4.0.0 version. Old versions still work, but new features (multi-connection warning) require the v4.0.0 Receiver. Overwrite the existing files in your VST folder with the Receiver included in the ZIP/DMG.

> **Stream Deck 플러그인 업데이트 권장**: [Elgato Marketplace](https://marketplace.elgato.com/product/directpipe-29f7cbb8-cb90-425d-9dbc-b2158e7ea8b3)에서 최신 버전으로 업데이트하세요. 3개 새 액션 (Performance Monitor, Plugin Parameter, Preset Bar)은 새 버전에서만 사용 가능합니다.
>
> **Stream Deck plugin update recommended**: Update from [Elgato Marketplace](https://marketplace.elgato.com/product/directpipe-29f7cbb8-cb90-425d-9dbc-b2158e7ea8b3). 3 new actions (Performance Monitor, Plugin Parameter, Preset Bar) require the updated plugin.

### Downloads / 다운로드

| File | Description |
|------|-------------|
| `DirectPipe-v4.0.0-Windows.zip` | DirectPipe.exe + Receiver VST2(.dll) + VST3(.vst3) |
| `DirectPipe-v4.0.0-macOS.dmg` | DirectPipe.app + Receiver VST2(.vst) + VST3(.vst3) + AU(.component) |
| `DirectPipe-v4.0.0-Linux.tar.gz` | DirectPipe + Receiver VST2(.so) + VST3(.vst3) |
| `com.directpipe.directpipe.streamDeckPlugin` | Stream Deck Plugin (더블클릭 설치 / double-click to install) |
| `checksums.sha256` | SHA-256 체크섬 / Checksums for integrity verification |

> Synced: Stream Deck Plugin v4.0.0.0 / DirectPipe Receiver v4.0.0

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v3.10.0...v4.0.0
