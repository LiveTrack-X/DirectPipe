# DirectPipe — Claude Code Project Guide v6.0

## Project Description
Real-time VST2/VST3 host for Windows. Processes microphone input through a plugin chain. Main output goes to AudioSettings Output device (WASAPI/ASIO), with optional separate monitor output (headphones) via independent WASAPI device. Focused on external control (hotkeys, MIDI, Stream Deck, HTTP API) and fast preset switching. Similar to Light Host but with remote control capabilities.

Windows용 실시간 VST2/VST3 호스트. 마이크 입력을 플러그인 체인으로 처리. 메인 출력은 AudioSettings Output 장치(WASAPI/ASIO)로 직접 전송, 별도 WASAPI 모니터 출력(헤드폰) 옵션. 외부 제어(단축키, MIDI, Stream Deck, HTTP API)와 빠른 프리셋 전환에 초점.

## Core Principles
1. WASAPI Shared default + ASIO support (non-exclusive mic access)
2. VST2 + VST3 hosting with drag-and-drop chain editing
3. Main output via AudioSettings Output device + optional Monitor (headphones) via separate WASAPI
4. Quick Preset Slots A-E (chain-only, includes plugin internal state)
5. External control: Hotkey, MIDI, WebSocket, HTTP -> unified ActionDispatcher
6. System tray resident (close = minimize to tray)
7. Out-of-process VST scanner (crash-safe)

## Tech Stack
- C++17, JUCE 7.0.12, CMake 3.22+, project version 3.5.0
- WASAPI Shared Mode + ASIO (Steinberg ASIO SDK)
- VST2 SDK 2.4 + VST3
- WebSocket: JUCE StreamingSocket + RFC 6455 (handshake, framing, custom SHA-1)
- HTTP: JUCE StreamingSocket manual parsing
- Stream Deck: @elgato/streamdeck SDK v2.0.1, SDKVersion 3, Node.js 20

## Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Architecture
```
Mic -> WASAPI Shared / ASIO -> [Mono/Stereo] -> VSTChain -> Main Output (outputChannelData)
                                                              \
                                                         OutputRouter -> Monitor (Headphones, separate WASAPI)

External Control:
Hotkey/MIDI/WebSocket/HTTP -> ControlManager -> ActionDispatcher
  -> VSTChain (bypass), OutputRouter (volume), PresetManager
```

## Key Implementations
- **ASIO + WASAPI dual driver**: Runtime switching. ASIO: single device, dynamic SR/BS, channel routing. WASAPI: separate I/O, fixed lists.
- **Main output**: Processed audio written directly to outputChannelData (AudioSettings Output device). Works with both WASAPI and ASIO.
- **Monitor output**: Separate WASAPI AudioDeviceManager + lock-free AudioRingBuffer for headphone monitoring. Independent of main driver type (works even with ASIO). Configured in Monitor tab. Status indicator (Active/Error/No device).
- **Quick Preset Slots (A-E)**: Chain-only. Plugin state via getStateInformation/base64. Async loading (replaceChainAsync). Same-chain fast path = instant switch.
- **Out-of-process VST scanner**: `--scan` child process. Auto-retry (5x), dead man's pedal. Blacklist for crashed plugins. Log: `%AppData%/DirectPipe/scanner-log.txt`.
- **Plugin chain editor**: Drag-and-drop, bypass toggle, native GUI edit. Safe deletion via callAsync.
- **Tabbed UI**: Audio/Monitor/Controls/Log tabs in right column. Monitor tab includes Recording section. Controls has sub-tabs: Hotkeys/MIDI/Stream Deck/General (with Settings Save/Load). Log tab has real-time log viewer + maintenance tools.
- **Start with Windows**: Registry-based (`HKCU\...\Run`). Toggle in tray menu + Controls > General tab.
- **System tray**: Close -> tray, double-click/left-click -> restore, right-click -> Show/Quit/Start with Windows.
- **Panic Mute**: Remembers pre-mute monitor enable state, restores on unmute.
- **Auto-save**: Dirty-flag pattern with 1-second debounce. `onSettingsChanged` callbacks from AudioSettings/OutputPanel trigger `markSettingsDirty()`.
- **WebSocket server**: RFC 6455 with custom SHA-1. Dead client cleanup on broadcast. Port 8765. UDP discovery broadcast (port 8767) at startup for instant Stream Deck connection.
- **HTTP server**: GET-only REST API. CORS enabled. 3-second read timeout. Port 8766. Volume range validated (0.0-1.0). Endpoints include recording toggle and plugin parameter control.
- **Audio recording**: Lock-free recording via `AudioRecorder` + `AudioFormatWriter::ThreadedWriter`. WAV output. RT-safe `juce::SpinLock` protects writer teardown. Timer-based duration tracking. Auto-stop on device change.
- **Settings export/import**: `SettingsExporter` class. Full settings save/load as `.dpbackup` files via native file chooser. Located in Controls > General tab.
- **Plugin scanner search/sort**: Real-time text filter + column sorting (name/vendor/format) in PluginScanner dialog.
- **MIDI plugin parameter mapping**: MidiTab 3-step popup flow (select plugin → select parameter → MIDI Learn). Creates `Continuous` MidiBinding with `SetPluginParameter` action.
- **System tray tooltip**: Shows current state (preset, plugins, volumes) on hover. Atomic dirty-flag for cross-thread safety.
- **Stream Deck plugin**: SDKVersion 3, 6 SingletonAction subclasses (Bypass/Volume/Preset/Monitor/Panic/Recording), Property Inspector HTML (sdpi-components v4), event-driven reconnection (UDP discovery + user-action trigger, no polling), SVG icons with @2x. Pending message queue (cap 50). Packaged via official `streamdeck pack` CLI.
- **NotificationBar**: Non-intrusive status bar notifications. Temporarily replaces latency/CPU labels. Color-coded: red (errors), orange (warnings), purple (info). Auto-fades after 3-8 seconds depending on severity. Triggered by audio device errors, plugin load failures, recording failures.
- **LogPanel**: 4th tab in right panel. Real-time log viewer with timestamped monospaced entries. Export Log (save to .txt) and Clear Log buttons. Maintenance section: Clear Plugin Cache, Clear All Presets, Reset Settings (all with confirmation dialogs).
- **DirectPipeLogger**: Centralized logging system feeding LogPanel and NotificationBar. Captures logs from all subsystems (audio engine, plugins, WebSocket, HTTP, etc.).

