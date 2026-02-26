<p align="center">
  <img src="docs/images/banner.svg" alt="DirectPipe" width="100%">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-Windows%2010%2F11-0078d4?style=flat-square&logo=windows" alt="Platform">
  <img src="https://img.shields.io/badge/version-3.3.0-4fc3f7?style=flat-square" alt="Version">
  <img src="https://img.shields.io/badge/C%2B%2B17-JUCE%207-00599C?style=flat-square&logo=cplusplus" alt="C++17">
  <img src="https://img.shields.io/badge/license-GPL--3.0-green?style=flat-square" alt="License">
  <img src="https://img.shields.io/badge/VST2%20%2B%20VST3-supported-ff6f00?style=flat-square" alt="VST">
</p>

Windows용 실시간 VST2/VST3 호스트. 마이크 입력에 VST 플러그인 체인을 걸어 실시간으로 처리하고, 메인 출력(AudioSettings Output 장치)으로 직접 전송한다. 별도 WASAPI 장치를 통한 모니터 출력(헤드폰)도 지원. Light Host와 비슷하지만 키보드 단축키 / MIDI CC / Stream Deck / HTTP API를 통한 외부 제어와 빠른 프리셋 전환에 초점을 맞추었다.

Real-time VST2/VST3 host for Windows. Processes microphone input through a VST plugin chain, with main output going directly to the AudioSettings Output device. Optional separate WASAPI monitor output for headphones. Similar to Light Host, but focused on external control (hotkeys, MIDI CC, Stream Deck, HTTP API) and fast preset switching.

<p align="center">
  <img src="docs/images/main-ui.png" alt="DirectPipe Main UI" width="700">
</p>

## 동작 원리 / How It Works

```
Mic -> WASAPI Shared / ASIO -> VST2/VST3 Plugin Chain
                                    |
                              Main Output (AudioSettings Output device)
                                    \
                               OutputRouter -> Monitor Output (Headphones, separate WASAPI)

External Control:
  Hotkeys / MIDI CC / Stream Deck / HTTP / WebSocket
    -> ActionDispatcher -> Plugin Bypass, Volume, Preset Switch, Mute ...
```

## 주요 기능 / Features

### VST 호스팅 / VST Hosting

- **VST2 + VST3** 플러그인 로드 및 인라인 실시간 처리 — Load and process plugins inline in real time
- **드래그 앤 드롭** 플러그인 체인 편집 — Drag & drop to reorder plugins, toggle bypass, open native plugin GUIs
- **Out-of-process 스캐너** — Scans plugins in a separate process; bad plugin crashes don't affect the host (auto-retry with dead man's pedal)
- **Quick Preset Slots (A-E)** — 5개 체인 전용 프리셋. 같은 체인이면 bypass/파라미터만 즉시 전환, 다른 체인이면 비동기 로딩 — 5 chain-only presets with instant or async switching

### 오디오 / Audio

- **WASAPI Shared + ASIO** 듀얼 드라이버, 런타임 전환 — Dual driver support with runtime switching
- WASAPI Shared 비독점 마이크 접근 — Non-exclusive mic access, other apps can use the mic simultaneously
- **메인 출력 + 모니터** — Main output to AudioSettings device + optional monitor (headphones) via separate WASAPI
- **Mono / Stereo** 채널 모드 — Channel mode selection
- **입력 게인** 조절 — Input gain control
- **실시간 레벨 미터** (입력/출력 RMS, 좌우 대칭 배치) — Real-time input/output level meters (symmetric vertical layout)

### 외부 제어 / External Control

- **키보드 단축키** — Ctrl+Shift+1~9 plugin bypass, Ctrl+Shift+M panic mute, Ctrl+Shift+F1~F5 preset slots
- **MIDI CC** — Learn 모드로 CC/노트 매핑 — CC/note mapping with Learn mode
- **WebSocket** (RFC 6455, port 8765) — 양방향 실시간 통신, 상태 자동 푸시 — Bidirectional real-time communication with auto state push
- **HTTP REST API** (port 8766) — curl이나 브라우저에서 원샷 커맨드 — One-shot commands from curl or browser
- **Stream Deck 플러그인** (SDK v2) — Bypass Toggle, Panic Mute, Volume Control, Preset Switch, Monitor Toggle

### UI

- **시스템 트레이** — X 버튼으로 트레이 최소화, 더블클릭 복원, 시작 프로그램 등록 — Close minimizes to tray, double-click to restore, Start with Windows toggle
- **탭 설정** — Audio / Monitor / Controls (Hotkeys, MIDI, Stream Deck, General) — Tabbed settings panel
- **Panic Mute** — 전체 출력 즉시 뮤트, 해제 시 이전 상태 복원 — Mute all outputs instantly, restores previous state on unmute
- **Output / Monitor Mute** — 개별 출력 뮤트 (UI 인디케이터 + 클릭 제어) — Independent output/monitor mute with clickable status indicators
- **다크 테마** — Dark theme (custom JUCE LookAndFeel)

