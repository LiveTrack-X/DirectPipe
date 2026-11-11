# Third-Party Licenses / 서드파티 라이선스

DirectPipe에서 사용하는 주요 서드파티 구성요소와 라이선스 정보입니다.
This document lists major third-party components used by DirectPipe and their licenses.

## 1) Core/Host Build Dependencies

### JUCE
- **License**: GPL-3.0-or-later (DirectPipe is GPL-3.0)
- **Source**: <https://github.com/juce-framework/JUCE>
- **Version**: `7.0.12` (fetched by CMake FetchContent)
- **Usage**: Host app UI/audio framework, plugin hosting, receiver plugin framework
- **DirectPipe patch provenance**: [Guarded endpoint identity and official bootstrap backport](tools/cmake/JuceEndpointIdentity.md); the SDK stays pinned to 7.0.12.

### GoogleTest
- **License**: BSD-3-Clause
- **Source**: <https://github.com/google/googletest>
- **Version**: `v1.14.0` (fetched by CMake FetchContent)
- **Usage**: Unit/integration test framework

### RNNoise
- **License**: BSD-3-Clause
- **Source**: <https://github.com/xiph/rnnoise>
- **Usage**: Built-in Noise Removal processor (`BuiltinNoiseRemoval`)
- **Files**: `thirdparty/rnnoise/`
- **License text**: `thirdparty/rnnoise/COPYING`

## 2) Optional SDKs (User-Provided)

### Steinberg VST2 SDK (optional)
- **License**: Steinberg VST2 license terms
- **Source**: User-provided in `thirdparty/VST2_SDK/`
- **Usage**: VST2 hosting + Receiver VST2 format build
- **Note**: Not redistributed in this repository due licensing restrictions

### Steinberg ASIO SDK (optional, Windows)
- **License**: Steinberg ASIO SDK license terms
- **Source**: User-provided in `thirdparty/asiosdk/`
- **Usage**: ASIO driver support in host app
- **Note**: Not redistributed in this repository

## 3) Stream Deck Plugin (Node.js)

Path: `com.directpipe.directpipe.sdPlugin/`

### Runtime dependencies
- `@elgato/streamdeck` (`2.0.1`) — Elgato Stream Deck SDK bindings
- `@elgato/utils` — complete MIT 0.4.1 package retained locally with a documented Zod 4.6.7 compatibility patch; [source and exact changes](com.directpipe.directpipe.sdPlugin/vendor/README.md)
- `ws` (`8.21.0`) — WebSocket client library
- Locked transitive runtime dependencies include `@elgato/schemas` and `zod`.

### Dev/build dependencies
- `esbuild` (`0.28.2`) — Node 20 CommonJS bundling
- `sharp` — icon generation (SVG -> PNG)
- Node syntax checks — the previous zero-rule lint contract, without an additional lint dependency

The build retains inline legal notices and packages full license texts for all
five runtime packages in `bin/THIRD_PARTY_NOTICES.txt`. The complete upstream
utils MIT license remains in its vendor directory. / 빌드 산출물에는 runtime 패키지
5개의 전체 라이선스를 포함하며, utils의 원본 MIT 라이선스도 vendor에 보존합니다.

각 npm 패키지의 상세 라이선스는 해당 패키지 레지스트리/저장소를 따릅니다.
Each npm package follows its own upstream license in its registry/repository.
