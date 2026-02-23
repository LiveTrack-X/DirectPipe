# DirectPipe Architecture

## System Overview

```
USB Mic -> WASAPI Shared / ASIO -> VST Chain (0ms added)
  -> SharedMem -> Virtual Loop Mic Driver -> Apps (Discord/Zoom/OBS)
  -> WASAPI / ASIO Out -> Headphones (local monitor)

External Control:
  Hotkey / MIDI / WebSocket / HTTP -> ControlManager -> ActionDispatcher
    -> VSTChain (bypass), OutputRouter (volume), PresetManager
```

**Key design choices:**
- **WASAPI Shared** mode allows the original USB microphone to remain accessible to other applications simultaneously — no device locking.
- **ASIO** mode provides lower latency for users with ASIO-compatible interfaces.
- Driver type switching is seamless — AudioSettings UI adapts dynamically.

## Components

### 1. Core Library (`core/`)

Shared static library used by both the host application and OBS plugin.

- **RingBuffer**: SPSC (Single Producer, Single Consumer) lock-free ring buffer using `std::atomic` with acquire/release semantics. Cache-line aligned (`alignas(64)`) to prevent false sharing. Power-of-2 capacity for efficient bitmask modulo.
- **SharedMemory**: Windows `CreateFileMapping`/`MapViewOfFile` wrapper with POSIX `shm_open`/`mmap` fallback for cross-platform development builds.
- **NamedEvent**: Windows Named Event for data-ready signaling between producer and consumer.
- **Protocol**: Shared header structure definition for IPC communication.
- **Constants**: Shared constants (buffer names, sizes, sample rates).

### 2. Host Application (`host/`)

JUCE 7.0.12-based desktop application. Main audio processing engine.

#### Audio Module (`host/Source/Audio/`)

- **AudioEngine**: Dual driver support (WASAPI Shared + ASIO). Manages the audio device callback. Pre-allocates work buffers with dynamic channel count. Supports mono mixing (0.5 * ch0 + 0.5 * ch1 average) and stereo passthrough. Methods for device type switching, sample rate/buffer size queries (dynamic for ASIO, fixed for WASAPI).
- **VSTChain**: `AudioProcessorGraph`-based VST2/VST3 plugin chain. Uses `suspendProcessing()` during graph rebuild. Pre-allocated `MidiBuffer`. Async chain replacement (`replaceChainAsync`) loads plugins on background thread to prevent UI freezing. Editor windows tracked per-plugin with onClosed callback for auto-save.
- **OutputRouter**: Distributes processed audio to outputs (SharedMem for Virtual Loop Mic, WASAPI/ASIO for monitor). Each output has independent atomic volume and enable controls.
- **VirtualMicOutput**: Manages virtual microphone output device separately from monitor.
- **AudioRingBuffer**: Lock-free ring buffer for audio thread communication.
- **LatencyMonitor**: High-resolution timer-based latency measurement.

#### Control Module (`host/Source/Control/`)

All external inputs funnel through a unified `ActionDispatcher`.

- **ActionDispatcher**: Central action routing. Actions include: `PluginBypass`, `MasterBypass`, `SetVolume`, `ToggleMute`, `LoadPreset`, `PanicMute`, `InputGainAdjust`, `NextPreset`, `PreviousPreset`, `InputMuteToggle`, `SwitchPresetSlot`.
- **ControlManager**: Aggregates all control sources (Hotkey, MIDI, WebSocket, HTTP).
- **HotkeyHandler**: Windows `RegisterHotKey` API for global keyboard shortcuts.
- **MidiHandler**: JUCE `MidiInput` for MIDI CC mapping with Learn mode.
- **WebSocketServer**: WebSocket server (port 8765, fallback 8766-8770) for Stream Deck and external clients.
- **HttpApiServer**: HTTP REST API (port 8766, fallback 8767-8771) for one-shot commands.
- **StateBroadcaster**: Pushes state changes to all connected clients.
- **ControlMapping**: JSON-based persistence for hotkey/MIDI/server configuration. Portable mode support.

#### UI Module (`host/Source/UI/`)

