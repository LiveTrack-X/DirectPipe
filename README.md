<p align="center">
  <img src="docs/images/banner.svg" alt="DirectPipe" width="100%">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-Windows%2010%2F11-0078d4?style=flat-square&logo=windows" alt="Platform">
  <img src="https://img.shields.io/badge/version-3.6.0-4fc3f7?style=flat-square" alt="Version">
  <img src="https://img.shields.io/badge/C%2B%2B17-JUCE%207-00599C?style=flat-square&logo=cplusplus" alt="C++17">
  <img src="https://img.shields.io/badge/license-GPL--3.0-green?style=flat-square" alt="License">
  <img src="https://img.shields.io/badge/VST2%20%2B%20VST3-supported-ff6f00?style=flat-square" alt="VST">
  <br>
  <a href="https://marketplace.elgato.com/product/directpipe-29f7cbb8-cb90-425d-9dbc-b2158e7ea8b3">
    <img src="https://img.shields.io/badge/Stream%20Deck-Marketplace-8B5CF6?style=for-the-badge&logo=elgato&logoColor=white" alt="Stream Deck Marketplace">
  </a>
</p>

Windows용 실시간 VST2/VST3 호스트. 마이크 입력에 VST 플러그인 체인을 걸어 실시간으로 처리하고, 메인 출력(AudioSettings Output 장치)으로 직접 전송한다. 별도 WASAPI 장치를 통한 모니터 출력(헤드폰)도 지원. Light Host와 비슷하지만 키보드 단축키 / MIDI CC / Stream Deck / HTTP API를 통한 외부 제어와 빠른 프리셋 전환에 초점을 맞추었다.

Real-time VST2/VST3 host for Windows. Processes microphone input through a VST plugin chain, with main output going directly to the AudioSettings Output device. Optional separate WASAPI monitor output for headphones. Similar to Light Host, but focused on external control (hotkeys, MIDI CC, Stream Deck, HTTP API) and fast preset switching.

<p align="center">
  <img src="docs/images/main-ui.png" alt="DirectPipe Main UI" width="700">
</p>

---

<table>
<tr>
<td width="80" align="center">
  <a href="https://marketplace.elgato.com/product/directpipe-29f7cbb8-cb90-425d-9dbc-b2158e7ea8b3">
    <img src="https://img.shields.io/badge/🎛-000?style=for-the-badge" width="50" alt="">
  </a>
</td>
<td>
  <b>🎛 Stream Deck Plugin — <a href="https://marketplace.elgato.com/product/directpipe-29f7cbb8-cb90-425d-9dbc-b2158e7ea8b3">Elgato Marketplace에서 무료 설치</a></b><br>
  Bypass · Volume (SD+ 다이얼) · Preset · Monitor · Panic Mute · Recording · IPC Toggle — 7가지 액션으로 Stream Deck에서 DirectPipe를 완전 제어<br>
  <sub>Free on Elgato Marketplace — 7 actions to fully control DirectPipe from your Stream Deck</sub>
</td>
</tr>
</table>

---

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

