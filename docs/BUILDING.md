# Building DirectPipe / 빌드 가이드

> **Released version / 릴리즈 버전: 4.2.3**

> **플랫폼 지원 상태**: Windows 10/11 x64는 안정 릴리즈 대상입니다. v4.2.3 로컬 Windows Release CTest 492개를 실행해 490개 통과, 환경 의존 2개 skip, 실패 0개를 확인했고 정확 태그 CI가 공개 전에 전체 플랫폼 빌드·등록 테스트·패키지 검증을 수행합니다. 실기기·제3자 VST crash-containment는 수행하지 않았습니다. macOS 10.15+ universal은 베타, Linux x86_64는 실험적이며 이번 업데이트에서 하드웨어 검증하지 않았습니다.
> **Platform support**: Windows 10/11 x64 is the stable release target. The local v4.2.3 Windows Release run completed all 492 CTest registrations (490 passed, 2 environment-dependent skips, 0 failed), and exact-tag CI runs cross-platform builds, registered tests, and package validation before publication. Real-device and third-party VST crash-containment checks were not run. macOS 10.15+ universal remains beta and Linux x86_64 experimental without hardware verification for this update.

## Support Status / 지원 상태

| Platform | Release status | CI target | Audio backend | Notes |
|---|---|---|---|---|
| Windows 10/11 x64 | Stable / 안정 | `windows-latest` | WASAPI Shared/Low Latency/Exclusive, ASIO when SDK is available | Local v4.2.3 Release CTest: 490 passed, 2 environment-dependent skips, 0 failed; exact-tag CI required; no real-device check. |
| macOS 10.15+ universal | Beta / 베타 | `macos-14` | CoreAudio | CI-generated DMG; ad-hoc signed, not treated as fully field-validated. |
| Linux x86_64 | Experimental / 실험적 | `ubuntu-24.04` | ALSA, JACK | CI-generated tarball; desktop/audio-device behavior can vary by distro. |
| Stream Deck plugin | Separate cross-platform package / 별도 크로스 플랫폼 패키지 | `ubuntu-latest` | WebSocket/UDP control client | Manifest targets Windows 10+, macOS 10.15+, Stream Deck 6.9+. |

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

Without the ASIO SDK, the build still succeeds; ASIO is disabled while the platform-native audio drivers remain available. / ASIO SDK 없이도 빌드 가능하며, ASIO만 비활성화되고 플랫폼 기본 오디오 드라이버는 유지됩니다.

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
build/tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe   Host tests
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
build/lib/Release/directpipe-core.*                                             Core IPC library (multi-config generators)
build/lib/directpipe-core.*                                                     Core IPC library (single-config generators)
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

Three test executables are built: `directpipe-tests` (core, no JUCE dependency), `directpipe-host-tests` (JUCE host coverage), and `directpipe-endpoint-watcher-tests` (focused endpoint notification coverage). The v4.2.3 inventory contains **492 CTest registrations** (59 core + 431 host + 2 focused endpoint tests). The binaries contain 45 suites in total (6 core + 38 host + 1 endpoint). The local Windows Release run completed with 490 passed, 2 environment-dependent skips, and 0 failed; publication remains gated by the exact-tag CI run.

세 개의 테스트 실행 파일을 빌드합니다: `directpipe-tests`(코어, JUCE 의존성 없음), `directpipe-host-tests`(JUCE 호스트 범위), `directpipe-endpoint-watcher-tests`(endpoint 알림 집중 검증). v4.2.3 인벤토리는 **CTest 등록 492개**(코어 59 + 호스트 431 + endpoint 집중 테스트 2), 전체 45개 suite(코어 6 + 호스트 38 + endpoint 1)입니다. 로컬 Windows Release 실행은 490개 통과, 환경 의존 2개 skip, 실패 0개였으며 공개는 정확 태그 CI 통과를 조건으로 합니다.

### directpipe-tests (Core)

