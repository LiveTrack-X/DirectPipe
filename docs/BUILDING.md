# Building DirectPipe / 빌드 가이드

## Requirements / 요구 사항

### Windows

- **Windows 10/11** (64-bit)
- **Visual Studio 2022** (C++ Desktop Development workload, C++17)
- **CMake 3.22+**
- **Git**

### macOS

- **macOS 10.15+** (Catalina 이상)
- **Xcode 14+** (Command Line Tools)
- **CMake 3.22+**
- **Git**

### Linux

- **GCC 9+** 또는 **Clang 10+**
- **CMake 3.22+**
- **Git**
- 시스템 패키지 / System packages:

```bash
# Ubuntu/Debian
sudo apt-get install libasound2-dev libjack-jackd2-dev \
  libfreetype-dev libx11-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libgl-dev libwebkit2gtk-4.1-dev libcurl4-openssl-dev

# Fedora
sudo dnf install alsa-lib-devel jack-audio-connection-kit-devel \
  freetype-devel libX11-devel libXrandr-devel libXinerama-devel \
  libXcursor-devel mesa-libGL-devel webkit2gtk4.1-devel libcurl-devel
```

### Optional / 선택

- **ASIO SDK** (Windows only) — For ASIO driver support. Place in `thirdparty/asiosdk/`. / ASIO 드라이버 지원용.
- **VST2 SDK** (all platforms) — Place VST2 interface headers in `thirdparty/VST2_SDK/pluginterfaces/vst2.x/` (`aeffect.h`, `aeffectx.h`). **Not included in this repository** — Steinberg prohibits redistribution of VST2 headers. You must obtain them separately if you have a valid VST2 license agreement. Without VST2 SDK, the build succeeds with VST3-only support. / Steinberg이 VST2 헤더 재배포를 금지하므로 저장소에 미포함. 유효한 VST2 라이선스 계약이 있는 경우 직접 배치. VST2 SDK 없이도 빌드 가능 (VST3만 지원).

### Auto-fetched Dependencies / 자동 다운로드 의존성

Downloaded automatically by CMake FetchContent: / CMake FetchContent로 자동 다운로드:

- **JUCE 7.0.12** — Audio framework / 오디오 프레임워크
- **Google Test 1.14.0** — Unit testing / 유닛 테스트

## Quick Build / 빠른 빌드

모든 플랫폼 공통 / Common to all platforms:

```bash
# Clone
git clone https://github.com/LiveTrack-X/DirectPipe.git
cd DirectPipe

# Configure and build / 설정 및 빌드
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Run tests / 테스트 실행
cd build && ctest --config Release
```

### Windows — Visual Studio

```powershell
# Generate Visual Studio solution / VS 솔루션 생성
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build / 빌드
cmake --build build --config Release
```

Or open `build/DirectPipe.sln` in Visual Studio and build from the IDE. / 또는 VS에서 직접 빌드.

### macOS — Xcode

```bash
cmake -B build -G Xcode
cmake --build build --config Release
```

Or open `build/DirectPipe.xcodeproj` in Xcode. / 또는 Xcode에서 직접 빌드.