- **키보드 단축키** — Ctrl+Shift+1–9 plugin bypass, Ctrl+Shift+M panic mute, Ctrl+Shift+F1–F5 preset slots
- **MIDI CC** — Learn 모드로 CC/노트 매핑 — CC/note mapping with Learn mode
- **WebSocket** (RFC 6455, port 8765) — 양방향 실시간 통신, 상태 자동 푸시 — Bidirectional real-time communication with auto state push
- **HTTP REST API** (port 8766) — curl이나 브라우저에서 원샷 커맨드 — One-shot commands from curl or browser
- **[Stream Deck 플러그인](https://marketplace.elgato.com/product/directpipe-29f7cbb8-cb90-425d-9dbc-b2158e7ea8b3)** — Bypass Toggle, Panic Mute, Volume Control (SD+ 다이얼 지원), Preset Switch, Monitor Toggle, Recording Toggle, IPC Toggle — [Elgato Marketplace에서 무료 설치](https://marketplace.elgato.com/product/directpipe-29f7cbb8-cb90-425d-9dbc-b2158e7ea8b3)

### IPC 출력 (Receiver VST) / IPC Output (Receiver VST)

- **Receiver VST2 플러그인** — OBS 등 VST2를 지원하는 앱에서 DirectPipe 처리 오디오를 공유 메모리(IPC)로 직접 수신. **가상 오디오 케이블 없이** OBS에 마이크 오디오를 바로 보낼 수 있다 — Receive processed audio directly via shared memory (IPC) in OBS or other VST2 hosts. Route mic audio to OBS **without a virtual audio cable**
- **IPC 토글** — 기본값은 OFF. Controls > General 탭의 체크박스, 단축키 Ctrl+Shift+I, MIDI, Stream Deck, HTTP API 등 다양한 방법으로 켜기/끄기 가능 — Off by default. Toggle via UI checkbox, hotkey Ctrl+Shift+I, MIDI, Stream Deck, or HTTP API
- **버퍼 크기 설정** — Receiver VST 플러그인 GUI에서 5단계 버퍼 프리셋 선택. 낮을수록 지연이 적지만 언더런 위험 증가 — 5 buffer presets in Receiver VST GUI. Lower = less latency but higher underrun risk

  | 프리셋 / Preset | 지연 / Latency | 용도 / Best for |
  |---|---|---|
  | Ultra Low | ~5ms | 최소 지연 필요 시 / Minimum latency |
  | Low | ~10ms (기본) | 일반 사용 / General use (default) |
  | Medium | ~21ms | 안정적 / Stable |
  | High | ~42ms | CPU 여유 적을 때 / Low CPU headroom |
  | Safe | ~85ms | 최대 안정성 / Maximum stability |

### 녹음 / Recording

- **오디오 녹음** — Monitor 탭에서 처리된 오디오를 WAV 파일로 녹음. Lock-free 실시간 안전 설계. — Record processed audio to WAV in Monitor tab. Lock-free, real-time safe design.
- **녹음 시간 표시** — Monitor 탭과 Stream Deck에서 실시간 경과 시간 확인 — Real-time duration display in Monitor tab and Stream Deck
- **녹음 파일 재생** — Play 버튼으로 마지막 녹음 파일 즉시 재생 — Play last recording with one click
- **녹음 폴더 관리** — Open Folder로 탐색기 열기, ... 버튼으로 폴더 위치 변경 — Open folder in Explorer, change recording location

### 설정 관리 / Settings Management

- **설정 저장/불러오기** — Controls > General 탭에서 전체 설정을 .dpbackup 파일로 저장/불러오기 — Save/load full settings as .dpbackup files in Controls > General tab
- **플러그인 검색/정렬** — 스캐너에서 이름/벤더/포맷으로 검색 및 정렬 — Search and sort plugins by name, vendor, or format in scanner
- **새 버전 알림** — 새 릴리즈가 있으면 하단 상태바에 주황색 "NEW" 표시 — Orange "NEW" indicator on status bar when a newer release is available

### 진단 / Diagnostics

- **오류 알림** — 상태 바에 비침습적 알림 표시 (빨강=오류, 주황=경고, 보라=정보). 3-8초 자동 페이드. — Non-intrusive status bar notifications (red=error, orange=warning, purple=info). Auto-fades after 3-8 seconds.
- **로그 탭** — 실시간 로그 뷰어 (4번째 탭). 타임스탬프 + 고정폭 폰트. Export Log / Clear Log 지원. — Real-time log viewer (4th tab). Timestamped entries in monospaced font. Export Log / Clear Log.
- **유지보수 도구** — 로그 탭에서 플러그인 캐시 삭제, 프리셋 전체 삭제, 설정 초기화 (확인 대화상자 포함) — Maintenance tools in Log tab: Clear Plugin Cache, Clear All Presets, Reset Settings (with confirmation dialogs)

### UI

- **시스템 트레이** — X 버튼으로 트레이 최소화, 더블클릭 복원, 시작 프로그램 등록. 트레이 툴팁에 현재 상태 표시. — Close minimizes to tray, double-click to restore, Start with Windows toggle. Tray tooltip shows current state.
- **탭 설정** — Audio / Monitor / Controls / Log (Hotkeys, MIDI, Stream Deck, General) — Tabbed settings panel
- **Panic Mute** — 전체 출력 즉시 뮤트, 해제 시 이전 상태 복원. 패닉 뮤트 중에는 OUT/MON/VST 및 외부 제어 잠금 — Mute all outputs instantly, restores previous state on unmute. Locks OUT/MON/VST buttons and external controls during panic mute
- **Output / Monitor Mute** — 개별 출력 뮤트 (UI 인디케이터 + 클릭 제어) — Independent output/monitor mute with clickable status indicators
- **MIDI 플러그인 파라미터 매핑** — MIDI CC로 VST 플러그인 파라미터 직접 제어 (Learn 모드) — Map MIDI CC to VST plugin parameters with Learn mode
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

### OBS에서 Receiver VST2로 사용하기 (가상 케이블 불필요) / Using Receiver VST2 with OBS (No Virtual Cable)

OBS에서는 Receiver VST2 플러그인을 사용하면 가상 케이블 없이 더 간단하게 설정할 수 있습니다.

If you use OBS, the Receiver VST2 plugin offers a simpler setup without any virtual cable.

1. `DirectPipe Receiver.dll`을 VST 폴더에 복사 (예: `C:\Program Files\VSTPlugins\`) — Copy DLL to VST folder
2. DirectPipe에서 **VST** 버튼 클릭 (IPC 출력 켜기) — Enable IPC output in DirectPipe
3. OBS → 오디오 소스 → 필터 → VST 2.x 플러그인 → **DirectPipe Receiver** 선택 — Add VST filter in OBS

```
USB Mic → DirectPipe (VST Chain: 노이즈 제거, EQ, 컴프 ...)
      ↓ IPC (공유 메모리, 가상 케이블 불필요)
OBS [DirectPipe Receiver VST 필터] → 방송/녹화
```

> **Tip**: 가상 케이블과 Receiver VST를 **동시에** 사용할 수도 있습니다. Discord는 VB-Cable로, OBS는 Receiver VST로 각각 보내면 됩니다. — You can use both methods simultaneously: VB-Cable for Discord, Receiver VST for OBS.

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
                            HotkeyHandler, MidiHandler, StateBroadcaster,
                            DirectPipeLogger
    IPC/                    SharedMemWriter
    UI/                     AudioSettings, OutputPanel, ControlSettingsPanel,
                            PluginChainEditor, PluginScanner, PresetManager,
                            LevelMeter, LogPanel, NotificationBar,
                            DirectPipeLookAndFeel
core/                     IPC library (RingBuffer, SharedMemory, Protocol)
plugins/receiver/         Receiver VST2 plugin (for OBS)
com.directpipe.directpipe.sdPlugin/  Stream Deck plugin (Node.js, SDK v3)
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
<summary><b>X 버튼을 눌렀는데 프로그램이 꺼지지 않아요 / Clicking X doesn't close the app</b></summary>

DirectPipe는 **시스템 트레이**에서 상주하는 앱입니다. X 버튼(닫기)을 누르면 **종료되는 것이 아니라 트레이로 최소화**됩니다. 오디오 처리는 백그라운드에서 계속 동작합니다.

- **트레이 아이콘 더블클릭** → 창 복원
- **트레이 아이콘 우클릭** → 메뉴에서 **"Quit DirectPipe"** 선택 → 완전 종료

이 동작은 의도된 설계입니다 — Discord나 OBS 등에 실시간 오디오를 계속 보내면서 창을 닫아도 처리가 중단되지 않도록 합니다.

---

DirectPipe is a **system tray** resident app. Clicking the X (close) button **minimizes it to the tray** instead of quitting. Audio processing continues running in the background.

- **Double-click the tray icon** → restore the window
- **Right-click the tray icon** → select **"Quit DirectPipe"** → fully exit

This is by design — it ensures audio processing keeps running for Discord, OBS, etc. even when the window is closed.
</details>

<details>
<summary><b>트레이 아이콘이 안 보여요 / 트레이에 고정하는 방법 / Tray icon not visible / How to pin to tray</b></summary>

Windows는 기본적으로 트레이 아이콘을 숨김 영역(▲ 화살표 안)에 넣습니다. DirectPipe 아이콘을 항상 보이게 하려면 **트레이에 고정**하세요.

**Windows 11:**
1. **설정** → **개인 설정** → **작업 표시줄** → **기타 시스템 트레이 아이콘** 클릭
2. 목록에서 **DirectPipe**를 찾아 **켬** 으로 변경
3. 또는: 숨김 영역(▲)에서 DirectPipe 아이콘을 **작업 표시줄로 드래그**

**Windows 10:**
1. **설정** → **개인 설정** → **작업 표시줄** → **알림 영역** → **작업 표시줄에 표시할 아이콘 선택** 클릭
2. 목록에서 **DirectPipe**를 찾아 **켬** 으로 변경
3. 또는: 숨김 영역(▲)에서 DirectPipe 아이콘을 **작업 표시줄로 드래그**

---

Windows hides tray icons in the overflow area (▲ arrow) by default. To keep the DirectPipe icon always visible, **pin it to the taskbar tray**.

**Windows 11:**
1. **Settings** → **Personalization** → **Taskbar** → click **Other system tray icons**
2. Find **DirectPipe** and toggle it **On**
3. Or: drag the DirectPipe icon from the overflow area (▲) onto the **taskbar**

**Windows 10:**
1. **Settings** → **Personalization** → **Taskbar** → **Notification area** → click **Select which icons appear on the taskbar**
2. Find **DirectPipe** and toggle it **On**
3. Or: drag the DirectPipe icon from the overflow area (▲) onto the **taskbar**
</details>

<details>
<summary><b>처음 실행할 때 빨간색 "Windows의 PC 보호" 경고가 떠요 / SmartScreen warning on first run</b></summary>

정상입니다! DirectPipe는 오픈소스라 코드 서명 인증서가 없어서 나타나는 경고입니다. 악성 소프트웨어가 아닙니다.

1. **"추가 정보"** 텍스트를 클릭하세요
2. 아래에 나타나는 **"실행"** 버튼을 누르세요
3. 한 번만 하면 이후로는 경고 없이 실행됩니다

This is normal! DirectPipe is open-source and does not have a code signing certificate, so Windows SmartScreen shows a warning. It is not malware.

1. Click the **"More info"** text
2. Click the **"Run anyway"** button that appears
3. You only need to do this once — the warning won't appear again
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

---

VST plugins are software that add audio effects to your signal — noise removal, EQ, compressor, reverb, and more.

**Free recommended plugins:**
- [ReaPlugs](https://www.reaper.fm/reaplugs/) — EQ, compressor, gate, and more essentials (free)
- [RNNoise](https://github.com/werman/noise-suppression-for-voice) — AI-powered real-time noise removal (free)
- [TDR Nova](https://www.tokyodawn.net/tdr-nova/) — High-quality dynamic EQ (free)
- [OrilRiver](https://www.kvraudio.com/product/orilriver-by-denis-tihanov) — Reverb (free)

After installing plugins, click **"Scan..."** in DirectPipe to scan your plugin folders. They will appear in the plugin list.
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
4. 자기 목소리를 확인하려면 **Monitor** 탭에서 헤드폰 장치를 설정하세요

```
내 USB 마이크 → DirectPipe (노이즈 제거, EQ 등) → VB-Cable Input
                                                         ↓
                                       Discord/Zoom/OBS ← VB-Cable Output (마이크로 인식)
```

---

You'll need [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) (free).

**Setup steps:**
1. Install VB-Audio Virtual Cable and reboot your PC
2. In DirectPipe **Audio** tab:
   - **Input** → Select your USB microphone
   - **Output** → Select `CABLE Input (VB-Audio Virtual Cable)`
3. In Discord / Zoom / OBS voice settings:
   - **Microphone** → Select `CABLE Output (VB-Audio Virtual Cable)`
4. To hear yourself, configure your headphone device in the **Monitor** tab

```
USB Mic → DirectPipe (Noise removal, EQ, etc.) → VB-Cable Input
                                                        ↓
                                      Discord/Zoom/OBS ← VB-Cable Output (recognized as mic)
```
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

---

**Troubleshooting steps:**
1. **Audio tab** → Make sure the correct Input device is selected
2. Check if the left **INPUT level meter** is moving → if it moves, mic input is working
3. If the level meter doesn't move:
   - Windows Settings → Privacy → Microphone — make sure app access is allowed
   - If another app (e.g., Discord) is using the mic in exclusive mode, disable exclusive mode
4. Check that the **OUT** button is green (red means muted → click to unmute)
5. If **PANIC MUTE** is active, click it again to deactivate
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

---

**Adjust the Buffer Size:**
- Audio tab → **Buffer Size**: lower values reduce latency but increase CPU load
  - `256 samples` (~5ms @ 48kHz) — good starting point
  - `128 samples` (~2.7ms) — low latency, recommended for higher-end PCs
  - `512 samples` (~10ms) — stable, recommended for lower-end PCs

**Still crackling?**
- Reduce the number of plugins, or Bypass CPU-heavy ones
- Switch from WASAPI to **ASIO** driver (allows even lower latency)
- Check the **CPU %** in the bottom status bar — above 60% indicates overload
</details>

<details>
<summary><b>ASIO가 뭔가요? 꼭 필요한가요? / What is ASIO? Do I need it?</b></summary>

**ASIO**는 저지연 오디오 드라이버입니다. 대부분의 USB 마이크는 **WASAPI** 모드로 충분히 잘 동작하므로 반드시 필요하지는 않습니다.

**ASIO** is a low-latency audio driver protocol. Most USB microphones work perfectly fine with **WASAPI** mode, so ASIO is not required.

| | WASAPI Shared | ASIO |
|---|---|---|
| 지연 / Latency | 보통 / Normal (5–15ms) | 매우 낮음 / Very low (2–5ms) |
| 설치 / Setup | 별도 설치 불필요 / No extra install | 오디오 인터페이스 드라이버 필요 / Requires audio interface driver |
| 다른 앱 동시 사용 / Shared access | 가능 (비독점) / Yes (non-exclusive) | 장치에 따라 다름 / Depends on device |
| 추천 대상 / Best for | 일반 사용자 / Most users | 전문가, 실시간 모니터링 / Pros, real-time monitoring |
</details>

<details>
<summary><b>플러그인 스캔 중 프로그램이 멈춘 것 같아요 / Plugin scan seems stuck</b></summary>

플러그인 스캔은 **별도 프로세스**에서 실행되므로 DirectPipe가 멈추거나 크래시하지 않습니다. 일부 플러그인은 스캔에 시간이 오래 걸릴 수 있습니다 (최대 5분).

- 크래시를 유발하는 플러그인은 자동으로 **블랙리스트**에 등록되어 다음 스캔에서 건너뜁니다
- 스캔 로그: `%AppData%/DirectPipe/scanner-log.txt`에서 확인 가능

---

Plugin scanning runs in a **separate process**, so DirectPipe itself will never freeze or crash. Some plugins may take a while to scan (up to 5 minutes).

- Plugins that cause crashes are automatically **blacklisted** and skipped in future scans
- Scan log: check `%AppData%/DirectPipe/scanner-log.txt`
</details>

<details>
<summary><b>프리셋은 어떻게 사용하나요? / How to use presets?</b></summary>

**Quick Preset Slots (A–E):**
- 현재 플러그인 체인과 설정을 **A–E** 슬롯에 저장할 수 있습니다
- **Save Preset** → 현재 상태를 선택된 슬롯에 저장
- 슬롯 버튼 **(A/B/C/D/E)** 클릭 → 즉시 전환
- 같은 플러그인이면 파라미터만 바꿔서 **즉시 전환**, 다른 플러그인이면 **비동기 로딩**

예: 게임 중엔 **A** (노이즈 제거만), 노래방에선 **B** (리버브 + 컴프레서)

---

**Quick Preset Slots (A–E):**
- Save your current plugin chain and settings to slots **A through E**
- **Save Preset** → saves the current state to the selected slot
- Click a slot button **(A/B/C/D/E)** → switch instantly
- If the plugins are the same, only parameters change (**instant switch**); different plugins use **async loading**

Example: Slot **A** for gaming (noise removal only), Slot **B** for karaoke (reverb + compressor)
</details>

<details>
<summary><b>Monitor 출력은 뭔가요? / What is Monitor output?</b></summary>

**Monitor**는 자기 목소리를 헤드폰으로 실시간 확인하는 기능입니다. VST 이펙트가 적용된 자신의 목소리를 들을 수 있습니다.

- **Monitor 탭**에서 헤드폰이 연결된 오디오 장치를 선택
- Main Output과는 별도의 WASAPI 장치를 사용하므로 **독립적으로 동작**
- **MON** 버튼으로 켜기/끄기

---

**Monitor** lets you hear your own processed voice through headphones in real-time, with all VST effects applied.

- Select your headphone device in the **Monitor tab**
- Uses a separate WASAPI device from the Main Output, so it **works independently**
- Toggle on/off with the **MON** button
</details>

<details>
<summary><b>컴퓨터 시작할 때 자동으로 실행되게 하려면? / How to auto-start with Windows?</b></summary>

두 가지 방법:
1. **시스템 트레이** 아이콘 우클릭 → **"Start with Windows"** 체크
2. **Controls** 탭 → **General** → **"Start with Windows"** 체크

활성화하면 Windows 시작 시 자동으로 트레이에서 실행됩니다. X 버튼으로 창을 닫아도 트레이에 남아서 계속 동작합니다.

---

Two ways to enable:
1. Right-click the **system tray** icon → check **"Start with Windows"**
2. **Controls** tab → **General** → check **"Start with Windows"**

Once enabled, DirectPipe launches automatically in the system tray when Windows starts. Closing the window (X button) minimizes it to the tray — it keeps running in the background.
</details>

<details>
<summary><b>Stream Deck 플러그인은 어디서 받나요? / Where to get the Stream Deck plugin?</b></summary>

**[Elgato Marketplace에서 무료 설치](https://marketplace.elgato.com/product/directpipe-29f7cbb8-cb90-425d-9dbc-b2158e7ea8b3)** — Stream Deck 앱에서 바로 설치됩니다.

**[Install free from Elgato Marketplace](https://marketplace.elgato.com/product/directpipe-29f7cbb8-cb90-425d-9dbc-b2158e7ea8b3)** — Installs directly into the Stream Deck app.

지원 액션: Bypass Toggle, Volume Control (SD+ 다이얼), Preset Switch, Monitor Toggle, Panic Mute, Recording Toggle, IPC Toggle

Supported actions: Bypass Toggle, Volume Control (SD+ dial), Preset Switch, Monitor Toggle, Panic Mute, Recording Toggle, IPC Toggle
</details>

<details>
<summary><b>Stream Deck 없이도 외부 제어가 되나요? / Can I control without a Stream Deck?</b></summary>

네! 다양한 방법으로 제어할 수 있습니다:

Yes! Multiple control methods are available:

| 방법 / Method | 예시 / Example | 적합한 용도 / Best for |
|---|---|---|
| **키보드 단축키 / Hotkeys** | Ctrl+Shift+1–9 bypass, F1–F5 프리셋 / presets | 가장 간편 / Simplest |
| **MIDI CC** | 미디 컨트롤러 노브/버튼 / MIDI controller knobs | 실시간 볼륨 조절 / Real-time volume |
| **HTTP API** | `curl http://localhost:8766/api/...` | 스크립트 자동화 / Script automation |
| **WebSocket** | ws://localhost:8765 | 커스텀 앱/봇 연동 / Custom app integration |

자세한 내용 / Details: [Control API](docs/CONTROL_API.md)
</details>

<details>
<summary><b>Receiver VST2 플러그인이 뭔가요? / What is the Receiver VST2 plugin?</b></summary>

**Receiver VST2**는 DirectPipe에서 처리한 마이크 오디오를 **OBS, DAW 등 VST2 플러그인을 지원하는 앱**에서 직접 받을 수 있게 해주는 플러그인입니다.

보통 DirectPipe에서 처리된 오디오를 OBS로 보내려면 **VB-Audio Virtual Cable** 같은 가상 오디오 케이블이 필요합니다. Receiver VST2를 사용하면 **가상 케이블 없이** 공유 메모리(IPC)를 통해 오디오를 직접 받을 수 있어 설정이 더 간단하고 지연도 적습니다.

---

**Receiver VST2** is a plugin that lets **OBS, DAWs, and other apps that support VST2 plugins** receive DirectPipe's processed mic audio directly.

Normally, to route DirectPipe's processed audio to OBS, you need a **virtual audio cable** like VB-Audio Virtual Cable. With the Receiver VST2, you can receive audio directly via shared memory (IPC) — **no virtual cable needed**, simpler setup, lower latency.

**Receiver VST2 vs. Virtual Cable 비교 / Comparison:**

| | Receiver VST2 | VB-Audio Virtual Cable |
|---|---|---|
| 설치 / Install | DirectPipe Receiver.dll을 VST 폴더에 복사 / Copy DLL to VST folder | VB-Cable 설치 + PC 재부팅 / Install VB-Cable + reboot |
| 설정 / Setup | OBS에서 VST 필터 추가만 하면 됨 / Just add VST filter in OBS | DirectPipe Output + OBS Input 양쪽 설정 필요 / Configure both sides |
| 지연 / Latency | ~5–85ms (프리셋 선택 가능) / Configurable | 드라이버에 의존 / Depends on driver |
| 추가 소프트웨어 / Extra software | 불필요 / None needed | VB-Cable 필요 / VB-Cable required |
| 호환성 / Compatibility | VST2 지원 앱만 / VST2-capable apps only | 모든 앱 / Any app |
| 추천 / Recommended for | OBS 사용자 / OBS users | Discord, Zoom 등 일반 앱 / General apps |
</details>

<details>
<summary><b>OBS에서 Receiver VST2는 어떻게 사용하나요? / How to use Receiver VST2 in OBS?</b></summary>

**OBS에서 Receiver VST2 설정하기:**

1. **DirectPipe Receiver.dll**을 VST 플러그인 폴더에 복사
   - 기본 위치: `C:\Program Files\VSTPlugins\` 또는 `C:\Program Files\Steinberg\VSTPlugins\`
   - OBS가 인식하는 VST 폴더에 넣어야 합니다

2. **DirectPipe**에서 IPC 출력 켜기
   - DirectPipe 실행 → 하단의 **VST** 버튼 클릭 (초록색으로 변경)
   - 또는: **Controls** > **General** 탭에서 **"IPC Output"** 체크
   - 또는: 키보드 **Ctrl+Shift+I**

3. **OBS** 설정
   - OBS 실행 → **소스** 영역에서 오디오 소스(마이크 등) 선택 → **필터** 클릭
   - **"+" 버튼** → **"VST 2.x 플러그인"** 선택
   - 플러그인 목록에서 **"DirectPipe Receiver"** 선택
   - **"플러그인 인터페이스 열기"** 클릭하면 연결 상태와 버퍼 설정 확인 가능

4. **연결 확인**
   - Receiver 플러그인 UI에서 **"Connected"** (초록색 원)이 표시되면 정상
   - "Disconnected" (빨간색)이면 DirectPipe가 실행 중이고 IPC가 켜져 있는지 확인

```
DirectPipe (마이크 + VST 이펙트)
      ↓ IPC (공유 메모리)
OBS [DirectPipe Receiver VST 필터]
      ↓
방송 / 녹화
```

---

**Setting up Receiver VST2 in OBS:**

1. **Copy DirectPipe Receiver.dll** to your VST plugin folder
   - Default: `C:\Program Files\VSTPlugins\` or `C:\Program Files\Steinberg\VSTPlugins\`
   - Must be in a folder that OBS scans for VST plugins

2. **Enable IPC output in DirectPipe**
   - Run DirectPipe → click the **VST** button at the bottom (turns green)
   - Or: **Controls** > **General** tab → check **"IPC Output"**
   - Or: press **Ctrl+Shift+I**

3. **Configure OBS**
   - Open OBS → select an audio source (e.g., mic) → click **Filters**
   - Click **"+"** → select **"VST 2.x Plug-in"**
   - Choose **"DirectPipe Receiver"** from the plugin list
   - Click **"Open Plug-in Interface"** to verify connection and adjust buffer settings

4. **Verify connection**
   - In the Receiver plugin UI, **"Connected"** with a green circle = working
   - If "Disconnected" (red), check that DirectPipe is running and IPC is enabled

```
DirectPipe (Mic + VST Effects)
      ↓ IPC (Shared Memory)
OBS [DirectPipe Receiver VST Filter]
      ↓
Stream / Recording
```
</details>

<details>
<summary><b>Receiver VST2에서 끊김/지연이 있어요 / Audio crackling or latency with Receiver VST2</b></summary>

**버퍼 크기를 조정하세요:**

Receiver VST의 플러그인 UI를 열면 **Buffer** 드롭다운에서 버퍼 크기를 선택할 수 있습니다.

| 프리셋 | 지연 | 권장 상황 |
|---|---|---|
| **Ultra Low** | ~5ms | 실시간 모니터링이 중요할 때 (끊김 가능성 있음) |
| **Low** (기본) | ~10ms | 대부분의 상황에 적합 |
| **Medium** | ~21ms | 안정적인 연결 |
| **High** | ~42ms | CPU 사용량이 높을 때 |
| **Safe** | ~85ms | 끊김이 자주 발생할 때 |

**여전히 끊긴다면:**
- 한 단계 높은 버퍼 프리셋을 선택하세요
- DirectPipe와 OBS의 **샘플레이트를 동일하게** 맞추세요 (예: 둘 다 48000Hz)
- CPU 사용량을 확인하세요 — DirectPipe 하단 상태 바에서 확인 가능

---

**Adjust the buffer size:**

Open the Receiver VST plugin interface and select a buffer size from the **Buffer** dropdown.

| Preset | Latency | Recommended for |
|---|---|---|
| **Ultra Low** | ~5ms | When real-time monitoring matters (risk of dropouts) |
| **Low** (default) | ~10ms | Most situations |
| **Medium** | ~21ms | Stable connection |
| **High** | ~42ms | When CPU load is high |
| **Safe** | ~85ms | If you experience frequent dropouts |

**Still getting dropouts?**
- Select a higher buffer preset
- Make sure DirectPipe and OBS use the **same sample rate** (e.g., both at 48000Hz)
- Check CPU usage — visible in DirectPipe's bottom status bar
</details>

<details>
<summary><b>Receiver VST2와 가상 케이블 중 뭘 써야 하나요? / Should I use Receiver VST2 or Virtual Cable?</b></summary>

**상황에 따라 다릅니다:**

**Receiver VST2를 추천하는 경우:**
- **OBS**에서 DirectPipe 오디오를 사용하는 경우 (OBS는 VST2 지원)
- 가상 오디오 케이블을 설치하고 싶지 않은 경우
- 설정을 최소화하고 싶은 경우

**VB-Audio Virtual Cable을 추천하는 경우:**
- **Discord, Zoom, Google Meet** 등 VST 플러그인을 지원하지 않는 앱에서 사용하는 경우
- 여러 앱에서 동시에 DirectPipe 오디오를 사용해야 하는 경우
- OBS 이외의 앱에서도 가상 마이크가 필요한 경우

**둘 다 동시에 사용 가능합니다!**
- DirectPipe Output → VB-Cable (Discord/Zoom용)
- DirectPipe IPC → Receiver VST2 (OBS용)
- 이렇게 하면 Discord에는 VB-Cable로, OBS에는 Receiver VST로 동시에 오디오를 보낼 수 있습니다

---

**Depends on your use case:**

**Use Receiver VST2 when:**
- Using DirectPipe audio in **OBS** (OBS supports VST2)
- Don't want to install virtual audio cable software
- Want minimal setup

**Use VB-Audio Virtual Cable when:**
- Using apps that **don't support VST plugins** (Discord, Zoom, Google Meet, etc.)
- Need to use DirectPipe audio in multiple apps simultaneously
- Need a virtual microphone for non-OBS apps

**You can use both at the same time!**
- DirectPipe Output → VB-Cable (for Discord/Zoom)
- DirectPipe IPC → Receiver VST2 (for OBS)
- This lets you send audio to Discord via VB-Cable and to OBS via Receiver VST simultaneously
</details>

<details>
<summary><b>IPC 출력(VST 버튼)은 뭔가요? 켜야 하나요? / What is IPC Output (VST button)? Do I need to enable it?</b></summary>

**IPC(Inter-Process Communication) 출력**은 DirectPipe에서 처리한 오디오를 **공유 메모리**를 통해 다른 프로세스(Receiver VST 플러그인)에 전달하는 기능입니다.

**기본값은 꺼져(OFF) 있습니다.** VB-Audio Virtual Cable만 사용하는 경우에는 켤 필요가 없습니다.

**켜야 하는 경우:**
- OBS에서 **DirectPipe Receiver VST2** 플러그인을 사용할 때
- 가상 케이블 없이 OBS로 오디오를 직접 보내고 싶을 때

**켜는 방법 (5가지):**
1. DirectPipe 메인 화면 하단의 **VST** 버튼 클릭
2. **Controls** > **General** 탭 → **"IPC Output"** 체크
3. 키보드 단축키 **Ctrl+Shift+I**
4. Stream Deck **IPC Toggle** 버튼
5. HTTP API: `curl http://localhost:8766/api/ipc/toggle`

켜면 VST 버튼이 **초록색**으로 변하고, 끄면 **빨간색**입니다.

---

**IPC (Inter-Process Communication) Output** sends DirectPipe's processed audio to another process (the Receiver VST plugin) via **shared memory**.

**It's OFF by default.** If you only use VB-Audio Virtual Cable, you don't need to enable it.

**Enable it when:**
- Using the **DirectPipe Receiver VST2** plugin in OBS
- Want to route audio to OBS without a virtual cable

**How to enable (5 ways):**
1. Click the **VST** button at the bottom of DirectPipe's main window
2. **Controls** > **General** tab → check **"IPC Output"**
3. Hotkey **Ctrl+Shift+I**
4. Stream Deck **IPC Toggle** button
5. HTTP API: `curl http://localhost:8766/api/ipc/toggle`

When enabled, the VST button turns **green**; when disabled, it's **red**.
</details>

## 후원 / Support

프로젝트가 도움이 되셨다면 커피 한 잔 사주세요! / If you find this project useful, consider buying me a coffee!

<a href="https://buymeacoffee.com/livetrack">
  <img src="https://img.shields.io/badge/Buy%20Me%20a%20Coffee-ffdd00?style=for-the-badge&logo=buy-me-a-coffee&logoColor=black" alt="Buy Me a Coffee">
</a>

## License

GPL v3 — [LICENSE](LICENSE)