| Test Group | Tests | Description |
|------------|-------|-------------|
| RingBufferTest | 23 | SPSC ring buffer correctness, atomic consumer claim, restart generation, bounds checks, and concurrency / 링 버퍼 정확성, atomic consumer 획득, 재시작 generation, 경계 검사, 동시성 |
| SharedMemoryTest | 8 | Shared memory create/map, named events, full IPC pipeline / 공유 메모리 생성/매핑, named event, 전체 IPC 파이프라인 |
| LatencyTest | 3 | Write/read latency, throughput benchmark / 레이턴시, 처리량 벤치마크 |
| IPCIntegrationTest | 12 | End-to-end IPC pipeline, data integrity / IPC 파이프라인 무결성 |
| ReceiverSimulationTest | 10 | Receiver VST processBlock simulation (de-interleave, underrun, clock drift, producer death) / Receiver VST processBlock 시뮬레이션 |
| CrossProcessIPC | 2 | Cross-process shared memory + ring buffer validation via child process / 자식 프로세스를 통한 크로스 프로세스 IPC 검증 |

### directpipe-host-tests (Host)

| Test Group | Tests | Description |
|------------|-------|-------------|
| WebSocket server/protocol | 39 | Shutdown/shared lifetime, handshake-ready ordering, idle sweep, listen and strict action-parameter validation / 종료·shared lifetime, handshake-ready 순서, idle sweep, listen 및 action parameter 엄격 검증 |
| StateSerializationTest | 13 | State JSON fields, reproducibility, device-loss/recording/IPC fields / 상태 JSON 필드, 재현성, 장치 손실/녹음/IPC 필드 |
| ActionDispatcherTest | 31 | Action dispatch, listener management, thread safety, ActionResult / 액션 디스패치, 리스너 관리, 스레드 안전, ActionResult |
| HttpApiServerTest | 5 | Restart/socket lifetime, input/listen-port validation, PDC-inclusive performance latency / 재시작·검증·PDC 포함 성능 레이턴시 |
| ActionResultTest | 12 | ActionResult data type: ok/fail factory methods, bool conversion, message propagation / ActionResult 데이터 타입 테스트 |
| ControlMappingTest | 19 | Hotkey/MIDI/server roundtrip plus strict enum/range/type validation / 핫키·MIDI·서버 왕복 및 enum/range/type 검증 |
| NotificationQueueTest | 10 | Lock-free SPSC notification queue: push/pop, FIFO, overflow, wrap-around, cross-thread / 락프리 SPSC 알림 큐 |
| PresetManager + portable/constants | 53 | Slot save/load/copy, canonical Base64 validation, structural backup fallback even when repair-copy fails, cache/ASIO/import guards / slot 저장·로드·복사, canonical Base64 검증, repair-copy 실패 시에도 backup fallback, cache·ASIO·import 보호 |
| SettingsExporterTest | 27 | Settings/full-backup exact restore, strict optional-action schema, transactional rollback, structural slot rejection / 설정·전체 백업 정확 복원, optional action schema 엄격 검증, transaction rollback, 잘못된 slot 거부 |
| SettingsAutosaverTest | 17 | Dirty/debounce auto-save, startup mute restore, partial-load guard / dirty·debounce auto-save, 시작 mute 복원, partial-load 보호 |
| OutputRouterTest | 10 | Monitor output routing, mute state, inactive meter reset / 모니터 출력 라우팅, 뮤트 상태, 비활성 meter reset |
| AudioEngineTest | 43 | Driver/device recovery, endpoint restart, separate manual/automatic mute ownership, directional state, ASIO/channel/XRun/SR-BS behavior / driver·장치 복구, endpoint restart, 수동·자동 mute ownership 분리, 방향 state, ASIO·channel·XRun·SR-BS 동작 |
| AudioRecorderTest | 7 | Recording lifecycle, output-path fallback, playback readiness, and invalid-state guards / 녹음 수명주기, 출력 경로 fallback, 재생 준비 상태, invalid-state 방어 |
| AudioRingBufferTest | 8 | Lock-free audio ring buffer reset/discard and fractional interpolation behavior / 오디오 링 버퍼 reset/discard 및 fractional interpolation 동작 |
| MonitorDriftPolicyTest | 10 | Adaptive monitor target, PLL ratio, and emergency trim policy / adaptive 모니터 target, PLL ratio, emergency trim 정책 |
| MonitorOutputTest | 10 | RT drain, lifecycle generation, active/fallback, priming/re-prime and drift behavior / RT drain, lifecycle generation, active·fallback, priming·re-prime 및 drift 동작 |
| DeviceStateTest | 10 | Device state FSM and invalid-state guards / 장치 상태 FSM 및 invalid-state 방어 |
| MidiHandlerTest | 11 | MIDI CC/Note mapping, learn mode / MIDI CC/노트 매핑, 학습 모드 |
| ActionHandlerTest | 8 | Panic mute engage/restore, callback order, explicit set-mode idempotency / 패닉 뮤트 활성화/복원, 콜백 순서, 명시 set 모드 멱등성 |
| SafetyLimiterTest | 15 | Guard ceiling, gain reduction, zero-latency sample-peak guard behavior / 가드 실링, 게인 리덕션, zero-latency 샘플-피크 가드 동작 |
| BuiltinFilterTest | 8 | HPF/LPF filter, frequency clamp, state roundtrip / HPF/LPF 필터, 주파수 클램프, 상태 왕복 |
| BuiltinNoiseRemovalTest + FIFO | 9 | RNNoise VAD thresholds, non-48k passthrough, latency, FIFO overflow guard / RNNoise VAD 임계값, 비-48kHz 패스스루, 레이턴시, FIFO overflow 보호 |
| BuiltinAutoGainTest | 8 | AGC boost/cut, freeze level, max gain clamp, post limiter ceiling/state/latency / AGC 부스트/컷, 프리즈 레벨, 최대 게인 클램프, post limiter 실링/상태/레이턴시 |
| VSTChainTest | 16 | VST chain operations, active-path PDC/bypass, stable status snapshot, cached-swap partial-failure guard / VST chain 연산, 활성 경로 PDC, 안정된 snapshot |
| PlatformTest | 8 | Platform abstraction: auto-start, process priority, multi-instance lock / 플랫폼 추상화 테스트 |
| SharedMemWriterTest | 4 | Initialization failure cleanup and in-flight write drain before unmap / 초기화 실패 정리 및 unmap 전 진행 write drain |
| EndpointChangeWatcherTest | 2 | Exact selected-endpoint filtering and signal coalescing / 선택 endpoint 정확 일치 및 signal coalescing |
| UpdateChecker/UpdateScript tests | 16 | Finished-worker reap/retry, strict semver/PID wait, staging, rollback, ZIP and percent-path handling / 완료 worker reap·재시도, 엄격 semver·PID wait, staging·rollback·ZIP·percent path 처리 |