## 사용 예시: 가상 케이블로 Discord/OBS에 보이스 이펙트 적용 / Usage: Voice Effects with Virtual Cable

USB 마이크에 VST 이펙트(노이즈 제거, 디에서, EQ 등)를 걸고, 처리된 오디오를 Discord·Zoom·OBS 등 다른 앱에서 마이크로 인식시키려면 가상 오디오 케이블이 필요하다.

To apply VST effects (noise removal, de-esser, EQ, etc.) to a USB mic and route the processed audio as a virtual microphone to apps like Discord, Zoom, or OBS, you need a virtual audio cable.

### 설정 방법 / Setup

1. **[VB-Audio Virtual Cable](https://vb-audio.com/Cable/)** 설치 (무료) — Install VB-Audio Virtual Cable (free)
2. DirectPipe **Audio** 탭에서 설정 — Configure in DirectPipe Audio tab:
   - **Input**: USB 마이크 선택 — Select your USB microphone
   - **Output**: `CABLE Input (VB-Audio Virtual Cable)` 선택 — Select VB-Cable as output
3. Discord/Zoom/OBS 등에서 마이크를 `CABLE Output (VB-Audio Virtual Cable)`로 변경 — In your app, set mic to `CABLE Output`

```
USB Mic → DirectPipe (VST Chain: 노이즈 제거, EQ, 컴프 ...) → VB-Cable Input
                                                                      ↓
                                                Discord/Zoom/OBS ← VB-Cable Output
```

4. (선택) **Monitor** 탭에서 헤드폰 장치를 설정하면 처리된 자신의 목소리를 실시간으로 모니터링 가능 — Optionally configure headphone monitoring in the Monitor tab

> **Tip**: [VoiceMeeter](https://vb-audio.com/Voicemeeter/) 등 다른 가상 오디오 장치도 동일하게 사용 가능. Output 장치만 바꾸면 된다. — Any virtual audio device works; just change the Output device.

---

## 빌드 / Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

자세한 내용은 [Build Guide](docs/BUILDING.md) 참조 / See Build Guide for details.

### 요구 사항 / Requirements

- Windows 10/11 (64-bit)
- Visual Studio 2022 (C++ Desktop Development)
- CMake 3.22+
- JUCE 7.0.12 (CMake FetchContent 자동 다운로드 / auto-fetched)
- ASIO SDK (`thirdparty/asiosdk/`) — ASIO 모드 사용 시 / for ASIO driver support (optional)

## 프로젝트 구조 / Project Structure

```
host/                     JUCE host application (main)
  Source/
    Audio/                  AudioEngine, VSTChain, OutputRouter, VirtualMicOutput,
                            AudioRingBuffer, LatencyMonitor
    Control/                ActionDispatcher, ControlManager, ControlMapping,
                            WebSocketServer, HttpApiServer,
                            HotkeyHandler, MidiHandler, StateBroadcaster
    IPC/                    SharedMemWriter
    UI/                     AudioSettings, OutputPanel, ControlSettingsPanel,
                            PluginChainEditor, PluginScanner, PresetManager,
                            LevelMeter, DirectPipeLookAndFeel
core/                     IPC library (RingBuffer, SharedMemory, Protocol)
streamdeck-plugin/        Stream Deck plugin (Node.js, SDK v2)
dist/                     Packaged plugin (.streamDeckPlugin) + marketplace assets
tests/                    Unit tests (Google Test)
thirdparty/               VST2 SDK, ASIO SDK (not included, see BUILDING.md)
```

## 문서 / Documentation

- [Architecture](docs/ARCHITECTURE.md) — 시스템 설계 / System design, data flow, thread safety
- [Build Guide](docs/BUILDING.md) — 빌드 가이드 / Build instructions and options
- [User Guide](docs/USER_GUIDE.md) — 사용법 / Setup and usage
- [Control API](docs/CONTROL_API.md) — WebSocket / HTTP API 레퍼런스 / API reference
- [Stream Deck Guide](docs/STREAMDECK_GUIDE.md) — Stream Deck 플러그인 / Stream Deck integration

## FAQ

<details>
<summary><b>처음 실행할 때 빨간색 "Windows의 PC 보호" 경고가 떠요 / SmartScreen warning on first run</b></summary>

정상입니다! DirectPipe는 오픈소스라 코드 서명 인증서가 없어서 나타나는 경고입니다. 악성 소프트웨어가 아닙니다.

1. **"추가 정보"** 텍스트를 클릭하세요
2. 아래에 나타나는 **"실행"** 버튼을 누르세요
3. 한 번만 하면 이후로는 경고 없이 실행됩니다

> This is expected for open-source software. Click **"More info"** → **"Run anyway"**. It only appears once.
</details>

<details>
<summary><b>VST 플러그인이 뭔가요? 어디서 구하나요? / What are VST plugins and where to get them?</b></summary>

VST 플러그인은 오디오에 효과를 추가하는 소프트웨어입니다. 노이즈 제거, EQ, 컴프레서 등 다양한 종류가 있습니다.

**무료 추천 플러그인:**
- [ReaPlugs](https://www.reaper.fm/reaplugs/) — EQ, 컴프레서, 게이트 등 기본 플러그인 모음 (무료)
- [RNNoise](https://github.com/werman/noise-suppression-for-voice) — AI 기반 실시간 노이즈 제거 (무료)
- [TDR Nova](https://www.tokyodawn.net/tdr-nova/) — 고품질 다이나믹 EQ (무료)
- [OrilRiver](https://www.kvraudio.com/product/orilriver-by-denis-tihanov) — 리버브 (무료)

설치 후 DirectPipe에서 **"Scan..."** 버튼으로 플러그인 폴더를 스캔하면 목록에 나타납니다.

> VST plugins are audio effect processors. After installing them, click **"Scan..."** in DirectPipe to detect them.
</details>

<details>
<summary><b>Discord / Zoom / OBS에서 처리된 마이크를 쓰려면? / How to use with Discord, Zoom, or OBS?</b></summary>

[VB-Audio Virtual Cable](https://vb-audio.com/Cable/) (무료)이 필요합니다.

**설정 순서:**
1. VB-Audio Virtual Cable을 설치하고 PC를 재부팅합니다
2. DirectPipe **Audio** 탭:
   - **Input** → 내 USB 마이크 선택
   - **Output** → `CABLE Input (VB-Audio Virtual Cable)` 선택
3. Discord/Zoom/OBS 음성 설정:
   - **마이크** → `CABLE Output (VB-Audio Virtual Cable)` 선택

```
내 USB 마이크 → DirectPipe (노이즈 제거, EQ 등) → VB-Cable Input
                                                         ↓
                                       Discord/Zoom/OBS ← VB-Cable Output (마이크로 인식)
```

4. 자기 목소리를 확인하려면 **Monitor** 탭에서 헤드폰 장치를 설정하세요

> Install [VB-Audio Virtual Cable](https://vb-audio.com/Cable/), set DirectPipe Output to `CABLE Input`, then select `CABLE Output` as your mic in Discord/Zoom/OBS.
</details>

<details>
<summary><b>소리가 안 나와요 / 마이크가 인식이 안 돼요 / No sound or mic not detected</b></summary>

**확인 순서:**
1. **Audio 탭** → Input 장치가 올바르게 선택되어 있는지 확인
2. 왼쪽 **INPUT 레벨 미터**가 움직이는지 확인 → 움직이면 마이크 입력은 정상
3. 레벨 미터가 움직이지 않으면:
   - Windows 설정 → 개인 정보 → 마이크에서 앱 접근이 허용되어 있는지 확인
   - 다른 앱(Discord 등)이 마이크를 독점 모드로 사용 중이면 해제
4. **OUT** 버튼이 초록색인지 확인 (빨간색이면 뮤트 상태 → 클릭해서 해제)
5. **PANIC MUTE**가 활성화되어 있으면 다시 클릭해서 해제

> Check: Input device → INPUT meter moving → OUT button green (not muted) → PANIC MUTE off.
</details>

<details>
<summary><b>소리가 끊기거나 지연이 커요 / Audio crackling or high latency</b></summary>

**Buffer Size를 조절하세요:**
- Audio 탭 → **Buffer Size**: 값을 낮추면 지연이 줄지만 CPU 부담 증가
  - `256 samples` (약 5ms @ 48kHz) — 일반적인 시작점
  - `128 samples` (약 2.7ms) — 낮은 지연, 고사양 PC 권장
  - `512 samples` (약 10ms) — 안정적, 저사양 PC 권장

**그래도 끊긴다면:**
- 플러그인 수를 줄이거나, CPU를 많이 쓰는 플러그인을 Bypass 처리
- WASAPI 대신 **ASIO** 드라이버 사용 (더 낮은 지연 가능)
- 하단 상태 바의 **CPU %** 수치를 확인 — 60% 이상이면 과부하

> Lower the **Buffer Size** for less latency. If crackling persists, reduce plugins or switch to ASIO driver.
</details>

<details>
<summary><b>ASIO가 뭔가요? 꼭 필요한가요? / What is ASIO? Do I need it?</b></summary>

**ASIO**는 저지연 오디오 드라이버입니다. 대부분의 USB 마이크는 **WASAPI** 모드로 충분히 잘 동작하므로 반드시 필요하지는 않습니다.

| | WASAPI Shared | ASIO |
|---|---|---|
| 지연(Latency) | 보통 (5~15ms) | 매우 낮음 (2~5ms) |
| 설치 | 별도 설치 불필요 | 오디오 인터페이스 드라이버 필요 |
| 다른 앱과 동시 사용 | 가능 (비독점) | 장치에 따라 다름 |
| 추천 대상 | 일반 사용자 | 전문가, 실시간 모니터링 |

> WASAPI works fine for most users. ASIO is optional for those who need ultra-low latency.
</details>

<details>
<summary><b>플러그인 스캔 중 프로그램이 멈춘 것 같아요 / Plugin scan seems stuck</b></summary>

플러그인 스캔은 **별도 프로세스**에서 실행되므로 DirectPipe가 멈추거나 크래시하지 않습니다. 일부 플러그인은 스캔에 시간이 오래 걸릴 수 있습니다 (최대 5분).

- 크래시를 유발하는 플러그인은 자동으로 **블랙리스트**에 등록되어 다음 스캔에서 건너뜁니다
- 스캔 로그: `%AppData%/DirectPipe/scanner-log.txt`에서 확인 가능

> Scans run in a separate process — the host never crashes. Bad plugins are auto-blacklisted.
</details>

<details>
<summary><b>프리셋은 어떻게 사용하나요? / How to use presets?</b></summary>

**Quick Preset Slots (A~E):**
- 현재 플러그인 체인과 설정을 **A~E** 슬롯에 저장할 수 있습니다
- **Save Preset** → 현재 상태를 선택된 슬롯에 저장
- 슬롯 버튼 **(A/B/C/D/E)** 클릭 → 즉시 전환
- 같은 플러그인이면 파라미터만 바꿔서 **즉시 전환**, 다른 플러그인이면 **비동기 로딩**

예: 게임 중엔 **A** (노이즈 제거만), 노래방에선 **B** (리버브 + 컴프레서)

> Save your plugin chain to slots A-E. Click a slot to switch instantly.
</details>

<details>
<summary><b>Monitor 출력은 뭔가요? / What is Monitor output?</b></summary>

**Monitor**는 자기 목소리를 헤드폰으로 실시간 확인하는 기능입니다. VST 이펙트가 적용된 자신의 목소리를 들을 수 있습니다.

- **Monitor 탭**에서 헤드폰이 연결된 오디오 장치를 선택
- Main Output과는 별도의 WASAPI 장치를 사용하므로 **독립적으로 동작**
- **MON** 버튼으로 켜기/끄기

> Monitor lets you hear your processed voice through headphones in real-time. Configure in the Monitor tab.
</details>

<details>
<summary><b>컴퓨터 시작할 때 자동으로 실행되게 하려면? / How to auto-start with Windows?</b></summary>

두 가지 방법:
1. **시스템 트레이** 아이콘 우클릭 → **"Start with Windows"** 체크
2. **Controls** 탭 → **General** → **"Start with Windows"** 체크

활성화하면 Windows 시작 시 자동으로 트레이에서 실행됩니다. X 버튼으로 창을 닫아도 트레이에 남아서 계속 동작합니다.

> Right-click the tray icon → "Start with Windows", or enable in Controls > General tab.
</details>

<details>
<summary><b>Stream Deck 없이도 외부 제어가 되나요? / Can I control without a Stream Deck?</b></summary>

네! 다양한 방법으로 제어할 수 있습니다:

| 방법 | 예시 | 적합한 용도 |
|---|---|---|
| **키보드 단축키** | Ctrl+Shift+1~9 bypass, F1~F5 프리셋 | 가장 간편 |
| **MIDI CC** | 미디 컨트롤러 노브/버튼 | 실시간 볼륨 조절 |
| **HTTP API** | `curl http://localhost:8766/api/...` | 스크립트 자동화 |
| **WebSocket** | ws://localhost:8765 | 커스텀 앱/봇 연동 |

자세한 내용: [Control API](docs/CONTROL_API.md)

> Hotkeys, MIDI CC, HTTP API, and WebSocket are all supported. See [Control API](docs/CONTROL_API.md).
</details>

## License

GPL v3 — [LICENSE](LICENSE)
