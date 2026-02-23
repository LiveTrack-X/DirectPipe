# DirectPipe — Claude Code Project Guide v5.0

## Project Description
Real-time VST2/VST3 host for Windows. Processes microphone input through a plugin chain with monitor output. Focused on external control (hotkeys, MIDI, Stream Deck, HTTP API) and fast preset switching. Similar to Light Host but with remote control capabilities.

Windows용 실시간 VST2/VST3 호스트. 마이크 입력을 플러그인 체인으로 처리. 외부 제어(단축키, MIDI, Stream Deck, HTTP API)와 빠른 프리셋 전환에 초점.

## Core Principles
1. WASAPI Shared default + ASIO support (non-exclusive mic access)
2. VST2 + VST3 hosting with drag-and-drop chain editing
3. Quick Preset Slots A-E (chain-only, includes plugin internal state)
4. External control: Hotkey, MIDI, WebSocket, HTTP → unified ActionDispatcher
5. System tray resident (close = minimize to tray)
6. Out-of-process VST scanner (crash-safe)

## Tech Stack
- C++17, JUCE 7.0.12, CMake 3.22+
- WASAPI Shared Mode + ASIO (Steinberg ASIO SDK)
- VST2 SDK 2.4 + VST3
- WebSocket: JUCE StreamingSocket + RFC 6455 (handshake, framing, SHA-1)
- HTTP: JUCE StreamingSocket manual parsing

## Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Architecture
```
Mic → WASAPI Shared / ASIO → [Mono/Stereo] → VSTChain → Monitor Output

External Control:
Hotkey/MIDI/WebSocket/HTTP → ControlManager → ActionDispatcher
  → VSTChain (bypass), OutputRouter (volume), PresetManager
```

## Key Implementations
- **ASIO + WASAPI dual driver**: Runtime switching. ASIO: single device, dynamic SR/BS. WASAPI: separate I/O, fixed lists.
- **Quick Preset Slots (A-E)**: Chain-only. Plugin state via getStateInformation/base64. Async loading (replaceChainAsync). Same-chain fast path = instant switch.
- **Out-of-process VST scanner**: `--scan` child process. Auto-retry (5x), dead man's pedal.
- **Plugin chain editor**: Drag-and-drop, bypass toggle, native GUI edit. Auto-save on editor close.
- **Tabbed UI**: Audio/Output/Controls tabs in right column.
- **System tray**: Close → tray, double-click → restore, right-click → Show/Quit.
- **Panic Mute**: Remembers pre-mute enable states, restores on unmute.
- **WebSocket server**: RFC 6455 with custom SHA-1 implementation.
- **Stream Deck plugin**: 4 class-based actions, Property Inspector HTML, auto-reconnect.

## Coding Rules
- Audio callback: no heap alloc, no mutex
- Control → audio: atomic flags or lock-free queue
- GUI and control share ActionDispatcher
- WebSocket/HTTP on separate threads
- `juce::MessageManager::callAsync` for UI self-deletion safety
- `loadingSlot_` guard prevents recursive auto-save during slot loading
- onChainChanged callback outside chainLock_ scope (deadlock prevention)

## Modules
- `core/` → IPC library (RingBuffer, SharedMemory)
- `host/` → JUCE app
  - `Audio/` → AudioEngine, VSTChain, OutputRouter, LatencyMonitor
  - `Control/` → ActionDispatcher, WebSocketServer, HttpApiServer, HotkeyHandler, MidiHandler, StateBroadcaster
  - `IPC/` → SharedMemWriter
  - `UI/` → PluginChainEditor, PluginScanner, AudioSettings, OutputPanel, ControlSettingsPanel, LevelMeter, PresetManager
- `driver/` → Virtual Loop Mic kernel driver (experimental)
- `streamdeck-plugin/` → Stream Deck plugin (Node.js, SDK v2)

## Known Notes
- `ChildProcess::start()`: quote paths with spaces
- VST scan timeout: 300s (5min)
- `channelMode_` default: 2 (Stereo)
- Plugin deletion: auto-close editor windows + cleanup editorWindows_ vector
- ASIO SDK path: `thirdparty/asiosdk/common`
- Preset version 4 (deviceType, activeSlot, plugin state)
- SHA-1: custom implementation for WebSocket handshake only
- Stream Deck: 4 actions complete, 3 PI HTMLs, placeholder images
