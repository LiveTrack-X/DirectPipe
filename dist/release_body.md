## DirectPipe v4.0.3 - Startup UX polish + brickwall limiter safety

> Follow-up release after v4.0.2 stabilization.  
> Focus: startup behavior consistency, cross-platform wording consistency, and safer output limiting.

---

### Highlights / 주요 변경

#### 1) Start Minimized to Tray (Settings + Tray menu)
- Added `Start Minimized to Tray` option in both:
  - `Settings > Application`
  - tray icon right-click menu
- The two entry points stay synchronized through persisted app settings (`settings.dppreset`).

#### 2) Cross-platform auto-start label consistency
- Removed Windows-only hardcoded wording from the Settings side.
- Both Settings and tray now use a shared platform label source:
  - macOS: `Open at Login`
  - Windows/Linux: `Start with System`

#### 3) Safety Limiter brickwall behavior fix
- Upgraded limiter to a look-ahead brickwall style:
  - ~2ms look-ahead
  - instant attack, smooth release
  - hard per-sample ceiling clamp
- Result: fast transients are kept under configured ceiling more reliably.

#### 4) ASIO channel routing preservation on driver switch
- `DriverTypeSnapshot` now preserves channel masks (input/output) per driver.
- Switching driver type no longer resets ASIO channel selection to 1-2 unexpectedly.

---

### Upgrade Notes / 업그레이드 안내

- `Start Minimized to Tray` is applied on next process start.  
  If DirectPipe is already running in tray, fully quit and relaunch to verify startup behavior.
- No breaking changes to control API/state schema in this release.

---

### Downloads / 다운로드

| File | Description |
|------|-------------|
| `DirectPipe-v4.0.3-Windows.zip` | DirectPipe.exe + Receiver VST3 (`.vst3`) + optional VST2 (`.dll`, if built) |
| `DirectPipe-v4.0.3-macOS.dmg` | DirectPipe.app + Receiver VST3 (`.vst3`) + AU (`.component`) + optional VST2 (`.vst`, if built) |
| `DirectPipe-v4.0.3-Linux.tar.gz` | DirectPipe + Receiver VST3 (`.vst3`) + optional VST2 (`.so`, if built) |
| `com.directpipe.directpipe.streamDeckPlugin` | Stream Deck plugin package |
| `checksums.sha256` | SHA-256 checksums |

> Synced versions: Stream Deck plugin `4.0.3.0` / DirectPipe host `4.0.3`

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.0.2...v4.0.3
