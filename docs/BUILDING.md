# Building DirectPipe

## Requirements

### Minimum

- **Windows 10/11** (64-bit) — primary target
- **Visual Studio 2022** (C++ Desktop Development workload)
- **CMake 3.22+**
- **Git**

### For full build

- **Windows Driver Kit (WDK)** — for Virtual Loop Mic driver
- **OBS Studio SDK** — for OBS plugin (optional)

### Auto-fetched dependencies

The following are downloaded automatically by CMake FetchContent:

- **JUCE 7.0.12** — audio framework
- **Google Test 1.14.0** — unit testing
- **VST2 SDK** — included in `thirdparty/VST2_SDK/` (interface headers only)

## Quick Build

```bash
# Clone
git clone https://github.com/LiveTrack-X/DirectLoopMic.git
cd DirectLoopMic

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Run tests (56 tests)
cd build && ctest --config Release
```

## Windows Build (Visual Studio)

```powershell
# Generate Visual Studio solution
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Release

# Test
cd build && ctest --config Release
```

Or open `build/DirectPipe.sln` in Visual Studio and build from the IDE.

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `DIRECTPIPE_BUILD_TESTS` | ON | Build unit tests (Google Test) |
| `DIRECTPIPE_BUILD_OBS_PLUGIN` | ON | Build OBS Studio source plugin |
| `DIRECTPIPE_BUILD_HOST` | ON | Build JUCE host application |

```bash
# Build only the core library and tests (no JUCE/OBS dependencies)
cmake -B build -DDIRECTPIPE_BUILD_HOST=OFF -DDIRECTPIPE_BUILD_OBS_PLUGIN=OFF
cmake --build build --config Release
```

## OBS Plugin

To build the OBS plugin, set `OBS_SOURCE_DIR`:

```bash
cmake -B build -DOBS_SOURCE_DIR=/path/to/obs-studio
```

## Virtual Loop Mic Driver

The kernel driver is built separately with the WDK:

```cmd
cd driver
msbuild virtualloop.vcxproj /p:Configuration=Release /p:Platform=x64
```

Or open `driver/virtualloop.vcxproj` in Visual Studio with WDK installed.

See [driver/README.md](../driver/README.md) for signing, installation, and debugging.

## Output Files

After a successful build:

```
build/bin/Release/DirectPipe.exe          Host application (JUCE GUI)
build/bin/Release/directpipe-tests.exe    Test suite (56 tests)
build/lib/Release/directpipe-core.lib     Core IPC static library
build/lib/Release/obs-directpipe-source.dll  OBS plugin (if built)
driver/Release/virtualloop.sys            Kernel driver (separate build)
```

## Test Suite

The test suite covers:

| Test Group | Count | Description |
|------------|-------|-------------|
| RingBufferTest | 11 | SPSC ring buffer correctness, concurrency |
| SharedMemoryTest | 7 | Shared memory create/map, named events |
| LatencyTest | 3 | Write/read latency, throughput benchmark |
| ActionDispatcherTest | 23 | Action dispatch, listener management, thread safety |
| IPCIntegrationTest | 12 | End-to-end IPC pipeline, data integrity |
| **Total** | **56** | |

```bash
# Run all tests
cd build && ctest --config Release --output-on-failure

# Run specific test group
./bin/directpipe-tests --gtest_filter="ActionDispatcherTest.*"
```

## Cross-Platform Development

While DirectPipe is designed for Windows, the core library and tests can be built on Linux/macOS for development:

```bash
# Linux/macOS (core + tests only)
cmake -B build -DDIRECTPIPE_BUILD_HOST=OFF -DDIRECTPIPE_BUILD_OBS_PLUGIN=OFF
cmake --build build
cd build && ctest
```

Windows-specific features (WASAPI, kernel driver, global hotkeys) are stubbed with platform checks.