### Linux

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j$(nproc)
```

## ASIO SDK Setup / ASIO SDK 설정 (Windows Only)

To enable ASIO driver support: / ASIO 드라이버를 사용하려면:

1. Download ASIO SDK from [Steinberg](https://www.steinberg.net/asiosdk) / Steinberg에서 다운로드
2. Extract to `thirdparty/asiosdk/` so that `thirdparty/asiosdk/common/asio.h` exists / 경로에 맞게 압축 해제
3. CMake will detect the SDK and enable `JUCE_ASIO=1` automatically / 자동 감지됨

Without the ASIO SDK, the build succeeds with WASAPI-only support. / ASIO SDK 없이도 빌드 가능 (WASAPI만 지원).

## Build Options / 빌드 옵션

| Option | Default | Description |
|--------|---------|-------------|
| `DIRECTPIPE_BUILD_TESTS` | ON | Build unit tests (Google Test) / 유닛 테스트 빌드 |
| `DIRECTPIPE_BUILD_HOST` | ON | Build JUCE host application / 호스트 앱 빌드 |
| `DIRECTPIPE_BUILD_RECEIVER` | ON | Build Receiver plugin (VST2/VST3/AU) / 리시버 플러그인 빌드 |

Note: `JUCE_DISPLAY_SPLASH_SCREEN=0` is set in CMakeLists.txt (GPL v3 license). / GPL v3 라이선스로 JUCE 스플래시 비활성화.

## Output Files / 빌드 결과물

### Windows
```
build/host/DirectPipe_artefacts/Release/DirectPipe.exe                          Host application
build/plugins/receiver/DirectPipeReceiver_artefacts/Release/VST/*.dll           Receiver VST2
build/plugins/receiver/DirectPipeReceiver_artefacts/Release/VST3/*.vst3         Receiver VST3
build/bin/Release/directpipe-tests.exe                                          Core tests
build/bin/Release/directpipe-host-tests.exe                                     Host tests
```

### macOS
```
build/host/DirectPipe_artefacts/Release/DirectPipe.app                          Host application
build/plugins/receiver/DirectPipeReceiver_artefacts/Release/VST/*.vst           Receiver VST2
build/plugins/receiver/DirectPipeReceiver_artefacts/Release/VST3/*.vst3         Receiver VST3
build/plugins/receiver/DirectPipeReceiver_artefacts/Release/AU/*.component      Receiver AU
```

### Linux
```
build/host/DirectPipe_artefacts/Release/DirectPipe                              Host application
build/plugins/receiver/DirectPipeReceiver_artefacts/Release/VST/*.so            Receiver VST2
build/plugins/receiver/DirectPipeReceiver_artefacts/Release/VST3/*.vst3         Receiver VST3
```

### Common / 공통
```
build/lib/Release/directpipe-core.*                                             Core IPC library
dist/com.directpipe.directpipe.streamDeckPlugin                                 Stream Deck plugin
```

## Stream Deck Plugin Build / Stream Deck 플러그인 빌드

```bash
cd com.directpipe.directpipe.sdPlugin
npm install                  # Install dependencies / 의존성 설치
npm run icons                # Generate PNG icons from SVG / SVG -> PNG 생성
npm run build                # Rollup bundle src/ -> bin/plugin.js
streamdeck validate .        # Validate structure / 구조 검증
streamdeck pack . --output ../dist/ --force  # Package (official CLI required) / 패키징 (공식 CLI 필수)
```

Requires `@elgato/cli` (`npm install -g @elgato/cli`). Custom ZIP packaging will NOT work for Maker Console — must use official CLI. / `@elgato/cli` 필요. 커스텀 ZIP은 Maker Console에서 거부됨 — 반드시 공식 CLI 사용.

## Pre-Release Dashboard / 프리릴리즈 대시보드

An interactive HTML test dashboard is available for manual and automated pre-release testing.

릴리즈 전 수동/자동 테스트를 위한 대화형 HTML 대시보드가 제공됩니다.

- **Location / 위치**: `tools/pre-release-dashboard.html`
- **Usage / 사용법**: Open in a browser while DirectPipe is running. Auto tests use the HTTP API (`localhost:8766`), manual tests require user verification.
  브라우저에서 열기 (DirectPipe 실행 중). 자동 테스트는 HTTP API 사용, 수동 테스트는 사용자 확인 필요.
- **Sections / 섹션**: API, Volume, Mute, Presets, Plugins, Devices, Hotkeys, MIDI, Stream Deck, IPC, Settings, UI, Regression tests
- **Features / 기능**: One-click auto test run, pass/fail tracking, export report, localStorage persistence
  원클릭 자동 테스트, 통과/실패 추적, 리포트 내보내기, localStorage 저장

## Test Suite / 테스트

Two test executables are built: `directpipe-tests` (core, no JUCE dependency) and `directpipe-host-tests` (requires JUCE).

두 개의 테스트 실행 파일: `directpipe-tests` (코어, JUCE 의존성 없음)와 `directpipe-host-tests` (JUCE 필요).

### directpipe-tests (Core)

| Test Group | Description |
|------------|-------------|
| RingBufferTest | SPSC ring buffer correctness, concurrency / 링 버퍼 정확성, 동시성 |
| SharedMemoryTest | Shared memory create/map, named events / 공유 메모리 생성/매핑 |
| LatencyTest | Write/read latency, throughput benchmark / 레이턴시, 처리량 벤치마크 |
| ActionDispatcherTest | Action dispatch, listener management, thread safety / 액션 디스패치, 스레드 안전 |
| IPCIntegrationTest | End-to-end IPC pipeline, data integrity / IPC 파이프라인 무결성 |

### directpipe-host-tests (Host)

| Test Group | Description |
|------------|-------------|
| WebSocketProtocolTest | JSON protocol parsing, state serialization / JSON 프로토콜 파싱, 상태 직렬화 |

```bash
# Run all tests / 전체 테스트 실행
cd build && ctest --config Release --output-on-failure

# Run specific test group / 특정 그룹만 실행
./bin/Release/directpipe-tests --gtest_filter="ActionDispatcherTest.*"
```
