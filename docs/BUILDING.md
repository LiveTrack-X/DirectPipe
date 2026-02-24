# Building DirectPipe / 빌드 가이드

## Requirements / 요구 사항

### Minimum / 최소

- **Windows 10/11** (64-bit)
- **Visual Studio 2022** (C++ Desktop Development workload)
- **CMake 3.22+**
- **Git**

### Optional / 선택

- **ASIO SDK** — For ASIO driver support. Place in `thirdparty/asiosdk/`. / ASIO 드라이버 지원용.
- **VST2 SDK** — Place VST2 interface headers in `thirdparty/VST2_SDK/pluginterfaces/vst2.x/` (`aeffect.h`, `aeffectx.h`). Not included in the repository due to Steinberg licensing. / Steinberg 라이선스로 인해 저장소에 미포함. 직접 배치 필요.

### Auto-fetched Dependencies / 자동 다운로드 의존성

Downloaded automatically by CMake FetchContent: / CMake FetchContent로 자동 다운로드:

- **JUCE 7.0.12** — Audio framework / 오디오 프레임워크
- **Google Test 1.14.0** — Unit testing / 유닛 테스트

## Quick Build / 빠른 빌드

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

## ASIO SDK Setup / ASIO SDK 설정

To enable ASIO driver support: / ASIO 드라이버를 사용하려면:

1. Download ASIO SDK from [Steinberg](https://www.steinberg.net/asiosdk) / Steinberg에서 다운로드
2. Extract to `thirdparty/asiosdk/` so that `thirdparty/asiosdk/common/asio.h` exists / 경로에 맞게 압축 해제
3. CMake will detect the SDK and enable `JUCE_ASIO=1` automatically / 자동 감지됨

Without the ASIO SDK, the build succeeds with WASAPI-only support. / ASIO SDK 없이도 빌드 가능 (WASAPI만 지원).

## Visual Studio Build / Visual Studio 빌드

```powershell
# Generate Visual Studio solution / VS 솔루션 생성
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build / 빌드
cmake --build build --config Release
```

Or open `build/DirectPipe.sln` in Visual Studio and build from the IDE. / 또는 VS에서 직접 빌드.

## Build Options / 빌드 옵션

| Option | Default | Description |
|--------|---------|-------------|
| `DIRECTPIPE_BUILD_TESTS` | ON | Build unit tests (Google Test) / 유닛 테스트 빌드 |
| `DIRECTPIPE_BUILD_HOST` | ON | Build JUCE host application / 호스트 앱 빌드 |

Note: `JUCE_DISPLAY_SPLASH_SCREEN=0` is set in CMakeLists.txt (GPL v3 license). / GPL v3 라이선스로 JUCE 스플래시 비활성화.

## Output Files / 빌드 결과물

```
build/host/DirectPipe_artefacts/Release/DirectPipe.exe   Host application / 호스트 앱
build/bin/Release/directpipe-tests.exe                   Core test suite / 코어 테스트
build/bin/Release/directpipe-host-tests.exe              Host test suite / 호스트 테스트
build/lib/Release/directpipe-core.lib                    Core IPC library / 코어 라이브러리
dist/com.directpipe.directpipe.streamDeckPlugin          Stream Deck plugin package
```

## Stream Deck Plugin Build / Stream Deck 플러그인 빌드

```bash
cd streamdeck-plugin
npm install                  # Install dependencies / 의존성 설치
npm run icons                # Generate PNG icons from SVG / SVG -> PNG 생성
streamdeck validate .        # Validate structure / 구조 검증
streamdeck pack . --output ../dist/ --force  # Package / 패키징
```

Requires `@elgato/cli` (`npm install -g @elgato/cli`). / `@elgato/cli` 필요.

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
