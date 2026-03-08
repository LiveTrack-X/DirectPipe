### [최신버전](../releases/latest)이 있습니다. 해당 버전을 받아 주세요.
### A [newer version](../releases/latest) is available. Please download the latest release.

---

## What's New in v3.10.0

### Multi-Instance & Portable Mode / 다중 인스턴스 & 포터블 모드
- **Multi-instance external control priority** — Named Mutex prevents hotkey/MIDI/WebSocket/HTTP conflicts between instances
- 다중 인스턴스 외부 제어 우선순위 — Named Mutex로 인스턴스 간 단축키/MIDI/WebSocket/HTTP 충돌 방지
- **Audio-only mode** — Secondary instances run with controls disabled (title bar, status bar, tray indicators)
- 오디오 전용 모드 — 보조 인스턴스는 제어 비활성화 상태로 실행 (타이틀바, 상태바, 트레이 표시)

### Audio Engine / 오디오 엔진
- **Driver type snapshot/restore** — Settings saved per driver type, restored when switching back (WASAPI <-> ASIO)
- 드라이버 타입별 설정 저장/복원 — 드라이버 전환 시 이전 설정 자동 복원
- **Device loss UI** — Combos show "(Disconnected)" on physical device loss instead of falling back to another device
- 디바이스 분리 UI — 물리적 장치 분리 시 콤보에 "(Disconnected)" 표시, 다른 장치로 전환하지 않음
- **Per-direction device loss handling** — Input/output loss tracked independently with edge-detection notifications
- 방향별 디바이스 손실 처리 — 입력/출력 손실 독립 추적 + 알림
- **Output "None" state fix** — WASAPI "None" -> ASIO switch no longer leaves OUT mute button stuck
- 출력 "None" 상태 수정 — WASAPI "None" -> ASIO 전환 시 OUT 뮤트 버튼 고착 방지

### UI
- **LevelMeter** — Smoother release (~230ms half-life), finer visual threshold
- 레벨미터 — 부드러운 릴리스, 세밀한 시각 임계값

### Stability / 안정성
- PluginPreloadCache `cancelAndWait` heap-allocated join state (dangling reference fix)
- HttpApiServer thread join outside mutex (deadlock prevention)
- StateBroadcaster hash includes more fields for accurate dirty detection
- PluginChainEditor delete fix (name mismatch from row prefix)

> Synced: Stream Deck Plugin v3.10.0.0 / DirectPipe Receiver v3.10.0

### Downloads / 다운로드

| File | Description |
|------|-------------|
| `DirectPipe-v3.10.0-win64.zip` | DirectPipe.exe + DirectPipe Receiver.dll |
| `com.directpipe.directpipe.streamDeckPlugin` | Stream Deck Plugin (double-click to install) |

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v3.9.12...v3.10.0
