# DirectPipe Architecture

## System Overview

```
USB Mic -> WASAPI Shared (~10ms) -> VST Chain (0ms added)
  -> SharedMem -> Virtual Loop Mic Driver -> Apps (Discord/Zoom/OBS)
  -> WASAPI Out -> Headphones (local monitor)

External Control:
  Hotkey / MIDI / WebSocket / HTTP -> ControlManager -> ActionDispatcher
    -> VSTChain (bypass), OutputRouter (volume), PresetManager
```

**Key design choice:** WASAPI **Shared** mode is used instead of Exclusive mode. This allows the original USB microphone to remain accessible to other applications simultaneously — no device locking.

## Components

### 1. Core Library (`core/`)

Shared static library used by both the host application and OBS plugin.

- **RingBuffer**: SPSC (Single Producer, Single Consumer) lock-free ring buffer using `std::atomic` with acquire/release semantics. Cache-line aligned (`alignas(64)`) to prevent false sharing. Power-of-2 capacity for efficient bitmask modulo.
- **SharedMemory**: Windows `CreateFileMapping`/`MapViewOfFile` wrapper with POSIX `shm_open`/`mmap` fallback for cross-platform development builds.
- **NamedEvent**: Windows Named Event for data-ready signaling between producer and consumer.
- **Protocol**: Shared header structure definition for IPC communication.
- **Constants**: Shared constants (buffer names, sizes, sample rates).

### 2. Host Application (`host/`)

JUCE 7-based desktop application. Main audio processing engine.

#### Audio Module (`host/Source/Audio/`)

- **AudioEngine**: WASAPI Shared mode input, manages the audio device callback. Pre-allocates work buffers to avoid heap allocation in the real-time audio thread. Supports mono mixing (0.5 * ch0 + 0.5 * ch1 average) and stereo passthrough.
- **VSTChain**: `AudioProcessorGraph`-based VST2/VST3 plugin chain. Uses `suspendProcessing()` during graph rebuild for thread safety. Pre-allocated `MidiBuffer` to avoid per-callback allocation.
- **OutputRouter**: Distributes processed audio to 3 simultaneous outputs (SharedMem for Virtual Loop Mic, WASAPI for monitor, level tracking for virtual mic). Each output has independent atomic volume and enable controls.
- **LatencyMonitor**: High-resolution timer-based latency measurement for the audio callback.

#### Control Module (`host/Source/Control/`)

All external inputs funnel through a unified `ActionDispatcher`.

- **ActionDispatcher**: Central action routing. Dispatches `ActionEvent` structs to registered listeners. Thread-safe with mutex-protected listener list. Provides convenience methods (`dispatchPluginBypass`, `dispatchPanicMute`, etc.).
- **ControlManager**: Aggregates all control sources (Hotkey, MIDI, WebSocket, HTTP).
- **HotkeyHandler**: Windows `RegisterHotKey` API for global keyboard shortcuts.
- **MidiHandler**: JUCE `MidiInput` for MIDI CC mapping with Learn mode.
- **WebSocketServer**: WebSocket server (port 8765) for Stream Deck and external clients. JSON protocol for actions and state push.
- **HttpApiServer**: Simple HTTP REST API (port 8766) for one-shot commands.
- **StateBroadcaster**: Pushes state changes to all connected clients (WebSocket, GUI, MIDI LED).
- **ControlMapping**: JSON-based persistence for hotkey/MIDI/server configuration.

#### IPC Module (`host/Source/IPC/`)

- **SharedMemWriter**: Producer-side wrapper. Writes processed audio to the shared memory ring buffer and signals the Named Event.

#### UI Module (`host/Source/UI/`)

