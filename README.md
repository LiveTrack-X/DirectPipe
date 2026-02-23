# DirectPipe

USB 마이크용 VST2/VST3 호스트 + 초저지연 루프백 시스템.

하드웨어 오디오 인터페이스 없이 VST 이펙트를 적용하고, "Virtual Loop Mic" 가상 장치를 통해 OBS/Discord/Zoom 등 모든 앱에서 사용할 수 있는 올인원 소프트웨어. Light Host 수준의 VST 호환성 + 기존 대비 50~60% 레이턴시 감소.

## Architecture

```
USB Mic -> WASAPI Shared (~10ms) -> VST Chain (0ms added)
  -> SharedMem -> Virtual Loop Mic Driver -> Discord/Zoom/OBS (~13-23ms total)
  -> WASAPI Out -> Headphones (local monitor)

External Control:
  Hotkey / MIDI / WebSocket / HTTP -> ActionDispatcher -> AudioEngine
```

## Features

- **WASAPI Shared mode** for non-exclusive microphone access (other apps can use mic simultaneously)
- **VST2/VST3 plugin chain** with real-time inline processing
- **Out-of-process VST scanner** — scans plugins in a separate process; if a bad plugin crashes, only the scanner process dies (main app survives). Automatic retry with dead man's pedal skips bad plugins.
- **Drag & drop plugin reordering** — drag plugins up/down to change processing order
- **Plugin editor** — open/close native VST plugin GUIs with proper window management
- **Virtual Loop Mic** kernel driver — appears as a standard microphone in Windows
- **Shared memory IPC** — lock-free SPSC ring buffer for ultra-low latency audio transfer
- **System tray** — close button minimizes to tray; right-click tray icon for Show/Quit
- **OBS Studio integration** via shared memory (no additional plugin required with driver)
- **External control** — keyboard shortcuts, MIDI CC, Stream Deck (WebSocket), HTTP REST API
- **Mono/Stereo** channel mode selection (default: Stereo)
- **Sample rate / buffer size** user-configurable (48kHz/480 samples default)
- **Real-time level meters** and latency monitoring
- **Preset management** (save/load VST chain + settings)
- **Dark themed UI** (JUCE custom LookAndFeel) with custom app icon

## Latency Comparison

| Buffer (samples @48kHz) | Light Host + VB-Cable | DirectPipe | Reduction |
|--------------------------|----------------------|------------|-----------|
| 480 (default)            | ~50ms                | ~23ms      | **54%**   |
| 256                      | ~35ms                | ~13ms      | **63%**   |
| 128                      | ~28ms                | ~8ms       | **71%**   |
| 64                       | ~22ms                | ~5ms       | **77%**   |

## Build

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release

# Test (56 tests)
cd build && ctest --config Release
```

### Windows (full build with JUCE GUI)

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cd build && ctest --config Release
```

## Requirements

- **Windows 10/11** (64-bit) — primary target platform
- **Visual Studio 2022** (C++ Desktop Development workload)
- **CMake 3.22+**
- **JUCE 7.0.12** (fetched automatically via CMake FetchContent)
- **Windows Driver Kit (WDK)** — for Virtual Loop Mic driver build
- **OBS Studio 28+** — optional, for OBS plugin

## Project Structure

```
core/                  IPC library (SPSC ring buffer, shared memory, named events)
host/                  JUCE host application
  Source/
    Audio/             AudioEngine, VSTChain, OutputRouter, LatencyMonitor
    Control/           ActionDispatcher, WebSocket, HTTP, Hotkey, MIDI handlers
    IPC/               SharedMemWriter (producer side)
    UI/                PluginChainEditor, PluginScanner, AudioSettings,
                       LevelMeter, OutputPanel, ControlSettingsPanel,
                       DirectPipeLookAndFeel
  Resources/           App icon (icon.png)
driver/                Virtual Loop Mic WDM kernel driver
obs-plugin/            OBS Studio audio source plugin (shared memory reader)
streamdeck-plugin/     Elgato Stream Deck plugin (WebSocket client)
tests/                 Unit tests (Google Test, 56 tests)
installer/             Inno Setup installer script
thirdparty/            VST2 SDK interface headers
docs/                  Architecture, build guide, API reference, user guide
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md) — system design, data flow, IPC protocol
- [Build Guide](docs/BUILDING.md) — build instructions and options
- [User Guide](docs/USER_GUIDE.md) — setup and usage
- [Control API](docs/CONTROL_API.md) — WebSocket and HTTP API reference
- [Stream Deck Guide](docs/STREAMDECK_GUIDE.md) — Elgato Stream Deck integration
- [Driver README](driver/README.md) — Virtual Loop Mic kernel driver

## Development Status

| Phase | Description | Status |
|-------|-------------|--------|
| Phase 0 | Environment setup, CMake scaffolding | Done |
| Phase 1 | Core IPC library (ring buffer, shared memory) | Done |
| Phase 2 | Audio engine + VST2/VST3 hosting | Done |
| Phase 3 | Virtual Loop Mic kernel driver | Done (code, needs Windows build) |
| Phase 4 | External control (Hotkey, MIDI, WebSocket, HTTP) | Done |
| Phase 5 | GUI (JUCE) | Done |
| Phase 6 | Stabilization, installer | In progress |

## License

MIT License — see [LICENSE](installer/assets/license.txt)
