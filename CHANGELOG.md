# Changelog

All notable changes to DirectPipe will be documented in this file.

---

## [4.0.0-alpha] — Cross-Platform Alpha

> **v3에서 아키텍처 리팩토링 + 크로스플랫폼 확장한 알파 버전입니다.**
> MainComponent를 7개 focused module로 분할, Platform/ 추상화 레이어 도입.
>
> **Architecture refactoring + cross-platform expansion from v3.**
> MainComponent split into 7 focused modules, Platform/ abstraction layer introduced.

### Added
- **Cross-platform support** — macOS (beta), Linux (experimental) alongside Windows (stable)
- **Platform abstraction layer** (`Platform/`) — PlatformAudio, AutoStart, ProcessPriority, MultiInstanceLock with per-OS implementations
- **ActionHandler** — Centralized action routing extracted from MainComponent
- **SettingsAutosaver** — Dirty-flag auto-save with debounce, extracted from MainComponent
- **StatusUpdater** — Periodic UI status updates, extracted from MainComponent
- **PresetSlotBar** — Preset slot A-E buttons, extracted from MainComponent
- **UpdateChecker** — GitHub API polling + update dialog, extracted from MainComponent
- **HotkeyTab / MidiTab / StreamDeckTab** — Controls sub-tabs split into separate files
- **ActionResult** pattern — Structured ok/fail returns replacing bare bool/void
- **HTTP CORS preflight** — OPTIONS request handler for browser-based clients (`Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS`)
- **HTTP input validation** — Volume and parameter endpoints validate numeric input (reject non-numeric strings like "abc")