- **AudioSettings**: Unified audio configuration panel. Driver type selector (WASAPI/ASIO), device selection (single for ASIO, separate input/output for WASAPI), sample rate, buffer size (dynamic lists for ASIO), channel mode, latency display, ASIO Control Panel button.
- **PluginChainEditor**: VST plugin chain management with drag-and-drop reordering, Bypass toggle, Edit button, Remove button. `onChainModified` callback for auto-save.
- **PluginScanner**: Out-of-process VST scanner dialog with automatic retry and dead man's pedal.
- **OutputPanel**: Monitor output controls (device selector, volume, enable toggle). Virtual Cable section removed.
- **PresetManager**: Full preset save/load (JSON) + Quick Preset Slots A-E (chain-only). Plugin internal state saved via `getStateInformation()` as base64. Async slot loading (`loadSlotAsync`) for non-blocking UI. Fast path (same chain) updates bypass/state instantly; slow path loads asynchronously.
- **ControlSettingsPanel**: Hotkey, MIDI, Stream Deck configuration UI.
- **LevelMeter**: Real-time input/output RMS level display.
- **DirectPipeLookAndFeel**: Custom dark theme. Toggle button colors respect active state.

#### Main Application (`host/Source/MainComponent.cpp`)

- Two-column layout: left (input + VST chain + slot buttons), right (tabbed panel: Audio/Output/Controls)
- Quick Preset Slot buttons A-E with visual active state
- Auto-save on chain change, editor close, slot switch, and periodic timer
- `loadingSlot_` guard prevents recursive saves during slot loading
- Panic mute remembers and restores pre-mute enable states

### 3. Virtual Loop Mic Driver (`driver/`)

WDM kernel-mode audio capture driver based on Microsoft Sysvad sample.

- Exposes a virtual microphone device ("Virtual Loop Mic") to Windows
- Reads PCM audio from shared memory (`ZwOpenSection`/`ZwMapViewOfSection`)
- Supports 44.1/48 kHz, mono/stereo, 16/24-bit PCM and 32-bit float

### 4. Stream Deck Plugin (`streamdeck-plugin/`)

Optional Elgato Stream Deck plugin (Node.js, SDK v2).

- Connects to DirectPipe via WebSocket (`ws://localhost:8765`)
- Button actions: Bypass Toggle (short/long press), Volume Control (dial + mute), Preset Switch, Panic Mute
- Auto-reconnect with exponential backoff (2s → 30s)

## Data Flow (Real-time Audio Thread)

```
1. WASAPI Shared / ASIO callback fires
2. Input PCM float32 copied to pre-allocated work buffer (no heap alloc)
3. Channel processing:
   - Mono mode: average two input channels
   - Stereo mode: pass through as-is
4. Apply input gain (atomic float)
5. Measure input RMS level
6. Process through VST chain (graph->processBlock, inline)
7. Route to outputs:
   a. SharedMem ring buffer write -> signal Named Event -> Virtual Loop Mic Driver
   b. Copy to output channels -> headphone monitor (volume applied)
8. Measure output RMS level
9. Update latency monitor
```

## Preset System

### Full Presets (Save/Load Preset menu)
- Audio settings (device type, devices, sample rate, buffer size, input gain)
- VST chain (plugins, order, bypass, internal state)
- Output settings (volumes, enables)
- Active slot index

### Quick Preset Slots (A-E)
- Chain-only: plugins, order, bypass state, **plugin internal parameters**
- Audio/output settings are NOT affected by slot switching
- Same-chain fast path: bypass + state update only (instant)
- Different-chain slow path: async background loading (non-blocking)
- Auto-save: chain changes, editor close, slot switch all trigger save

### Plugin State Serialization
- `PluginDescription` saved as XML via `createXml()` (handles WaveShell sub-plugins)
- Plugin internal state saved via `getStateInformation()` → base64 encoding
- Restored via `setStateInformation()` after plugin load

## Thread Safety Rules

1. **Audio callback (RT thread)**: No heap allocation, no mutexes, no I/O
2. **Control -> Audio**: `std::atomic` flags only
3. **Graph modification**: `suspendProcessing(true)` before, `false` after
4. **Chain modification**: `juce::CriticalSection` on non-RT thread, never in `processBlock`
5. **Async chain loading**: Plugins loaded on `std::thread`, wired into graph on message thread
6. **onChainChanged callback**: Called OUTSIDE `chainLock_` scope to prevent deadlock
7. **WebSocket/HTTP**: Separate threads, communicate via `ActionDispatcher`
8. **UI component self-deletion**: Use `juce::MessageManager::callAsync`
9. **Slot auto-save guard**: `loadingSlot_` flag prevents recursive saves during load operations
