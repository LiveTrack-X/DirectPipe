### [최신버전](../releases/latest)이 있습니다. 해당 버전을 받아 주세요.
### A [newer version](../releases/latest) is available. Please download the latest release.

---

## What's New in v4.0.0

### Cross-Platform Support / 크로스 플랫폼 지원
- **macOS (Beta)** — CoreAudio driver, CGEventTap global hotkeys (Accessibility permission), LaunchAgent auto-start, Gatekeeper handling
- macOS (베타) — CoreAudio 드라이버, CGEventTap 글로벌 핫키 (접근성 권한), LaunchAgent 자동 시작
- **Linux (Experimental)** — ALSA + JACK drivers, XDG autostart, PipeWire JACK compatibility
- Linux (실험적) — ALSA + JACK 드라이버, XDG 자동 시작, PipeWire JACK 호환
- **Platform abstraction layer** — PlatformAudio, AutoStart, ProcessPriority, MultiInstanceLock interfaces with per-platform implementations
- 플랫폼 추상화 레이어 — 오디오, 자동 시작, 프로세스 우선순위, 다중 인스턴스 인터페이스

### Data Safety / 데이터 안전
- **Cross-OS backup protection** — Backup files now include platform info; restoring on a different OS is blocked with a warning dialog
- 크로스 OS 백업 보호 — 백업 파일에 플랫폼 정보 포함, 다른 OS에서 복원 시 경고 다이얼로그 표시 후 차단
- **Platform-adaptive UI** — "Start with Windows" / "Start at Login" (macOS) / "Start on Login" (Linux)
- 플랫폼 적응형 UI — 자동 시작 라벨이 OS별로 표시

### Receiver Plugin / 리시버 플러그인
- **VST3 + AU formats** — Receiver now builds as VST2, VST3, and AU (macOS) across all platforms
- VST3 + AU 포맷 — 리시버를 VST2, VST3, AU (macOS)로 빌드
- **macOS IPC** — POSIX named semaphore for cross-process signaling (replaces eventfd)
- macOS IPC — POSIX named semaphore로 프로세스 간 시그널링

### CI / 빌드
- **3-platform CI** — GitHub Actions builds and tests on Windows, macOS, Linux with full artifact uploads
- 3 플랫폼 CI — Windows, macOS, Linux에서 빌드 + 테스트 + 아티팩트 업로드

### Documentation / 문서
- All docs updated for cross-platform: README, User Guide, Quick Start, Architecture, Build Guide, Platform Guide (new), API Examples, Stream Deck Guide, Log Rules
- 전체 문서 크로스 플랫폼 업데이트

> Synced: Stream Deck Plugin v4.0.0.0 / DirectPipe Receiver v4.0.0

### Downloads / 다운로드

| File | Description |
|------|-------------|
| `DirectPipe-v4.0.0-win64.zip` | DirectPipe.exe + DirectPipe Receiver.dll (Windows) |
| `DirectPipe-macOS.zip` | DirectPipe.app (macOS beta) |
| `DirectPipe-Linux.tar.gz` | DirectPipe binary (Linux experimental) |
| `com.directpipe.directpipe.streamDeckPlugin` | Stream Deck Plugin (double-click to install) |

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v3.10.0...v4.0.0
