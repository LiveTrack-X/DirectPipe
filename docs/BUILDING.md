# Building DirectPipe / 빌드 가이드

> **Current release / 현재 릴리즈: 4.2.8**

> **플랫폼 지원 상태**: Windows 10/11 x64는 안정 릴리즈 대상입니다. v4.2.8은 로컬 Windows Release 등록 582개 중 580개 통과, 환경 의존 2개 건너뜀, 실패 0개로 완료하고 정확 태그 CI run `30640495424`의 전체 플랫폼 빌드·패키지·checksum·Windows unsigned 상태 검증을 통과했습니다. 실기기·제3자 VST crash-containment는 별도 검증 범위입니다.
> **Platform support**: Windows 10/11 x64 is the stable release target. v4.2.8 completed 582 local Windows Release registrations with 580 passed, 2 environment-dependent skips, and 0 failures, then passed exact-tag CI run `30640495424`, including cross-platform builds, packages, checksums, and Windows unsigned-state validation. Real-device and third-party VST crash-containment remain separate evidence.

## Support Status / 지원 상태

| Platform | Release status | CI target | Audio backend | Notes |
|---|---|---|---|---|
| Windows 10/11 x64 | Stable release / 안정 릴리즈 | `windows-latest` | WASAPI Shared/Low Latency/Exclusive, ASIO when SDK is available | v4.2.8 passed local Release and exact-tag CI; five public assets and checksums were verified. CI requires unsigned binaries because no trusted signing secrets are configured; no real-device claim. |
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

Three test executables are built: `directpipe-tests` (core, no JUCE dependency), `directpipe-host-tests` (JUCE host coverage), and `directpipe-endpoint-watcher-tests` (focused endpoint notification coverage). v4.2.8 passed all 582 local Windows Release registrations (580 passed, 2 environment-dependent skips, 0 failed), exact-tag CI run `30640495424`, package checksums, and executable-version identity gates. Authenticode remains optional until a trusted certificate is configured.

세 개의 테스트 실행 파일을 빌드합니다: `directpipe-tests`(코어, JUCE 의존성 없음), `directpipe-host-tests`(JUCE 호스트 범위), `directpipe-endpoint-watcher-tests`(endpoint 알림 집중 검증). v4.2.8은 로컬 Windows Release 등록 582개 중 580개 통과, 환경 의존 2개 건너뜀, 실패 0개로 완료하고 정확 태그 CI run `30640495424`, 패키지 checksum, 실행 파일 버전 신원 게이트를 통과했습니다. 신뢰 인증서가 설정되기 전 Authenticode는 선택 사항입니다.

### directpipe-tests (Core)

