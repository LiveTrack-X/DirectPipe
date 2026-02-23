# Building DirectPipe

## Requirements

### Minimum

- **Windows 10/11** (64-bit) — primary target
- **Visual Studio 2022** (C++ Desktop Development workload)
- **CMake 3.22+**
- **Git**

### For full build

- **ASIO SDK** — for ASIO driver support (place in `thirdparty/asiosdk/`)
- **Windows Driver Kit (WDK)** — for Virtual Loop Mic driver

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

## ASIO SDK Setup

To enable ASIO driver support:

1. Download ASIO SDK from [Steinberg](https://www.steinberg.net/asiosdk)
2. Extract to `thirdparty/asiosdk/` so that `thirdparty/asiosdk/common/asio.h` exists
3. CMake will automatically detect the SDK and enable `JUCE_ASIO=1`

Without the ASIO SDK, the build will succeed but only WASAPI drivers will be available.

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

## Virtual Loop Mic Driver

The kernel driver is built separately with the WDK:

```cmd
cd driver
build.bat
```

See [driver/README.md](../driver/README.md) for signing, installation, and debugging.

## Output Files

After a successful build:

```
build/host/DirectPipe_artefacts/Release/DirectPipe.exe   Host application
build/bin/Release/directpipe-tests.exe                   Core test suite (56 tests)
build/lib/Release/directpipe-core.lib                    Core IPC static library
driver/out/                                               Kernel driver (separate build)
```

## Test Suite

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
