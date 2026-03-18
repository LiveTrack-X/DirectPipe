## DirectPipe v4.0.1 — Stability & Safety Patch

> **87건의 버그 수정 + 보안 강화 + Receiver/Stream Deck 개선 + AGC 튜닝.**
> 10라운드 코드 진단, 295 자동 테스트 통과, 모든 문서 동기화 완료.
>
> **87 bug fixes + security hardening + Receiver/Stream Deck improvements + AGC tuning.**
> 10 rounds of code audit, 295 automated tests passing, full documentation sync.

---

### Highlights / 주요 변경

#### Audio Safety / 오디오 안전
- **VST 크래시 방어** — 써드파티 플러그인 processBlock 크래시 시 앱 종료 대신 체인 자동 바이패스. Windows SEH + 기타 플랫폼 try/catch / VST crash guard: auto-bypass chain instead of app crash
- **NoiseRemoval 25시간 버그 수정** — uint32_t 링 버퍼 오버플로로 인한 영구 무음 해결 / Ring buffer overflow fix: permanent silence after ~25h
- **MMCSS 최적화** — RT 콜백에서 LoadLibraryA 제거, 함수 포인터 캐싱, release/acquire 배리어 (ARM 안전) / MMCSS optimization: cache function pointers, ARM-safe barriers

#### Receiver VST / 리시버 VST
- **OBS 모노 지원** — `isBusesLayoutSupported` 오버라이드 추가 / Mono output bus support for OBS
- **DAW 레이턴시 보정** — `setLatencySamples` 보고 / Report buffering latency to host
- **드리프트 보정 개선** — 히스테리시스 데드밴드로 Ultra Low 프리셋 마이크로갭 감소 / Drift compensation hysteresis
- **언더런 시 클릭 제거** — 저장된 버퍼 데이터 기반 페이드아웃 / Fade-out uses saved buffer tail

#### Stream Deck / 스트림 덱
- **자동 재연결** — 호스트 재시작 시 자동 복구 (autoReconnect: true) / Auto-reconnect on host restart
- **Auto 슬롯 지원** — `activeSlot: -1` 및 `auto_slot_active` 필드 처리 / Handle Auto slot state
- **볼륨 오버라이드 개선** — per-target 오버라이드로 무관한 타겟 차단 방지 / Per-target volume override

#### Security / 보안
- **연결 제한** — WebSocket 32개, HTTP 64개 동시 연결 제한 (DoS 방지) / Connection limits: WS 32, HTTP 64
- **IPC 권한** — POSIX sem/shm 0600 (소유자 전용) / POSIX IPC permissions restricted to owner

#### State & Presets / 상태 & 프리셋
- **activeSlot 브로드캐스트** — 0-4로 클램프, `auto_slot_active` 필드 추가 / Clamped activeSlot + auto_slot_active field
- **masterBypassed 정확도** — 미로드 플러그인 제외, 실제 바이패스 상태만 반영 / Requires at least one loaded plugin
- **자동 저장 보호** — 모든 체인 변경 작업에 loadingSlot_ 가드 / loadingSlot_ guard on all chain operations

#### AGC Tuning / AGC 튜닝
- **내부 오프셋 -4dB → -6dB** — 상용 레벨러에 가까운 출력 레벨 / Output level closer to commercial levelers

#### Build / 빌드
- **ARM 호환** — RNNoise x86 소스 아키텍처 가드 (Universal macOS 빌드 수정) / RNNoise x86 files guarded for ARM
- **조건부 VST2** — VST2 SDK 없이도 pre-release 테스트 통과 / Conditional VST2 target

---

### Upgrade Notes / 업그레이드 안내

> **Receiver VST 업데이트 권장**: OBS/DAW에서 사용 중인 DirectPipe Receiver를 v4.0.1로 교체하세요. 모노 출력 지원, 레이턴시 보정, 드리프트 개선 등 새 기능은 v4.0.1 Receiver에서만 지원됩니다.
>
> **Receiver VST update recommended**: Replace your DirectPipe Receiver in OBS/DAW with the v4.0.1 version. Mono output, latency reporting, and drift improvements require the updated Receiver.

> **Stream Deck 플러그인 업데이트 권장**: [Elgato Marketplace](https://marketplace.elgato.com/product/directpipe-29f7cbb8-cb90-425d-9dbc-b2158e7ea8b3)에서 최신 버전으로 업데이트하세요. 자동 재연결, Auto 슬롯 표시 등 새 기능은 새 버전에서만 사용 가능합니다.
>
> **Stream Deck plugin update recommended**: Update from [Elgato Marketplace](https://marketplace.elgato.com/product/directpipe-29f7cbb8-cb90-425d-9dbc-b2158e7ea8b3). Auto-reconnect and Auto slot display require the updated plugin.

### Downloads / 다운로드

| File | Description |
|------|-------------|
| `DirectPipe-v4.0.1-Windows.zip` | DirectPipe.exe + Receiver VST2(.dll) + VST3(.vst3) |
| `DirectPipe-v4.0.1-macOS.dmg` | DirectPipe.app + Receiver VST2(.vst) + VST3(.vst3) + AU(.component) |
| `DirectPipe-v4.0.1-Linux.tar.gz` | DirectPipe + Receiver VST2(.so) + VST3(.vst3) |
| `com.directpipe.directpipe.streamDeckPlugin` | Stream Deck Plugin (더블클릭 설치 / double-click to install) |
| `checksums.sha256` | SHA-256 체크섬 / Checksums for integrity verification |

> Synced: Stream Deck Plugin v4.0.1.0 / DirectPipe Receiver v4.0.1

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.0.0...v4.0.1
