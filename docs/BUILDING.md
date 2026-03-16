# Building DirectPipe / 빌드 가이드

> **플랫폼 지원 상태**: Windows (안정), macOS (베타), Linux (실험적). macOS/Linux 빌드는 실험적이며 실기기 테스트가 제한적입니다.
> **Platform support**: Windows (stable), macOS (beta), Linux (experimental). macOS/Linux builds are experimental with limited real-hardware testing.

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
  libxcursor-dev libgl-dev

# Fedora
sudo dnf install alsa-lib-devel jack-audio-connection-kit-devel \
  freetype-devel libX11-devel libXrandr-devel libXinerama-devel \
  libXcursor-devel mesa-libGL-devel
```

> **Note**: `libwebkit2gtk` and `libcurl` are NOT required — DirectPipe sets `JUCE_WEB_BROWSER=0` and `JUCE_USE_CURL=0`.
>
> **참고**: `libwebkit2gtk`와 `libcurl`은 필요하지 않습니다 — DirectPipe는 `JUCE_WEB_BROWSER=0`과 `JUCE_USE_CURL=0`을 설정합니다.

### Optional / 선택

- **ASIO SDK** (Windows only) — For ASIO driver support. Place in `thirdparty/asiosdk/`. / ASIO 드라이버 지원용.
- **VST2 SDK** (all platforms) — Place VST2 interface headers in `thirdparty/VST2_SDK/pluginterfaces/vst2.x/` (`aeffect.h`, `aeffectx.h`). **Not included in this repository** — Steinberg prohibits redistribution of VST2 headers. You must obtain them separately if you have a valid VST2 license agreement. Without VST2 SDK, the build succeeds with VST3-only support. / Steinberg이 VST2 헤더 재배포를 금지하므로 저장소에 미포함. 유효한 VST2 라이선스 계약이 있는 경우 직접 배치. VST2 SDK 없이도 빌드 가능 (VST3만 지원).

### RNNoise (Noise Suppression) / 노이즈 제거

- Included in `thirdparty/rnnoise/` (BSD-3-Clause, vendored source) / `thirdparty/rnnoise/`에 포함 (BSD-3-Clause, 벤더링)
- Built automatically by CMake as a static library / CMake에서 정적 라이브러리로 자동 빌드
- No external download or SDK setup required / 외부 다운로드 불필요
- Pure C — no platform-specific dependencies / 순수 C — 플랫폼 의존성 없음

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
- **Platform selector / 플랫폼 선택**: Tests are tagged per OS (Windows/macOS/Linux). Select your platform at the top to show only relevant tests. Platform stored in localStorage and included in exported reports.
  테스트는 OS별 태그 지정. 상단에서 플랫폼 선택하면 관련 테스트만 표시. 플랫폼은 localStorage에 저장되며 내보내기 리포트에 포함.
- **Features / 기능**: One-click auto test run, pass/fail tracking, export report, localStorage persistence, GTest JSON result loading
  원클릭 자동 테스트, 통과/실패 추적, 리포트 내보내기, localStorage 저장, GTest JSON 결과 로딩

## Test Suite / 테스트

Two test executables are built: `directpipe-tests` (core, no JUCE dependency) and `directpipe-host-tests` (requires JUCE). Total: **110+ tests** across 12 test groups.

두 개의 테스트 실행 파일: `directpipe-tests` (코어, JUCE 의존성 없음)와 `directpipe-host-tests` (JUCE 필요). 총 **110+ 테스트**, 12개 테스트 그룹.

### directpipe-tests (Core)

| Test Group | Tests | Description |
|------------|-------|-------------|
| RingBufferTest | ~10 | SPSC ring buffer correctness, concurrency / 링 버퍼 정확성, 동시성 |
| SharedMemoryTest | ~5 | Shared memory create/map, named events / 공유 메모리 생성/매핑 |
| LatencyTest | ~3 | Write/read latency, throughput benchmark / 레이턴시, 처리량 벤치마크 |
| IPCIntegrationTest | ~5 | End-to-end IPC pipeline, data integrity / IPC 파이프라인 무결성 |
| ReceiverSimulationTest | ~5 | Receiver VST processBlock simulation (de-interleave, underrun, clock drift, producer death) / Receiver VST processBlock 시뮬레이션 |
| CrossProcessIPC | ~3 | Cross-process shared memory + ring buffer validation via child process / 자식 프로세스를 통한 크로스 프로세스 IPC 검증 |

### directpipe-host-tests (Host)

| Test Group | Tests | Description |
|------------|-------|-------------|
| WebSocketProtocolTest | 30 | JSON protocol parsing, state serialization, error handling, edge cases / JSON 프로토콜 파싱, 상태 직렬화, 오류 처리, 엣지 케이스 |
| ActionDispatcherTest | 31 | Action dispatch, listener management, thread safety, ActionResult / 액션 디스패치, 리스너 관리, 스레드 안전, ActionResult |
| ActionResultTest | 12 | ActionResult data type: ok/fail factory methods, bool conversion, message propagation / ActionResult 데이터 타입 테스트 |
| ControlMappingTest | 16 | Hotkey/MIDI/server config serialization roundtrip, defaults, error handling / 핫키/MIDI/서버 설정 직렬화, 기본값, 오류 처리 |
| NotificationQueueTest | 10 | Lock-free SPSC notification queue: push/pop, FIFO, overflow, wrap-around, cross-thread / 락프리 SPSC 알림 큐: 푸시/팝, FIFO, 오버플로, 랩어라운드, 크로스스레드 |

Host test source files: `test_websocket_protocol.cpp`, `test_action_dispatcher.cpp`, `test_action_result.cpp`, `test_control_mapping.cpp`, `test_notification_queue.cpp`.

호스트 테스트 소스: `test_websocket_protocol.cpp`, `test_action_dispatcher.cpp`, `test_action_result.cpp`, `test_control_mapping.cpp`, `test_notification_queue.cpp`.

### GTest JSON Output / GTest JSON 출력

`tools/pre-release-test.sh` generates GTest JSON output files (`test-results-core.json`, `test-results-host.json`) that can be loaded into the pre-release dashboard for visual test result inspection.

`tools/pre-release-test.sh`는 GTest JSON 출력 파일을 생성하며, 프리릴리즈 대시보드에서 로드하여 시각적으로 테스트 결과를 확인할 수 있습니다.

```bash
# Run all tests / 전체 테스트 실행
cd build && ctest --config Release --output-on-failure

# Run specific test group / 특정 그룹만 실행
./bin/Release/directpipe-tests --gtest_filter="RingBufferTest.*"
./tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe --gtest_filter="ActionDispatcherTest.*"

# Generate JSON output for dashboard / 대시보드용 JSON 출력 생성
bash tools/pre-release-test.sh
```