Host test source files additionally include HTTP/WebSocket shutdown, shared-memory writer, endpoint watcher, and updater-script coverage. `test_endpoint_change_watcher.cpp` is also built as the dedicated focused executable so the two endpoint cases can run independently of the full host binary.

호스트 테스트에는 HTTP/WebSocket 종료, shared-memory writer, endpoint watcher, updater script 범위도 포함됩니다. `test_endpoint_change_watcher.cpp`는 host 전체와 독립적으로 실행할 수 있도록 endpoint 전용 실행 파일에도 빌드됩니다.

### GTest JSON Output / GTest JSON 출력

`tools/pre-release-test.sh` generates GTest JSON output files (`core-test-results.json`, `host-test-results.json`) that can be loaded into the pre-release dashboard for visual test result inspection.

`tools/pre-release-test.sh`는 GTest JSON 출력 파일을 생성하며, 프리릴리즈 대시보드에서 로드하여 시각적으로 테스트 결과를 확인할 수 있습니다.

```bash
# Run all tests / 전체 테스트 실행
cd build && ctest --config Release --output-on-failure

# Run specific test group / 특정 그룹만 실행
./bin/Release/directpipe-tests --gtest_filter="RingBufferTest.*"
./tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe --gtest_filter="ActionDispatcherTest.*"

# Generate JSON output for dashboard / 대시보드용 JSON 출력 생성
bash tools/pre-release-test.sh --skip-api
```

> `tools/pre-release-test.sh`는 Windows Git Bash와 고정 Visual Studio CMake 경로 기준입니다. 실행 중인 DirectPipe를 강제 종료하지 않으므로 API는 의도적으로 실행한 instance에 `--api-only`로 먼저 검증하고, build/unit 단계는 `--skip-api`로 실행할 수 있습니다. macOS/Linux에서는 동일 흐름을 수동 명령으로 실행하는 것을 권장합니다.
>
> `tools/pre-release-test.sh` targets Windows Git Bash and a fixed Visual Studio CMake path. It never force-terminates DirectPipe: run `--api-only` against a deliberate test instance, then use `--skip-api` for build/unit steps. On macOS/Linux, run equivalent steps manually.

