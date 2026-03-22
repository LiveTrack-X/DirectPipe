# Third-Party Licenses / 서드파티 라이선스

DirectPipe에서 사용하는 주요 서드파티 구성요소와 라이선스 정보입니다.
This document lists major third-party components used by DirectPipe and their licenses.

## 1) Core/Host Build Dependencies

### JUCE
- **License**: GPL-3.0-or-later (DirectPipe is GPL-3.0)
- **Source**: <https://github.com/juce-framework/JUCE>
- **Version**: `7.0.12` (fetched by CMake FetchContent)
- **Usage**: Host app UI/audio framework, plugin hosting, receiver plugin framework

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
- `@elgato/streamdeck` (`^2.0.1`) — Elgato Stream Deck SDK bindings
- `ws` (`^8.16.0`) — WebSocket client library

### Dev/build dependencies
- `rollup`, `@rollup/plugin-commonjs`, `@rollup/plugin-node-resolve` — bundling
- `sharp` — icon generation (SVG -> PNG)
- `eslint` — linting

각 npm 패키지의 상세 라이선스는 해당 패키지 레지스트리/저장소를 따릅니다.
Each npm package follows its own upstream license in its registry/repository.