| Test Group | Tests | Description |
|------------|-------|-------------|
| RingBufferTest | 23 | SPSC ring buffer correctness, atomic consumer claim, restart generation, bounds checks, and concurrency / 링 버퍼 정확성, atomic consumer 획득, 재시작 generation, 경계 검사, 동시성 |
| SharedMemoryTest | 9 | Shared memory create/map, named events, full IPC pipeline / 공유 메모리 생성/매핑, named event, 전체 IPC 파이프라인 |
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
| HttpApiServerTest | 7 | Restart/socket lifetime, input/listen-port validation, queued `202` compatibility, PDC-inclusive performance latency / 재시작·검증·queued `202` 호환·PDC 포함 성능 레이턴시 |
| ActionResultTest | 12 | ActionResult data type: ok/fail factory methods, bool conversion, message propagation / ActionResult 데이터 타입 테스트 |
| ControlMappingTest | 19 | Hotkey/MIDI/server roundtrip plus strict enum/range/type validation / 핫키·MIDI·서버 왕복 및 enum/range/type 검증 |
| NotificationQueueTest | 10 | Lock-free SPSC notification queue: push/pop, FIFO, overflow, wrap-around, cross-thread / 락프리 SPSC 알림 큐 |
| PresetManager + portable/constants | 57 | Slot save/load/copy, canonical Base64 validation, structural backup fallback even when repair-copy fails, cache/ASIO/import/channel-mask guards / slot 저장·로드·복사, canonical Base64 검증, repair-copy 실패 시에도 backup fallback, cache·ASIO·import·채널 mask 보호 |
| SettingsExporterTest | 27 | Settings/full-backup exact restore, strict optional-action schema, transactional rollback, structural slot rejection / 설정·전체 백업 정확 복원, optional action schema 엄격 검증, transaction rollback, 잘못된 slot 거부 |
| SettingsAutosaverTest | 25 | Debounced save, transitional/partial-chain guard, preserved-chain merge, shutdown recovery sidecar, bounded retry, backup fallback, reset behavior, manual/automatic mute separation / debounce 저장, 전환·부분 chain 보호, 보존 chain 병합, 종료 복구 sidecar, 제한 재시도, backup fallback, reset 및 수동·자동 mute 분리 |
| OutputRouterTest | 10 | Monitor output routing, mute state, inactive meter reset / 모니터 출력 라우팅, 뮤트 상태, 비활성 meter reset |
| LatencyMonitorTest | 2 | Device/PDC total-estimate behavior / 장치·PDC 총 추정 레이턴시 동작 |
| AudioEngineTest | 90 | Transactional driver/device/SR/BS/channel recovery, per-candidate runtime validation, packed/sparse selected-pair routing, input-front dual mono, genuine-mono output switching, exact rollback/fail-closed behavior, endpoint restart, mute ownership, ASIO/channel/XRun behavior / 트랜잭션 driver·장치·SR·BS·channel 복구, 후보별 runtime 검증, packed/sparse 선택 pair 라우팅, 입력단 dual mono, 실제 1채널 출력 전환, 정확 rollback·fail-closed, endpoint restart, mute ownership, ASIO·channel·XRun 동작 |
| AudioRecorderTest | 7 | Recording lifecycle, output-path fallback, playback readiness, and invalid-state guards / 녹음 수명주기, 출력 경로 fallback, 재생 준비 상태, invalid-state 방어 |
| AudioRingBufferTest | 8 | Lock-free audio ring buffer reset/discard and fractional interpolation behavior / 오디오 링 버퍼 reset/discard 및 fractional interpolation 동작 |
| MonitorDriftPolicyTest | 10 | Adaptive monitor target, PLL ratio, and emergency trim policy / adaptive 모니터 target, PLL ratio, emergency trim 정책 |
| MonitorOutputTest | 17 | Selected-device-first open, bounded failed recovery, enumerated-open backoff, reconnect log cadence, RT drain, lifecycle generation, active/fallback, priming/re-prime and drift behavior / 선택 장치 우선 open, 실패 복구 제한, 열거 장치 open backoff, 재연결 로그 주기, RT drain, lifecycle generation, active·fallback, priming·re-prime 및 drift 동작 |
| DeviceStateTest | 10 | Device state FSM and invalid-state guards / 장치 상태 FSM 및 invalid-state 방어 |
| MidiHandlerTest | 11 | MIDI CC/Note mapping, learn mode / MIDI CC/노트 매핑, 학습 모드 |
| ActionHandlerTest | 8 | Panic mute engage/restore, callback order, explicit set-mode idempotency / 패닉 뮤트 활성화/복원, 콜백 순서, 명시 set 모드 멱등성 |
| SafetyLimiterTest | 15 | Guard ceiling, gain reduction, zero-latency sample-peak guard behavior / 가드 실링, 게인 리덕션, zero-latency 샘플-피크 가드 동작 |
| BuiltinFilterTest | 8 | HPF/LPF filter, frequency clamp, state roundtrip / HPF/LPF 필터, 주파수 클램프, 상태 왕복 |
| BuiltinNoiseRemovalTest + FIFO | 9 | RNNoise VAD thresholds, non-48k passthrough, latency, FIFO overflow guard / RNNoise VAD 임계값, 비-48kHz 패스스루, 레이턴시, FIFO overflow 보호 |
| BuiltinAutoGainTest | 8 | AGC boost/cut, freeze level, max gain clamp, post limiter ceiling/state/latency / AGC 부스트/컷, 프리즈 레벨, 최대 게인 클램프, post limiter 실링/상태/레이턴시 |
| VSTChainTest | 18 | VST chain operations, late active-path PDC refresh/bypass, stable status snapshot, cached-swap partial-failure guard / VST chain 연산, 늦은 활성 경로 PDC 갱신·bypass, 안정된 snapshot |
| PlatformTest | 8 | Platform abstraction: auto-start, process priority, multi-instance lock / 플랫폼 추상화 테스트 |
| PlatformAudioTest | 1 | Shared/exclusive Windows driver classification / Windows shared·exclusive 드라이버 분류 |
| SharedMemWriterTest | 4 | Initialization failure cleanup and in-flight write drain before unmap / 초기화 실패 정리 및 unmap 전 진행 write drain |
| EndpointChangeWatcherTest | 2 | Exact selected-endpoint filtering and signal coalescing / 선택 endpoint 정확 일치 및 signal coalescing |
| UpdateChecker/UpdateScript tests | 21 | Finished-worker reap/retry, strict semver/PID wait, checksum/identity validation, single-EXE staging, retained backup/rollback, ZIP and percent-path handling / 완료 worker reap·재시도, 엄격 semver·PID wait, checksum·신원 검증, 단일 EXE staging, backup 보존·rollback, ZIP·percent path 처리 |