- **DeviceSelector**: Audio input device selection dropdown.
- **AudioSettings**: Sample rate, buffer size, channel mode controls.
- **PluginChainEditor**: VST plugin add/remove/reorder, bypass toggle, editor popup.
- **LevelMeter**: Real-time input/output RMS level display.
- **OutputPanel**: Virtual Loop Mic status, monitor volume controls.
- **PresetManager**: JSON-based preset save/load.
- **ControlSettingsPanel**: Hotkey, MIDI, Stream Deck configuration UI.
- **DirectPipeLookAndFeel**: Custom dark theme.

### 3. Virtual Loop Mic Driver (`driver/`)

WDM (Windows Driver Model) kernel-mode audio capture driver based on Microsoft Sysvad sample.

- Exposes a virtual microphone device ("Virtual Loop Mic") to Windows
- Reads PCM audio from shared memory (`ZwOpenSection`/`ZwMapViewOfSection`)
- Any application can select it as a standard microphone input
- Supports 44.1/48 kHz, mono/stereo, 16-bit PCM and 32-bit float

### 4. OBS Plugin (`obs-plugin/`)

OBS Studio audio source plugin that reads from the same shared memory ring buffer.

- Registers as `directpipe_audio_source` in OBS
- Consumer-side reader thread with Named Event wait
- Auto-reconnect on host restart

### 5. Stream Deck Plugin (`streamdeck-plugin/`)

Optional Elgato Stream Deck plugin (Node.js).

- Connects to DirectPipe via WebSocket
- Button actions: Bypass Toggle, Volume Control, Preset Switch, Panic Mute
- Auto-reconnect with exponential backoff

## Data Flow (Real-time Audio Thread)

```
1. WASAPI Shared callback fires (default: 480 samples @48kHz = 10ms period)
2. Input PCM float32 copied to pre-allocated work buffer (no heap alloc)
3. Channel processing:
   - Mono mode: average two input channels (0.5 * L + 0.5 * R)
   - Stereo mode: pass through as-is
4. Apply input gain (atomic float, set from any thread)
5. Measure input RMS level
6. Process through VST chain (graph->processBlock, inline)
   - Each plugin bypass is atomic<bool>, toggled from control thread
   - Graph rebuild uses suspendProcessing() for safety
7. Route to outputs:
   a. SharedMem ring buffer write -> signal Named Event -> Virtual Loop Mic Driver
   b. Copy to WASAPI output channels -> headphone monitor
8. Measure output RMS level
9. Update latency monitor
```

## Control Event Flow (Non-RT Thread)

```
External Input (Hotkey/MIDI/WebSocket/HTTP)
  -> ControlManager
  -> ActionDispatcher::dispatch(ActionEvent)
  -> Registered Listeners
  -> AudioEngine sets atomic flags (gain, mute, bypass)
  -> Next audio callback reads updated atomic values (lock-free)
```

## IPC Protocol

### Shared Memory Layout

```
[DirectPipeHeader - 64-byte cache-line aligned]
  write_pos:      atomic<uint64_t>  (cache line 1)
  read_pos:       atomic<uint64_t>  (cache line 2)
  sample_rate:    uint32_t
  channels:       uint32_t
  buffer_frames:  uint32_t  (4096, power of 2)
  version:        uint32_t  (protocol version = 1)
  active:         atomic<bool>

[Ring Buffer Data]
  float32 PCM, interleaved
  4096 frames x 2 channels x 4 bytes = 32KB (~85ms buffer)
```

### Synchronization

- Named Event `Local\DirectPipeDataReady` signals new data
- Producer (host app): write -> `SetEvent()`
- Consumer (driver/OBS): `WaitForSingleObject()` -> read
- Timeout 500ms -> connection lost detection

### Thread Safety Rules

1. **Audio callback (RT thread)**: No heap allocation, no mutexes, no I/O
2. **Control -> Audio**: `std::atomic` flags only (relaxed memory order for simple values)
3. **Graph modification**: `suspendProcessing(true)` before, `false` after
4. **Chain modification**: `juce::CriticalSection` on non-RT thread, never in `processBlock`
5. **WebSocket/HTTP**: Separate threads, communicate via `ActionDispatcher` (mutex-protected listener list)
