# Building DirectPipe

## Requirements

- Windows 10/11 (64-bit)
- Visual Studio 2022 (C++ Desktop Development workload)
- CMake 3.22+
- Git

## Quick Build

```bash
# Clone and configure
git clone https://github.com/LiveTrack-X/DirectLoopMic.git
cd DirectLoopMic
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release

# Run tests
cd build && ctest --config Release
```

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `DIRECTPIPE_BUILD_TESTS` | ON | Build unit tests |
| `DIRECTPIPE_BUILD_OBS_PLUGIN` | ON | Build OBS Studio plugin |
| `DIRECTPIPE_BUILD_HOST` | ON | Build JUCE host application |

```bash
# Build only the core library and tests (no JUCE/OBS)
cmake -B build -DDIRECTPIPE_BUILD_HOST=OFF -DDIRECTPIPE_BUILD_OBS_PLUGIN=OFF
```

## OBS Plugin

To build the OBS plugin, set `OBS_SOURCE_DIR` to your OBS Studio source directory:

```bash
cmake -B build -DOBS_SOURCE_DIR=/path/to/obs-studio
```

## Output

After building, find:
- `build/bin/Release/DirectPipe.exe` — Main application
- `build/lib/Release/obs-directpipe-source.dll` — OBS plugin
- `build/bin/Release/directpipe-tests.exe` — Test suite
