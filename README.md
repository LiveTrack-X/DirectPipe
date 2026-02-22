# DirectPipe

USB 마이크용 VST 호스트 + 초저지연 루프백 시스템.

하드웨어 오디오 인터페이스 없이 VST 이펙트를 적용하고 OBS/Discord에 초저지연(3~12ms)으로 전달하는 올인원 앱.

## Architecture

```
USB Mic → WASAPI Exclusive (~2.7ms) → VST Chain (0ms added)
  ├──→ [OBS Plugin] Shared Memory direct (3~6ms total)
  └──→ [Virtual Mic] Driver output (8~12ms, Discord/Zoom)
```

## Features

- WASAPI Exclusive mode for ultra-low input latency
- VST2/VST3 plugin chain with real-time processing
- OBS Studio integration via shared memory IPC (lock-free)
- Virtual microphone output for Discord, Zoom, etc.
- Real-time level meters and latency monitoring
- Preset management (save/load VST chain settings)
- Dark themed UI

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cd build && ctest --config Release
```

## Requirements

- Windows 10/11 (64-bit)
- Visual Studio 2022
- CMake 3.22+
- OBS Studio 28+ (for plugin)
- VB-Cable (optional, for virtual mic output)

## Project Structure

```
core/        — Shared IPC library (ring buffer, shared memory)
host/        — JUCE host app (VST host, audio engine, GUI)
obs-plugin/  — OBS Studio audio source plugin
tests/       — Unit tests and benchmarks
installer/   — Inno Setup installer script
docs/        — Architecture, build guide, user guide
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Build Guide](docs/BUILDING.md)
- [User Guide](docs/USER_GUIDE.md)

## License

MIT License — see [LICENSE](installer/assets/license.txt)