---

## CI/CD (GitHub Actions)

워크플로우 파일: `.github/workflows/build.yml`

### 트리거 / Triggers

| 이벤트 / Event | 조건 / Condition | 동작 / Action |
|---|---|---|
| Pull Request | `v4` 또는 `main` 브랜치 대상 / targeting `v4` or `main` | Windows/macOS/Linux + Stream Deck 빌드/테스트 (릴리스 업로드 없음) / Build + test (no upload) |
| workflow_dispatch | 이미 push된 정확한 `vX.Y.Z` tag 입력 / Manual with an existing exact tag | tag/version 검증 → 전체 빌드/테스트 → draft asset/checksum 업로드 → public latest 공개 / Validate, build/test, upload a complete draft, then publish latest |

### 빌드 매트릭스 / Build Matrix

| 플랫폼 / Platform | Runner | 결과물 / Output | 포함 내용 / Includes |
|---|---|---|---|
| Windows | `windows-latest` | `DirectPipe-{tag}-Windows.zip` | DirectPipe.exe + Receiver VST2(.dll) + VST3(.vst3) |
| macOS | `macos-14` (ARM) | `DirectPipe-{tag}-macOS.dmg` | DirectPipe.app + Receiver VST2(.vst) + VST3(.vst3) + AU(.component) |
| Linux | `ubuntu-24.04` | `DirectPipe-{tag}-Linux.tar.gz` | DirectPipe + Receiver VST2(.so) + VST3(.vst3) |
| Stream Deck | `ubuntu-latest` | `com.directpipe.directpipe.streamDeckPlugin` | Node.js 20, npm ci + test + rollup + validate + streamdeck pack |

### GitHub Secrets (필수 / Required)

| Secret | 용도 / Purpose | 없을 때 / Without |
|---|---|---|
| `VST2_SDK_B64` | Base64-encoded VST2 SDK archive | PR에서는 VST2 생략, release-tag 실행은 실패 / VST2 skipped on PR; release-tag run fails |
| `ASIO_SDK_B64` | Base64-encoded Steinberg ASIO SDK (Windows only) | PR에서는 ASIO 비활성, release-tag 실행은 실패 / ASIO disabled on PR; release-tag run fails |

> VST2/ASIO SDK는 라이선스 제약으로 리포지토리에 포함되지 않습니다. CI에서는 Secrets에서 디코딩하여 `thirdparty/` 하위에 복원합니다.
> VST2/ASIO SDKs are excluded from the repository due to licensing. CI decodes them from Secrets into `thirdparty/`.

### 릴리스 프로세스 / Release Process

1. 검증된 commit을 `main`에 push하고 같은 commit에 immutable `vX.Y.Z` tag를 만들어 push / Push the validated commit and its immutable exact tag.
2. `gh workflow run build.yml --repo LiveTrack-X/DirectPipe -f release_tag=vX.Y.Z`로 `Build & Release` 실행 / Dispatch the workflow with the exact pushed tag.
3. CI가 tag/version을 확인하고 Windows/macOS/Linux + Stream Deck을 모두 빌드·테스트 / CI validates the tag/version and builds/tests every platform artifact.
4. 모든 산출물과 `checksums.sha256` 준비 후에만 draft를 public latest로 전환 / Only after every asset and checksum is ready, publish the draft as latest.
5. 릴리스 asset 네이밍: `DirectPipe-{tag}-{platform}.{ext}` / Asset naming convention.

> Release CI가 마지막 publish 단계에서 `--latest`를 명시합니다. 공개 GitHub Release를 먼저 수동 생성하지 마세요. asset 없는 public latest를 막기 위해 CI가 완성된 draft를 직접 생성·공개합니다.
> Release CI explicitly sets `--latest` only in its final publish step. Do not create the public GitHub Release first; CI creates and publishes the complete draft so an assetless public latest cannot appear.