## Coding Rules
- Audio callback: no heap alloc, no mutex. Pre-allocated 8-channel work buffer.
- Control -> audio: atomic flags or lock-free queue
- GUI and control share ActionDispatcher
- MainComponent::onAction() checks thread, uses callAsync if not on message thread
- WebSocket/HTTP on separate threads
- `juce::MessageManager::callAsync` for UI self-deletion safety (PluginChainEditor remove button etc.)
- `loadingSlot_` (std::atomic<bool>) guard prevents recursive auto-save during slot loading
- onChainChanged callback outside chainLock_ scope (deadlock prevention)
- MidiBuffer pre-allocated in prepareToPlay, cleared after processBlock
- VSTChain removes old I/O nodes before adding new ones in prepareToPlay
- ActionDispatcher/StateBroadcaster: copy-before-iterate for reentrant safety
- WebSocket broadcast on dedicated thread (non-blocking)
- Update check thread properly joined (no detached threads)
- AudioRingBuffer capacity must be power-of-2 (assertion enforced)

## Modules
- `core/` -> IPC library (RingBuffer, SharedMemory, Protocol, Constants)
- `host/` -> JUCE app
  - `Audio/` -> AudioEngine, VSTChain, OutputRouter, VirtualMicOutput, AudioRingBuffer, LatencyMonitor, AudioRecorder
  - `Control/` -> ActionDispatcher, ControlManager, ControlMapping, WebSocketServer, HttpApiServer, HotkeyHandler, MidiHandler, StateBroadcaster, DirectPipeLogger
  - `IPC/` -> SharedMemWriter
  - `UI/` -> AudioSettings, OutputPanel, ControlSettingsPanel, PluginChainEditor, PluginScanner, PresetManager, LevelMeter, LogPanel, NotificationBar, DirectPipeLookAndFeel, SettingsExporter
- `com.directpipe.directpipe.sdPlugin/` -> Stream Deck plugin (Node.js, @elgato/streamdeck SDK v3)
- `tests/` -> Google Test (core tests + host tests)
- `dist/` -> Packaged .streamDeckPlugin + marketplace assets

## Known Notes
- `ChildProcess::start()`: quote paths with spaces
- VST scan timeout: 300s (5min)
- `channelMode_` default: 2 (Stereo)
- Plugin deletion: auto-close editor windows + cleanup editorWindows_ vector
- ASIO SDK path: `thirdparty/asiosdk/common`
- Preset version 4 (deviceType, activeSlot, plugin state)
- SHA-1: custom implementation for WebSocket handshake only
- Stream Deck: SDKVersion 3 (SDK v2.0.1 npm), 6 actions (Bypass/Volume/Preset/Monitor/Panic/Recording), 3 PI HTMLs, SVG-based icons + @2x, packaged via `streamdeck pack` CLI
- Auto-save: dirty-flag + 1s debounce (not periodic timer), onSettingsChanged callbacks
- License: GPL v3 (JUCE GPL compatibility). JUCE_DISPLAY_SPLASH_SCREEN=0
- Credit label "Created by LiveTrack" at bottom-right of main UI. Shows "NEW vX.Y.Z" in orange when a newer GitHub release exists.
- **Update check**: Background thread fetches latest release from GitHub API on startup. Compares semver, updates credit label if newer.
- Process priority: HIGH_PRIORITY_CLASS at startup
- Portable mode: `portable.flag` next to exe -> config stored in `./config/`
