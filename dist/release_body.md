## DirectPipe v4.0.3 - 시작 UX 정리 + 브릭월 리미터 안정화 / Startup UX polish + brickwall limiter safety

> v4.0.2 안정화 이후 후속 릴리즈입니다.  
> Follow-up release after v4.0.2 stabilization.
>
> 핵심: 시작 동작 일관성, 플랫폼 라벨 일관성, 출력 제한 안전성 강화.  
> Focus: startup behavior consistency, cross-platform wording consistency, and safer output limiting.

---

### Highlights / 주요 변경

#### 1) Start Minimized to Tray (Settings + Tray menu) / 시작 시 트레이 최소화
- `Start Minimized to Tray` 옵션을 두 위치에 추가:
  - `Settings > Application`
  - tray icon right-click menu
- 두 진입점은 저장 설정(`settings.dppreset`)으로 항상 동기화됩니다.
- Added `Start Minimized to Tray` option in both entry points, kept in sync via persisted settings.

#### 2) Cross-platform auto-start label consistency / 자동 시작 라벨 통일
- Settings 경로의 Windows 고정 문구를 제거했습니다.
- Settings와 Tray 모두 플랫폼별 라벨 소스를 공유합니다:
  - macOS: `Open at Login`
  - Windows/Linux: `Start with System`
- Removed Windows-only hardcoded wording and unified labels across Settings and tray.

#### 3) Safety Limiter brickwall behavior fix / 세이프티 리미터 브릭월 개선
- 리미터를 look-ahead 브릭월 스타일로 개선:
  - ~2ms look-ahead
  - instant attack, smooth release
  - hard per-sample ceiling clamp
- 순간 피크가 설정 ceiling을 넘지 않도록 더 안정적으로 제한합니다.
- Upgraded limiter behavior for more reliable ceiling enforcement on fast transients.

#### 4) ASIO channel routing preservation on driver switch / ASIO 채널 선택 보존
- `DriverTypeSnapshot`가 드라이버별 input/output channel mask를 보존합니다.
- 드라이버 전환 시 ASIO 채널 선택이 1-2로 초기화되지 않습니다.
- ASIO channel selection now survives driver-type switches.

---

### Upgrade Notes / 업그레이드 안내

- `Start Minimized to Tray`는 다음 프로세스 시작 시 반영됩니다. 트레이 실행 중이면 완전 종료 후 재실행하세요.  
  `Start Minimized to Tray` applies on next process start; fully quit and relaunch if currently running in tray.
- API/state 스키마 비호환 변경은 없습니다.  
  No breaking changes to control API/state schema.

---

### Downloads / 다운로드

| File | Description |
|------|-------------|
| `DirectPipe-v4.0.3-Windows.zip` | DirectPipe.exe + Receiver VST3 (`.vst3`) + optional VST2 (`.dll`, if built) |
| `DirectPipe-v4.0.3-macOS.dmg` | DirectPipe.app + Receiver VST3 (`.vst3`) + AU (`.component`) + optional VST2 (`.vst`, if built) |
| `DirectPipe-v4.0.3-Linux.tar.gz` | DirectPipe + Receiver VST3 (`.vst3`) + optional VST2 (`.so`, if built) |
| `com.directpipe.directpipe.streamDeckPlugin` | Stream Deck plugin package / 스트림덱 플러그인 패키지 |
| `checksums.sha256` | SHA-256 checksums / 무결성 체크섬 |

> Synced versions / 동기화 버전: Stream Deck plugin `4.0.3.0` / DirectPipe host `4.0.3`

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.0.2...v4.0.3