### Fixed
- **Panic mute now blocks all actions** — PluginBypass, MasterBypass, InputGainAdjust, SetVolume (input), SetPluginParameter, LoadPreset, SwitchPresetSlot, NextPreset, PreviousPreset, RecordingToggle all check `isMuted()` before executing / 패닉 뮤트 중 모든 액션 차단
- **Panic mute stops recording** — Active recording is automatically stopped when panic mute engages (recording is mic output, so it must stop). Recording does not auto-restart on unmute / 패닉 뮤트 시 녹음 자동 중지 (해제 시 자동 재시작 안 함)
- **HTTP gain delta scaling** — `/api/gain/:delta` now correctly applies the requested gain change (was applying 1/10th of intended value due to ActionHandler's `*0.1f` scaling for hotkey steps) / HTTP 게인 델타 스케일링 수정
- **`input_muted` state field** — Clarified that `input_muted` mirrors `muted` (panic mute state). There is no independent input mute — `InputMuteToggle` triggers panic mute / `input_muted`는 `muted`와 동일 (독립 입력 뮤트 없음)

### Changed
- **CI/CD** — macOS build artifact changed from `.zip` to `.dmg` (updater safety: prevents v3 updater from downloading wrong binary)
- **MainComponent** — Reduced from ~1,835 lines to ~729 lines via extraction of 7 focused classes

---

## [3.10.3] — Final Stable Release

> **이 버전은 v3.10.x 라인의 최종 안정 릴리스입니다 (Windows 전용).**
> 이후 치명적 버그와 보안 패치만 적용됩니다. 새 기능과 크로스 플랫폼은 v4.0+을 참조하세요.
>
> **This is the final stable release of the v3.10.x line (Windows only).**
> Only critical bug fixes and security patches will be applied after this point.
> For new features and cross-platform support, see v4.0+.

### Fixed
- `outputPanelPtr_` 댕글링 포인터 위험 — `SafePointer<OutputPanel>`로 교체 / Dangling pointer risk — replaced with `SafePointer<OutputPanel>`
- 슬롯 이름 변경 다이얼로그 AlertWindow 이중 해제 — `enterModalState` 소유권 수정 / AlertWindow double-delete in slot rename dialog — fixed `enterModalState` ownership
- pre-release-test.sh 테스트 exe 경로 수정 + summary 렌더링 버그 수정 / Fixed test exe paths and summary rendering bug

### Changed
- README에 최종 안정 버전 안내 문구 추가 / Added final stable version notice to README
- CHANGELOG.md 생성 (전체 버전 히스토리) / Created CHANGELOG.md with full version history

---

## [3.10.2]

### Fixed
- ASIO device selection fallback and driver combo sync

## [3.10.1]

### Fixed
- VST bypass state, preset slot oscillation, and cache corruption

## [3.10.0]

### Added
- Multi-instance external control priority (Named Mutex coordination)
- Audio-only mode for secondary instances
- Driver type snapshot/restore (including Output None state)
- Per-direction device loss detection with "(Disconnected)" UI
- Quit button in Settings tab

### Fixed
- Output None bug when switching WASAPI to ASIO
- PluginChainEditor delete failure (name mismatch)
- LevelMeter smoother for visual consistency

## [3.9.12]

### Fixed
- ASIO startup buffer size bounce (wrong device selection)
- pushNotification MPSC race condition
- MonitorOutput alive_ lifetime guard
- AudioRingBuffer reset ordering
- StateBroadcaster slotNames hash

## [3.9.11]

### Fixed
- Output device switching fallback false positive
- Input channel loss when changing WASAPI output device

## [3.9.10]

### Fixed
- ASIO buffer size persistence (desiredBufferSize_ preserves user request)

## [3.9.9]

### Added
- Slot naming (right-click Rename, display as `A|Name`)
- Individual slot export/import (.dppreset)
- StateBroadcaster slot_names array

## [3.9.8]

### Added
- Device fallback protection (intentionalChange_ flag)
- Hotkey drag-and-drop reorder
- MIDI HTTP API test endpoints
- MIDI Learn / key recording cancel buttons

## [3.9.7]

### Added
- Instant preset switching (keep-old-until-ready)
- Settings scope separation (Save/Load vs Full Backup)
- Full Backup/Restore (.dpfullbackup)

## [3.9.6]

### Added
- Device auto-reconnection (dual mechanism: ChangeListener + timer polling)
- Monitor device reconnection
- Device combo click-to-refresh
- StateBroadcaster device_lost/monitor_lost

## [3.9.5]

### Added
- WASAPI Exclusive Mode (5th driver type)
- Audio optimizations (timeBeginPeriod, ScopedNoDenormals, RMS decimation)
- XRun monitoring (rolling 60s window)

## [3.9.4]

### Fixed
- Modal dialog fixes
- HTTP API input gain range
- Constructor SafePointer patterns

## [3.9.3]

### Fixed
- 25 bug fixes: thread safety, lifetime guards, server fixes, RT-safety

## [3.9.2]

### Added
- Categorized logging ([AUDIO], [VST], [PRESET], etc.)
- LogPanel batch flush optimization

### Fixed
- VSTChain lock-ordering hazard with DirectPipeLogger

## [3.9.0]

### Added
- Buffer display in status bar
- Sample rate propagation (main SR applies globally)
- Monitor SR mismatch detection
- Receiver VST SR warning

## [3.8.0]

### Added
- Auto-updater UI (download + auto-restart on Windows)
- CJK font fix (platform-specific font selection)

## [3.7.0]

### Fixed
- Plugin scanner crash (moreThanOneInstanceAllowed)

## [3.6.0]

### Added
- IPC Toggle action (Ctrl+Shift+I)
- Receiver VST buffer size configuration (5 presets)
- Panic mute lockout (all controls locked during panic)

## [3.5.0]

### Added
- NotificationBar (non-intrusive, color-coded, auto-fade)
- LogPanel (Settings tab with log viewer + maintenance tools)

### Fixed
- 4 callAsync lifetime guard fixes

## [3.4.0]

### Added
- System tray tooltip (current state on hover)
- Plugin scanner search/sort
- Audio recording (WAV, lock-free)
- Settings save/load (.dpbackup)
- MIDI plugin parameter mapping
- In-app auto-updater (version check)