Host test source files additionally include HTTP/WebSocket shutdown, shared-memory writer, endpoint watcher, and updater-script coverage. `test_endpoint_change_watcher.cpp` is also built as the dedicated focused executable so the two endpoint cases can run independently of the full host binary.

호스트 테스트에는 HTTP/WebSocket 종료, shared-memory writer, endpoint watcher, updater script 범위도 포함됩니다. `test_endpoint_change_watcher.cpp`는 host 전체와 독립적으로 실행할 수 있도록 endpoint 전용 실행 파일에도 빌드됩니다.

### GTest JSON Output / GTest JSON 출력

`tools/pre-release-test.sh` generates GTest JSON output files (`core-test-results.json`, `host-test-results.json`, and `endpoint-test-results.json`) that can be loaded into the pre-release dashboard or retained as validation evidence.

`tools/pre-release-test.sh`는 core, host, endpoint GTest JSON 출력 파일을 생성하며, 프리릴리즈 대시보드에서 로드하거나 검증 증거로 보존할 수 있습니다.

Exact-tag CI uploads 30-day JUnit/CTest logs for Windows, macOS, and Linux plus
the Stream Deck TAP log as separate evidence artifacts; these logs are not
included in the public release payload or `checksums.sha256`.

정확 태그 CI는 Windows·macOS·Linux JUnit/CTest 로그와 Stream Deck TAP
로그를 별도 검증 artifact로 30일 보존합니다. 이 로그는 공개 릴리즈
payload와 `checksums.sha256`에는 포함하지 않습니다.

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
| `WINDOWS_SIGNING_PFX_B64` | Optional Base64-encoded trusted Authenticode PFX | 비어 있으면 Windows package를 명시적으로 unsigned 게시 / explicitly unsigned when absent |
| `WINDOWS_SIGNING_PFX_PASSWORD` | Password paired with the optional PFX | PFX와 둘 중 하나만 있으면 구성 오류로 release 실패 / partial configuration fails |

> VST2/ASIO SDK는 라이선스 제약으로 리포지토리에 포함되지 않습니다. CI에서는 Secrets에서 디코딩하여 `thirdparty/` 하위에 복원합니다. Windows 서명 secret은 선택 사항이며 둘 다 있으면 서명·검증하고, 둘 다 없으면 모든 staged 바이너리가 `NotSigned`인지 확인한 뒤 unsigned 상태를 명시합니다. 하나만 설정된 불완전 구성이나 서명 상태 불일치는 실패합니다.
> VST2/ASIO SDKs are restored from Secrets. Windows signing is optional: both secrets sign and verify the binaries; neither requires every staged binary to be `NotSigned` before publishing an explicitly unsigned package. Partial configuration or any signature-state mismatch fails.

### 릴리스 프로세스 / Release Process

1. 검증된 commit을 `main`에 push하고 같은 commit에 immutable `vX.Y.Z` tag를 만들어 push / Push the validated commit and its immutable exact tag.
2. `gh workflow run build.yml --repo LiveTrack-X/DirectPipe -f release_tag=vX.Y.Z`로 `Build & Release` 실행 / Dispatch the workflow with the exact pushed tag.
3. CI가 tag/version을 확인하고 Windows/macOS/Linux + Stream Deck을 모두 빌드·테스트 / CI validates the tag/version and builds/tests every platform artifact.
4. 모든 산출물과 `checksums.sha256` 준비 후에만 draft를 public latest로 전환 / Only after every asset and checksum is ready, publish the draft as latest.
5. 릴리스 asset 네이밍: `DirectPipe-{tag}-{platform}.{ext}` / Asset naming convention.

> Release CI가 마지막 publish 단계에서 `--latest`를 명시합니다. 공개 GitHub Release를 먼저 수동 생성하지 마세요. asset 없는 public latest를 막기 위해 CI가 완성된 draft를 직접 생성·공개합니다.
> Release CI explicitly sets `--latest` only in its final publish step. Do not create the public GitHub Release first; CI creates and publishes the complete draft so an assetless public latest cannot appear.
