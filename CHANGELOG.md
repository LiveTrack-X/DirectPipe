# Changelog

All notable changes to DirectPipe will be documented in this file.

---

## [Unreleased]

### Changed
- **Auto-start label (cross-platform UI consistency)**: Unified tray menu and Settings toggle to use a single platform label source via `Platform::getAutoStartLabel()` (`Open at Login` on macOS, `Start with System` on others), removing Windows-specific hardcoded wording from the Settings toggle path.

---

## [4.0.2] - 2026-03-19

### Added
- **Input Mute**: Independent input mute — silences microphone while VST chain continues processing (reverb tails fade naturally, AGC enters freeze). [INPUT] button in INPUT section, green=active / red=muted
- **VSTChain.isStable()**: Additional auto-save safety net
- **XRun**: Click CPU/XRun label to manually reset counter

### Changed
- **State Model (breaking)**: `active_slot` now 0-5 (5=Auto). `auto_slot_active` deprecated
- **Input Mute Toggle**: Now independent from Panic Mute
- **`input_muted` state field**: Now independent from `muted`
- **Mute Button UX**: INPUT/OUT/MON/VST/PANIC/AUTO button colors and labels now clearly distinguish normal/user-mute/panic-lock states (PANIC active shows `UNMUTE`)

### Fixed
- **XRun**: 60-second window drift — now uses real elapsed time
- **Panic**: Shows notification when recording was stopped during panic mute
- **UI**: Freeze Level label LUFS → dBFS, AGC labels clarified (Boost/Cut)
- **Noise Removal**: `holdSamples` and `gateSmooth` are now recalculated from runtime sample rate to preserve intended timing across SR changes

---

## [4.0.1] - 2026-03-19

### Fixed
- **NoiseRemoval**: Ring buffer uint32_t overflow causing permanent silence after ~25h continuous use
- **HTTP API**: Strict numeric validation — reject mixed alpha-numeric input (e.g., "abc0.5")
- **UI**: Plugin chain editor negative height on very small window
- **State Broadcast**: activeSlot clamped to 0-4, added `auto_slot_active` field for WebSocket/SD clients
- **Linux**: Complete XDG Desktop Entry Exec key character escaping per spec §6
- **macOS**: Notify user when hotkey accessibility permission not granted
- **Linux**: Show "unsupported" message in Hotkeys tab instead of non-functional UI
- **HTTP API**: Escape JSON special characters in API responses
- **Platform**: AutoStart setters return bool, notify user on failure
- **IPC**: Restrict POSIX semaphore/shm permissions to owner-only (0600)
- **XRun Tracking**: Device restart no longer clears XRun history — display persists for full 60s window
- **Audio RT**: Moved MMCSS LoadLibraryA call from RT callback to audioDeviceAboutToStart
- **MIDI**: Fixed LearnTimeout use-after-free when timer callback destroyed itself
- **Preset**: Added loadingSlot_ guard to Reset Auto to prevent intermediate state auto-save
- **Audio**: Recorder now stops on device loss (audioDeviceStopped) to prevent WAV corruption
- **Preset**: Auto first-click failure now sets partialLoad_ to prevent saving incomplete chain
- **Preset**: pendingSlot_ cleared on Factory Reset / Clear All Presets
- **State**: Plugin name now included in quickStateHash — name changes trigger WebSocket broadcast
- **WebSocket**: broadcastToClients releases clientsMutex_ before socket writes — prevents slow client blocking shutdown
- **IPC**: Receiver VST now calls detach() before close() on failed producer check — fixes spurious multi-consumer warning
- **Thread Safety**: audioDeviceAboutToStart non-atomic variable writes deferred to message thread via callAsync
- **Update**: PowerShell batch script now escapes single quotes in paths (e.g., O'Brien user profiles)
- **Audio**: Added null guard on outputChannelData for ASIO legacy drivers
- **Preset**: parseSlotFile now uses .bak fallback (loadFileWithBackupFallback) for crash resilience
- **UI**: Plugin removal now identifies by index+name, not name-only (fixes duplicate-name removal)
- **WebSocket**: Sends RFC 6455 close frame (1009) before disconnecting oversized-message clients
- **Settings**: importAll/importFullBackup now check platform compatibility internally
- **Platform**: macOS/Linux AutoStart uses atomicWriteFile for crash-safe plist/desktop writes
- **MIDI**: Guard LearnTimeout callAsync with alive_ flag — prevent use-after-free on rapid shutdown
- **Audio RT**: Add release/acquire barrier for MMCSS cached function pointers (ARM memory ordering safety)
- **Audio RT**: Guard partial GetProcAddress failure — null both pointers to allow retry
- **VST**: Validate nodeId in openPluginEditor second lock scope — prevent TOCTOU use-after-free
- **HTTP**: Close client sockets on HttpApiServer::stop() — prevent shutdown hang on blocked handlers
- **WebSocket**: Close socket on handshake failure — prevent zombie connections in clients_ vector
- **Cache**: Remove dangerous thread detach in PluginPreloadCache::cancelAndWait — leak instead of use-after-free
- **IPC**: Add atomic detach guard to RingBuffer — prevent null deref on concurrent detach/write
- **Audio**: Protect desiredDevice_ reads in audioDeviceAboutToStart with SpinLock (cross-thread safety)
- **Settings**: Guard SettingsAutosaver callAsync with alive_ flag — prevent use-after-free on fast quit
- **State**: Fix masterBypassed computation — no longer reports true when plugins are unloaded (DLL missing)
- **WebSocket**: Handle continuation frame (opcode 0x0) — prevent stream corruption on fragmented messages
- **Audio**: Log error when driver snapshot restore fails (setAudioDeviceSetup return value was ignored)
- **Preset**: Sync loadSlot path now sets partialLoad_ — prevent auto-save overwriting incomplete chains
- **WebSocket**: Check sendFrame return on initial state delivery — disconnect on failure
- **Preset**: Allow clearing slot name to empty (previously ghost name persisted in JSON)
- **Settings**: Reset activeSlot to A on .dpbackup import (slot files not included in backup)
- **Receiver**: Guard against processBlock before prepareToPlay — prevent infinite loop on empty buffer
- **Receiver**: Reset lastOutputSamples_ in releaseResources — prevent stale fade-out data
- **Receiver**: Add isBusesLayoutSupported for mono — fix OBS mono source left-channel-only output
- **Receiver**: Report latency via setLatencySamples — enable DAW latency compensation
- **Receiver**: Detect stale consumer_active after OBS crash — suppress false multi-consumer warning
- **Receiver**: Add drift compensation hysteresis — reduce micro-gaps at Ultra Low preset
- **Receiver**: Use saved buffer data in fade-out — eliminate click artifact on underrun
- **Stream Deck**: Enable autoReconnect — recover from host restarts without UDP
- **Stream Deck**: Clear stale state on reconnect — prevent displaying previous session data
- **Stream Deck**: Handle activeSlot -1 and auto_slot_active — correct preset display
- **Stream Deck**: Per-target volume override check — prevent unrelated targets from blocking
- **Build**: Guard RNNoise x86 sources for ARM compatibility — fix Universal macOS build
- **Build**: Conditional VST2 target in pre-release test — handle missing VST2 SDK
- **Build**: Remove stale DeviceSelector.cpp/h files
- **Audio**: Catch plugin processBlock exceptions — prevent app crash from third-party VST bugs
- **Audio**: Add 48kHz warning when Auto button clicked at non-48kHz sample rate
- **MIDI**: Overwrite duplicate CC bindings instead of creating duplicates
- **Audio**: Notify user when monitor sample rate mismatches main device
- **Audio**: Reset chainCrashed_ flag on device restart — user can recover after removing crashing plugin
- **Audio**: Revert MMCSS registration in audioDeviceStopped — prevent handle leak on device restart
- **Audio**: Protect audit log desiredDevice_ reads with SpinLock
- **Preset**: Fix onResetSettings loadingSlot_ timing — let loadFromFile manage the guard internally
- **Settings**: Clear partialLoad_ on successful self-heal in loadFromFile
- **State**: masterBypassed requires at least one loaded plugin — unloaded slots no longer trigger false bypass
- **Audio**: Windows SEH crash guard (__try/__except) for VST processBlock — catches access violations that try/catch misses
- **Audio**: chainCrashed_ now resets on chain modification (plugin add/remove), not on every device restart
- **Audio**: setInputDevice clears outputAutoMuted_ — prevents permanently stuck muted output after input device change
- **Audio**: mmcssTaskHandle_ is now std::atomic<HANDLE> — prevents data race between RT and device threads on ARM
- **Audio**: Guard SharedMemWriter::shutdown() against double call in AudioEngine::shutdown()
- **Settings**: Add null guard on getDynamicObject() in ControlMapping::load() — prevent crash on corrupt config
- **Audio**: Guard audioDeviceAboutToStart against BS=0 or SR=0 — prevent VST prepareToPlay with invalid params
- **Audio**: Move crash notification from RT thread to 30Hz message-thread timer — eliminate heap alloc in crash handler
- **Action**: Add loadingSlot_ guard to AutoProcessorsAdd fallback path — prevent intermediate state auto-save
- **File I/O**: Check atomicWriteFile return values at 4 sites — log warning on write failure instead of silent data loss
- **Security**: HTTP server enforces 64-handler connection limit — prevents DoS via connection flooding
- **Security**: WebSocket 32-client limit with atomic check+increment under clientsMutex_ — fixes TOCTOU race
- **AGC**: Internal LUFS offset increased from -4dB to -6dB — output level closer to commercial levelers

---

## [4.0.0] — Cross-Platform Release

> **v3에서 아키텍처 리팩토링 + 크로스플랫폼 확장 + 내장 프로세서 + 외부 제어 강화.**
> MainComponent를 7개 focused module로 분할, Platform/ 추상화 레이어 도입, 294+ 테스트.
>
> **Architecture refactoring + cross-platform + built-in processors + enhanced external control.**
> MainComponent split into 7 focused modules, Platform/ abstraction layer, 294+ tests.

### Added — 새 기능

- **Cross-platform support** — macOS (beta), Linux (experimental) alongside Windows (stable) / 크로스 플랫폼: macOS(베타), Linux(실험적)
- **Platform abstraction layer** (`Platform/`) — PlatformAudio, AutoStart, ProcessPriority, MultiInstanceLock with per-OS implementations / 플랫폼 추상화 레이어
- **Built-in Processors (내장 프로세서)** — VST 없이 기본 마이크 처리 제공:
  - **Filter** — HPF (60Hz ON) + LPF (16kHz OFF), 프리셋 + Custom 슬라이더 / High-pass + Low-pass filter
  - **Noise Removal** — RNNoise AI 노이즈 제거, 3단계 강도 (Light/Standard/Aggressive), 48kHz 전용, VAD 게이팅 / AI noise suppression with VAD gating
  - **Auto Gain** — LUFS 기반 자동 볼륨 레벨러, WebRTC 듀얼 envelope 패턴 / LUFS-based AGC with WebRTC dual-envelope
- **[Auto] 프리셋 슬롯** — A-E와 별도인 특수 슬롯 (인덱스 5). 원클릭 Filter+NR+AGC 기본 체인 / Special preset slot for one-click built-in processor chain
- **Safety Limiter** — VST 체인 후 모든 출력 경로 전에 적용되는 피드포워드 리미터 (0.1ms attack, 50ms release, -0.3dBFS) / Feed-forward limiter after VST chain
- **Stream Deck 10 액션** — 기존 7개 + Performance Monitor, Plugin Parameter (SD+), Preset Bar (SD+) / 10 actions (was 7)
- **19 통합 액션** — XRunReset, SafetyLimiterToggle, SetSafetyLimiterCeiling, AutoProcessorsAdd 추가 / 19 actions (was 15)
- **IPC consumer_active 감지** — Receiver VST 다중 연결 시 경고 표시 / SPSC violation warning when multiple Receivers connect
- **DPC Latency 대책** — MMCSS "Pro Audio" AVRT_PRIORITY_HIGH, IPC SetEvent 최적화, 콜백 오버런 감지 / MMCSS registration, IPC optimization, callback overrun detection
- **SHA-256 체크섬 검증** — 자동 업데이터 다운로드 무결성 확인 (`checksums.sha256`) / Auto-updater integrity check
- **48kHz NotificationBar 경고** — Auto/NR 추가 시 비-48kHz 샘플레이트 경고 / Warning when NR added at non-48kHz
- **ActionHandler** — 중앙 액션 라우팅, MainComponent에서 추출 / Centralized action routing
- **SettingsAutosaver** — dirty-flag + 1초 디바운스 자동 저장 / Auto-save with debounce
- **StatusUpdater** — 30Hz UI 상태 갱신, MainComponent에서 추출 / Periodic status updates
- **PresetSlotBar** — 프리셋 슬롯 A-E 버튼, 우클릭 컨텍스트 메뉴 / Preset slot buttons with context menu
- **UpdateChecker** — GitHub API 업데이트 체크 + 다이얼로그 / Update check + dialog
- **HotkeyTab / MidiTab / StreamDeckTab** — Controls 서브탭 분리 / Split into separate files
- **ActionResult** 패턴 — ok/fail 구조화 반환값 / Structured error handling
- **HTTP CORS preflight** — 브라우저 클라이언트용 OPTIONS 핸들러 / Browser client support
- **CONTRIBUTING.md / SECURITY.md** — 기여 가이드 + 보안 취약점 보고 절차 / Contributor and security guidelines
- **294+ 테스트** — NR, AGC, VSTChain, DeviceState, PresetManager, Safety Limiter 등 / Comprehensive test suite

### Fixed — 버그 수정

- **AGC freeze gate** — 무음 구간에서 게인을 0dB로 리셋하던 버그 → 현재 게인 유지 (hold) / Freeze now holds current gain instead of resetting to unity
- **AGC 오버슈트** — 1.5s LUFS 윈도우 + IIR envelope 이중 지연 → WebRTC 듀얼 envelope + direct gain으로 해결 / Dual-envelope resolves overshoot from double-smoothing
- **Mono 합산 +3dB** — L+R 합산 시 나눗셈 누락 → numInputChannels로 평균 / Fixed mono summing to average instead of raw sum
- **MIDI Continuous 게인** — InputGainAdjust에 0-1.0 delta 적용되던 버그 → SetVolume으로 절대 게인 설정 / Continuous CC now maps to absolute gain via SetVolume
- **HTTP gain 범위** — `/api/gain/:delta` 범위 검증 추가 (-2.0~2.0) / Added range validation
- **outputFifoWrite/Read 오버플로** — BuiltinNoiseRemoval의 `int` → `uint32_t` (12시간 후 정의되지 않은 동작 방지) / Prevent undefined behavior after ~12h
- **quickStateHash 누락** — chainPDCMs 필드 미포함 → 해당 변경이 브로드캐스트 안 되던 문제 / Added missing field to state hash
- **Panic mute 모든 액션 차단** — 패닉 뮤트 중 바이패스, 볼륨, 프리셋 등 모든 액션 차단 / All actions blocked during panic mute
- **Panic mute 녹음 중지** — 패닉 뮤트 시 녹음 자동 중지 (해제 시 재시작 안 함) / Recording stops on panic engage
- **HTTP gain delta 스케일링** — `*0.1f` 보상용 `*10.0f` 스케일링 문서화 / Documented gain scaling convention

### Changed — 변경사항

- **AGC 아키텍처** — IIR envelope → WebRTC 듀얼 envelope (fast 10ms/200ms + slow 0.4s LUFS) + direct gain / Architecture rewrite
- **AGC 기본값** — hiCorr 0.75→0.90, maxGain 24→22dB, LUFS 1.5s→0.4s, release 700→100ms, -4dB 타겟 오프셋 / Updated defaults
- **CI/CD 릴리스 에셋** — `win64.zip` → `Windows.zip`, `linux-x64.tar.gz` → `Linux.tar.gz` (업데이터 플랫폼 태그 일치) / Asset names match updater
- **CI checksums.sha256** — 릴리스 아티팩트 SHA-256 체크섬 자동 생성 / Auto-generated checksums
- **MainComponent** — ~1,835줄 → ~729줄 (7개 클래스 추출) / Reduced via extraction
- **기본 핫키** — 11개 (Ctrl+Shift+F6=Input Mute, Ctrl+Shift+1~3=Plugin 1-3 Bypass 등) / 11 default hotkeys
- **juce_cryptography** 모듈 추가 — SHA-256 검증용 / Added for checksum verification

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
