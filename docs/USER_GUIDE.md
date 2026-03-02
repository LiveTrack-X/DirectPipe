# DirectPipe User Guide / 사용자 가이드

> **Version 3.9.3** — [GitHub Releases](https://github.com/LiveTrack-X/DirectPipe/releases)

## DirectPipe란? / What is DirectPipe?

DirectPipe는 Windows용 실시간 VST2/VST3 호스트입니다. USB 마이크 입력에 노이즈 제거, EQ, 컴프레서 등 VST 플러그인을 걸어 실시간으로 처리한 뒤, Discord·Zoom·OBS 등 다른 앱에서 사용할 수 있도록 출력합니다. 시스템 트레이에 상주하며, 키보드 단축키·MIDI·Stream Deck·HTTP API로 원격 제어할 수 있습니다.

DirectPipe is a real-time VST2/VST3 host for Windows. It processes your USB microphone input through VST plugins (noise removal, EQ, compressor, etc.) and routes the output to other apps like Discord, Zoom, or OBS. It runs in the system tray and can be remotely controlled via hotkeys, MIDI, Stream Deck, or HTTP API.

```
USB 마이크 → DirectPipe (VST 플러그인 체인) → Main Output (VB-Cable → Discord/Zoom)
                                                 ├→ Monitor Output (헤드폰)
                                                 ├→ IPC Output (OBS Receiver VST)
                                                 └→ WAV Recording
```

---

## 빠른 시작 / Quick Start

### 1단계: 기본 설정 / Step 1: Basic Setup

1. **DirectPipe 실행** — 처음 실행 시 SmartScreen 경고가 나타나면 "추가 정보" → "실행" 클릭 (오픈소스라 코드 서명이 없어서 나타나는 정상적인 경고)
2. **Audio 탭**에서 드라이버 선택:
   - **Windows Audio (WASAPI)** — 대부분의 사용자에게 권장 (기본값)
   - **ASIO** — 저지연이 필요한 경우 (오디오 인터페이스 필요)
3. **Input** — USB 마이크 선택
4. **Output** — 가상 오디오 케이블 선택 (예: `CABLE Input (VB-Audio Virtual Cable)`)

---

1. **Launch DirectPipe** — If SmartScreen warns, click "More info" → "Run anyway" (normal for unsigned open-source apps)
2. **Audio tab** — Select driver:
   - **Windows Audio (WASAPI)** — Recommended for most users (default)
   - **ASIO** — For lowest latency (requires audio interface)
3. **Input** — Select your USB microphone
4. **Output** — Select a virtual audio cable (e.g., `CABLE Input (VB-Audio Virtual Cable)`)

### 2단계: 플러그인 추가 / Step 2: Add Plugins

1. **"Scan..."** 클릭 → 설치된 VST 플러그인 스캔 (처음 한 번만 필요)
2. **"+ Add Plugin"** 클릭 → 목록에서 원하는 플러그인 선택
3. 플러그인이 체인에 추가됨 — **Edit** 버튼으로 플러그인 GUI 열기

**추천 무료 플러그인:**
- [ReaPlugs](https://www.reaper.fm/reaplugs/) — EQ, 컴프레서, 게이트 (무료)
- [RNNoise](https://github.com/werman/noise-suppression-for-voice) — AI 노이즈 제거 (무료)
- [TDR Nova](https://www.tokyodawn.net/tdr-nova/) — 다이나믹 EQ (무료)

---

1. Click **"Scan..."** → Scans installed VST plugins (only needed once)
2. Click **"+ Add Plugin"** → Select a plugin from the list
3. Plugin is added to the chain — Click **Edit** to open the plugin GUI

### 3단계: 다른 앱에서 사용 / Step 3: Use in Other Apps

**Discord / Zoom / Google Meet:**
1. [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) 설치 (무료) → PC 재부팅
2. DirectPipe Audio 탭: Output → `CABLE Input (VB-Audio Virtual Cable)` 선택
3. Discord/Zoom 음성 설정: 마이크 → `CABLE Output (VB-Audio Virtual Cable)` 선택

```
USB 마이크 → DirectPipe → VB-Cable Input
                                ↓
              Discord/Zoom ← VB-Cable Output (마이크로 인식)
```

**OBS (Receiver VST2 — 가상 케이블 불필요):**
1. `DirectPipe Receiver.dll`을 아래 VST2 폴더 중 하나에 복사:
   - `C:\Program Files\VSTPlugins\` (권장)
   - `C:\Program Files\Common Files\VST2\`
   - `C:\Program Files\Steinberg\VstPlugins\`
2. DirectPipe에서 **VST** 버튼 클릭 (IPC 출력 켜기)
3. OBS → 오디오 소스 → 필터 → VST 2.x 플러그인 → **DirectPipe Receiver** 선택

```
USB 마이크 → DirectPipe (VST 이펙트)
      ↓ IPC (공유 메모리)
OBS [DirectPipe Receiver VST 필터] → 방송/녹화
```

> **Tip**: VB-Cable과 Receiver VST를 **동시에** 사용할 수 있습니다. Discord는 VB-Cable로, OBS는 Receiver VST로 각각 보내면 됩니다.

---

## UI 구성 / UI Layout

DirectPipe는 2컬럼 레이아웃입니다. / DirectPipe uses a two-column layout.

**왼쪽 컬럼 / Left Column:**
- INPUT 레벨 미터 (dB 로그 스케일)
- 입력 게인 슬라이더 (0.0x ~ 2.0x)
- VST 플러그인 체인 (드래그 앤 드롭 순서 변경)
- Add Plugin / Scan / Remove 버튼
- Quick Preset Slots (A ~ E)
- Save Preset / Load Preset 버튼
- 뮤트 인디케이터 (OUT / MON / VST)
- PANIC MUTE 버튼

**오른쪽 컬럼 / Right Column:**
- 4개 탭: **Audio** / **Output** / **Controls** / **Settings**
- OUTPUT 레벨 미터

**하단 상태 바 / Status Bar:**
- 레이턴시 (ms), CPU 사용률 (%), 오디오 포맷, 포터블 모드 표시
- 오류/경고/정보 알림 (자동 페이드)
- 버전 정보 + 업데이트 알림

---

## 오디오 설정 (Audio 탭) / Audio Settings

### 드라이버 타입 / Driver Type

| | Windows Audio (WASAPI) | ASIO |
|---|---|---|
| **지연** / Latency | 보통 5~15ms | 매우 낮음 2~5ms |
| **설치** / Setup | 별도 설치 불필요 | 오디오 인터페이스 드라이버 필요 |
| **마이크 공유** / Shared | 가능 (비독점) | 장치에 따라 다름 |
| **장치 선택** / Devices | 입출력 개별 선택 | 단일 장치 |
| **추천** / Best for | 대부분의 사용자 | 전문가, 실시간 모니터링 |

### 샘플레이트 & 버퍼 크기 / Sample Rate & Buffer Size

> **Audio 탭의 샘플레이트가 전체 시스템에 적용됩니다**: VST 체인, 모니터 출력, IPC(Receiver VST) 모두 이 값을 따릅니다. 모니터 출력은 메인 SR에 맞지 않으면 Error 상태가 됩니다.
>
> **Audio tab sample rate applies globally**: VST chain, monitor output, and IPC (Receiver VST) all follow this value. Monitor output will show Error if its device cannot match the main SR.

- **WASAPI**: 고정 목록 (44100, 48000 Hz / 64~2048 samples)
- **ASIO**: 장치가 지원하는 값만 표시. ASIO Control Panel에서 설정 가능

**버퍼 크기 가이드:**

| 버퍼 / Buffer | 지연 (@48kHz) | 권장 / Recommended |
|---|---|---|
| 128 samples | ~2.7ms | 고사양 PC, 최소 지연 |
| 256 samples | ~5.3ms | 일반적인 시작점 |
| 512 samples | ~10.7ms | 안정적, 저사양 PC |
| 1024 samples | ~21.3ms | CPU 부하 높을 때 |

### 채널 모드 / Channel Mode

- **Stereo** (기본값) — 입력 채널이 그대로 L/R로 통과 / Input channels pass through as-is
- **Mono** — 모든 입력 채널을 합산하여 모노로 만든 뒤 양쪽(L/R)으로 출력. 이후 VST 체인·메인 출력·모니터·IPC·녹음 모두 스테레오로 처리됨. 마이크 1개만 사용하면 볼륨 손실 없음. 2개 이상 입력 시 합산 (클리핑 발생 시 입력 게인으로 조절) / Sums all input channels to mono, then outputs to both L/R. Everything downstream (VST chain, main output, monitor, IPC, recording) processes as stereo. No volume loss for single-mic input. Multiple inputs are summed at full gain (adjust input gain if clipping occurs)

---

## VST 플러그인 체인 / VST Plugin Chain

VST2 (.dll)와 VST3 (.vst3) 플러그인을 직렬 체인으로 연결합니다. / Connects VST2 and VST3 plugins in a serial chain.

| 동작 / Action | 방법 / How |
|---|---|
| **추가** / Add | "+ Add Plugin" 클릭 → 목록에서 선택 |
| **제거** / Remove | 플러그인 행의 X 버튼 또는 하단 "Remove" |
| **순서 변경** / Reorder | 드래그 앤 드롭 |
| **Bypass** | 플러그인 행의 Bypass 토글 클릭 |
| **편집** / Edit | "Edit" 클릭 → 플러그인 네이티브 GUI 열기 |

### 자동 저장 / Auto-Save

플러그인 파라미터(EQ, 컴프레서 설정 등)는 자동으로 저장/복원됩니다:
- 설정 변경 후 1초 뒤 자동 저장
- 프리셋 슬롯 전환 시
- 앱 종료 시

Plugin parameters are auto-saved/restored on setting change (1-second debounce), preset slot switch, and app exit.

### VST 스캐너 / Plugin Scanner

- **별도 프로세스**에서 안전하게 스캔 — DirectPipe가 멈추지 않음
- 크래시를 유발하는 플러그인은 자동 **블랙리스트** 등록 (최대 10회 재시도)
- **검색/정렬**: 이름·벤더·포맷으로 실시간 필터링 및 컬럼 정렬
- 스캔 로그: `%AppData%/DirectPipe/scanner-log.txt`

Out-of-process scanner — crashes won't affect DirectPipe. Blacklists problematic plugins. Search and sort by name, vendor, or format.

---

## 퀵 프리셋 슬롯 (A~E) / Quick Preset Slots

5개의 VST 체인 구성을 빠르게 저장하고 전환할 수 있습니다. / Save and switch between 5 VST chain configurations instantly.

- **슬롯 클릭** → 비어있으면 현재 체인 저장, 차있으면 해당 슬롯 로드
- **같은 플러그인** → 파라미터만 변경되어 **즉시 전환**
- **다른 플러그인** → 백그라운드 **비동기 로딩** (UI 응답 유지)
- **활성 슬롯**: 보라색 / **사용 중**: 밝은색 / **빈 슬롯**: 어두운색
- **Save Preset / Load Preset** → `.dppreset` 파일로 내보내기/불러오기

슬롯은 **체인 데이터만** 저장합니다 (플러그인, 순서, Bypass, 파라미터). 오디오/출력 설정은 영향 없음.

**활용 예시:**
- **A**: 게임 (노이즈 제거만)
- **B**: 노래방 (리버브 + 컴프레서)
- **C**: 회의 (노이즈 제거 + EQ)
- **D**: 방송 (풀 체인: 게이트 + 디에서 + EQ + 컴프)

---

## Output 탭 / Output Tab

Output 탭은 3개 섹션으로 구성됩니다. / The Output tab has 3 sections.

### 모니터 출력 / Monitor Output

자기 목소리를 헤드폰으로 실시간 확인하는 기능입니다. VST 이펙트가 적용된 소리를 들을 수 있습니다.

Monitor lets you hear your own processed voice through headphones in real-time.

- **Device** — 모니터링용 출력 장치 선택 (헤드폰) / Select headphone device
- **Volume** — 모니터 볼륨 0~100% / Monitor volume
- **Enable** — 모니터 켜기/끄기 / Toggle on/off
- **Status** — 상태 표시 / Status indicator:
  - 🟢 **Active** — 정상 동작 / Working
  - 🔴 **Error** — 장치 오류 / Device error
  - ⚫ **No device** — 장치 미선택 / Not configured

> 모니터는 메인 드라이버와 **독립된 별도 WASAPI 장치**를 사용합니다. ASIO 모드에서도 정상 동작합니다.
>
> Monitor uses a **separate WASAPI device**, independent from the main driver. Works even with ASIO.

> **⚠️ 모니터 지연(레이턴시) 안내**: 모니터 출력은 별도 WASAPI 장치를 경유하기 때문에 메인 출력 대비 **~15-20ms 추가 지연**이 발생합니다. 이는 WASAPI Shared Mode 듀얼 디바이스 구조의 하한선이며, 소프트웨어로 줄일 수 없는 한계입니다. 실시간 모니터링 시 지연이 거슬린다면 다음을 권장합니다:
> - **ASIO 드라이버 사용** — 입력과 출력이 하나의 ASIO 디바이스에서 처리되어 별도 모니터 장치 없이 최소 지연으로 자기 목소리를 들을 수 있습니다
> - **하드웨어 다이렉트 모니터링** — 오디오 인터페이스 자체의 Direct Monitor 기능을 사용하면 컴퓨터를 거치지 않아 지연이 0입니다
>
> **⚠️ Monitor latency note**: Monitor output routes through a separate WASAPI device, adding **~15-20ms extra latency** compared to the main output. This is the inherent floor of the dual-device WASAPI Shared Mode architecture and cannot be reduced by software. If monitoring latency is noticeable, consider:
> - **ASIO driver** — Input and output share a single ASIO device, so you can hear yourself with minimal latency without a separate monitor device
> - **Hardware direct monitoring** — Use your audio interface's built-in Direct Monitor feature for zero latency (bypasses the computer entirely)

### VST Receiver (IPC) / VST 리시버

- **Enable VST Receiver Output** — IPC 출력 켜기/끄기 (공유 메모리로 Receiver VST2에 오디오 전송)
- 기본값은 **꺼짐(OFF)**. OBS에서 Receiver VST2를 사용할 때만 켜면 됩니다
- 메인 화면 **VST** 버튼, **Ctrl+Shift+I**, MIDI, Stream Deck, HTTP API로도 제어 가능
- **샘플레이트 불일치 경고**: DirectPipe의 SR과 OBS(호스트)의 SR이 다르면 Receiver VST GUI에 경고 메시지가 표시됩니다. SR이 다르면 피치/속도가 변합니다. OBS 오디오 설정의 샘플레이트를 DirectPipe Audio 탭과 동일하게 맞춰주세요.
- **SR mismatch warning**: If DirectPipe SR differs from the OBS (host) SR, a warning is shown in the Receiver VST GUI. Mismatched SR causes pitch/speed artifacts. Match OBS audio sample rate to the DirectPipe Audio tab setting.

### 녹음 / Recording

처리된 오디오(VST 체인 이후)를 WAV 파일로 녹음합니다. / Record post-chain audio to WAV.

| 기능 / Feature | 설명 / Description |
|---|---|
| **REC / STOP** | 녹음 시작/중지 |
| **경과 시간** | mm:ss 형식으로 표시 |
| **Play** | 마지막 녹음 파일을 기본 플레이어로 재생 |
| **Open Folder** | 녹음 폴더를 탐색기에서 열기 |
| **... (폴더 변경)** | 녹음 폴더 변경 (자동 저장) |

- **기본 폴더**: `Documents/DirectPipe Recordings`
- **파일명**: `DirectPipe_YYYYMMDD_HHMMSS.wav`
- **외부 제어**: Stream Deck (경과 시간 표시), HTTP API (`/api/recording/toggle`), WebSocket (`recording_toggle`)
- 녹음은 lock-free(실시간 안전) — 오디오 처리 성능에 영향 없음

---

## IPC 출력 & Receiver VST / IPC Output & Receiver VST

### OBS에서 Receiver VST2 사용하기 / Using Receiver VST2 in OBS

OBS에서는 Receiver VST2를 사용하면 **가상 케이블 없이** DirectPipe 오디오를 직접 받을 수 있습니다.

1. **`DirectPipe Receiver.dll`**을 아래 VST2 폴더 중 하나에 복사:
   - `C:\Program Files\VSTPlugins\` (권장 / Recommended)
   - `C:\Program Files\Common Files\VST2\`
   - `C:\Program Files\Steinberg\VstPlugins\`
2. **DirectPipe**에서 IPC 출력 켜기 (하단 **VST** 버튼 클릭 → 초록색)
3. **OBS** → 오디오 소스 → 필터 → "+" → "VST 2.x 플러그인" → **DirectPipe Receiver** 선택
4. "플러그인 인터페이스 열기" → **Connected** (초록색) 확인

### IPC 켜기/끄기 (5가지 방법) / Toggle IPC (5 ways)

1. 메인 화면 하단 **VST** 버튼
2. Output 탭 → **Enable VST Receiver Output** 체크
3. 키보드 **Ctrl+Shift+I**
4. Stream Deck **IPC Toggle** 버튼
5. HTTP API: `curl http://localhost:8766/api/ipc/toggle`

### Receiver 버퍼 크기 설정 / Receiver Buffer Size

Receiver VST GUI에서 Buffer 드롭다운으로 선택합니다. / Select in Receiver VST GUI.

| 프리셋 / Preset | 지연 / Latency | 권장 / Recommended |
|---|---|---|
| **Ultra Low** | ~5ms | 최소 지연 (끊김 가능성) |
| **Low** (기본) | ~10ms | 대부분의 상황 |
| **Medium** | ~21ms | 안정적 |
| **High** | ~42ms | CPU 부하 높을 때 |
| **Safe** | ~85ms | 끊김 자주 발생 시 |

> DirectPipe와 OBS의 **샘플레이트를 동일하게** 맞추세요 (예: 둘 다 48000Hz).

---

## 시스템 트레이 / System Tray

DirectPipe는 시스템 트레이에 상주합니다. X 버튼은 종료가 아닌 **트레이 최소화**입니다.

DirectPipe is a tray-resident app. The X button **minimizes to tray**, not quit.

| 동작 / Action | 방법 / How |
|---|---|
| **창 복원** / Restore | 트레이 아이콘 더블클릭 |
| **메뉴** | 트레이 아이콘 우클릭 → Show / Start with Windows / Quit |
| **완전 종료** / Quit | 우클릭 → "Quit DirectPipe" |

**트레이 아이콘 고정하기 (Windows 11):**
설정 → 개인 설정 → 작업 표시줄 → 기타 시스템 트레이 아이콘 → DirectPipe **켬**

**Pin tray icon (Windows 11):**
Settings → Personalization → Taskbar → Other system tray icons → DirectPipe **On**

### 시작 프로그램 / Start with Windows

트레이 메뉴 또는 Settings 탭에서 설정. Windows 시작 시 트레이에서 자동 실행됩니다. / Toggle via tray menu or Settings tab.

### 포터블 모드 / Portable Mode

`DirectPipe.exe` 옆에 `portable.flag` 파일을 만들면 설정이 `%AppData%` 대신 `./config/`에 저장됩니다. USB 메모리 등에서 휴대 사용 가능.

Place a `portable.flag` file next to `DirectPipe.exe` to store config in `./config/` instead of `%AppData%`.

---

## 뮤트 컨트롤 / Mute Controls

메인 화면 하단에 3개의 뮤트 인디케이터와 PANIC MUTE 버튼이 있습니다.

| 버튼 | 기능 / Function | 초록색 / Green | 빨간색 / Red |
|---|---|---|---|
| **OUT** | 메인 출력 뮤트 | 출력 정상 | 뮤트됨 |
| **MON** | 모니터 출력 | 모니터 켜짐 | 모니터 꺼짐 |
| **VST** | IPC 출력 (Receiver) | IPC 켜짐 | IPC 꺼짐 |

### PANIC MUTE / 패닉 뮤트

**전체 출력을 즉시 뮤트**합니다. 갑자기 큰 소리가 나거나 피드백이 발생했을 때 사용하세요.

- 패닉 뮤트 중에는 OUT/MON/VST 버튼과 **모든 외부 제어가 잠금**됩니다
- 해제하면 이전 상태(모니터 켜짐/꺼짐, 출력 뮤트 등)가 **자동 복원**됩니다
- 단축키: **Ctrl+Shift+M**

Immediately mutes all outputs. During panic mute, all buttons and external controls are locked — only PanicMute/unmute can change state. Previous state is restored on unmute.

---

## 외부 제어 / External Control

DirectPipe는 최소화 상태에서도 다양한 방법으로 제어할 수 있습니다.

### 키보드 단축키 / Keyboard Shortcuts

모든 단축키는 **Controls > Hotkeys** 탭에서 변경할 수 있습니다. / All shortcuts customizable in Controls > Hotkeys tab.

| 단축키 / Shortcut | 동작 / Action |
|---|---|
| Ctrl+Shift+1~9 | 플러그인 1-9 Bypass 토글 |
| Ctrl+Shift+0 | 마스터 Bypass (전체 체인) |
| Ctrl+Shift+M | 패닉 뮤트 |
| Ctrl+Shift+N | 입력 뮤트 토글 |
| Ctrl+Shift+O | 출력 뮤트 토글 |
| Ctrl+Shift+H | 모니터 토글 |
| Ctrl+Shift+I | IPC 출력 토글 |
| Ctrl+Shift+F1~F5 | 프리셋 슬롯 A~E |

### MIDI 제어 / MIDI Control

1. **Controls > MIDI** 탭에서 MIDI 장치 선택
2. 매핑할 동작 옆의 **[Learn]** 클릭
3. MIDI 컨트롤러의 노브/버튼/슬라이더 조작
4. 매핑 자동 저장

**매핑 타입**: Toggle, Momentary, Continuous, NoteOnOff
**핫플러그**: MIDI 장치 연결 후 **[Rescan]** 클릭

#### MIDI 플러그인 파라미터 매핑 / Plugin Parameter Mapping

MIDI CC로 VST 플러그인의 개별 파라미터를 직접 제어할 수 있습니다.

1. MIDI 탭에서 **[+ Add Param]** 클릭
2. 팝업에서 **플러그인** 선택
3. 제어할 **파라미터** 선택
4. MIDI 컨트롤러 **노브/슬라이더** 조작으로 할당

### Stream Deck

Elgato Marketplace에서 무료 설치: **[DirectPipe Stream Deck Plugin](https://marketplace.elgato.com/product/directpipe-29f7cbb8-cb90-425d-9dbc-b2158e7ea8b3)**

7가지 액션: Bypass Toggle, Volume Control (SD+ 다이얼), Preset Switch, Monitor Toggle, Panic Mute, Recording Toggle, IPC Toggle

자세한 내용: [Stream Deck Guide](STREAMDECK_GUIDE.md)

### HTTP REST API

포트 8766에서 GET 요청으로 제어합니다. / Control via GET requests on port 8766.

```bash
# Bypass 토글
curl http://localhost:8766/api/bypass/0/toggle

# 패닉 뮤트
curl http://localhost:8766/api/mute/panic

# 모니터 볼륨 50%
curl http://localhost:8766/api/volume/monitor/0.5

# 녹음 토글
curl http://localhost:8766/api/recording/toggle

# IPC 토글
curl http://localhost:8766/api/ipc/toggle

# 프리셋 슬롯 A 로드
curl http://localhost:8766/api/preset/0

# 현재 상태 조회
curl http://localhost:8766/api/state
```

전체 엔드포인트: [Control API Reference](CONTROL_API.md)

### WebSocket

포트 8765에서 양방향 실시간 통신. 상태 변경 시 자동 푸시. / Bidirectional real-time on port 8765 with auto state push.

---

## Settings 탭 / Settings Tab

우측 패널의 4번째 탭. / The 4th tab in the right panel.

### 앱 설정 / Application

- **Start with Windows** — Windows 시작 시 트레이에서 자동 실행

### 설정 저장/불러오기 / Settings Export/Import

전체 설정(오디오, VST 체인, 볼륨, 프리셋, 제어 매핑)을 `.dpbackup` 파일로 내보내기/가져오기. PC를 옮기거나 설정을 백업할 때 유용합니다.

Export/import full settings as `.dpbackup` files. Useful for backups or migrating to a new PC.

- **Save Settings** → `.dpbackup` 파일로 저장
- **Load Settings** → `.dpbackup` 파일에서 복원

### 로그 뷰어 / Log Viewer

앱의 모든 이벤트(오디오 엔진, 플러그인, WebSocket, HTTP 등)를 실시간으로 확인합니다.

- 타임스탬프 + 고정폭 폰트
- **카테고리 태그**: `[APP]` `[AUDIO]` `[VST]` `[PRESET]` `[ACTION]` `[HOTKEY]` `[MIDI]` `[WS]` `[HTTP]` `[MONITOR]` `[IPC]` `[REC]` `[CONTROL]` — 서브시스템별 구분 / Category tags per subsystem
- **Export Log** — `.txt` 파일로 저장 (문제 신고 시 유용)
- **Clear Log** — 로그 화면 지우기

### 유지보수 / Maintenance

모든 작업은 확인 대화상자가 먼저 표시됩니다. / All actions show a confirmation dialog.

| 기능 / Function | 설명 / Description |
|---|---|
| **Clear Plugin Cache** | 스캔된 플러그인 목록 삭제 (다음 Scan 시 재스캔) |
| **Clear All Presets** | 퀵 슬롯 A~E 및 저장된 프리셋 전체 삭제 |
| **Reset Settings** | 공장 초기화 (오디오 설정, 단축키, MIDI 매핑 삭제) |

---

## 인앱 업데이트 / In-App Update

DirectPipe는 실행 시 자동으로 GitHub에서 최신 버전을 확인합니다. / Checks for updates on startup.

1. **새 버전 발견** → 하단 credit 라벨에 **"NEW vX.Y.Z"** 주황색 표시
2. **라벨 클릭** → 업데이트 다이얼로그:
   - **Update Now** — GitHub에서 자동 다운로드 → exe 교체 → 앱 재시작
   - **View on GitHub** — 브라우저에서 릴리즈 페이지 열기
   - **Later** — 닫기 (다음 실행 시 다시 알림)
3. **업데이트 완료** → 재시작 후 **"Updated to vX.Y.Z successfully!"** 알림 (보라색)

> **인터넷 미연결 시**: 오류 없이 기존 버전이 정상 동작합니다.
>
> **Offline**: No error, app works normally with current version.

---

## 오류 알림 / Error Notifications

상태 바에 비침습적 알림이 자동으로 표시됩니다. / Non-intrusive notifications in the status bar.

| 색상 / Color | 의미 / Meaning | 예시 / Example |
|---|---|---|
| 🔴 **빨간색** | 오류 | 오디오 장치 실패, 플러그인 로드 실패 |
| 🟠 **주황색** | 경고 | 장치 변경, 설정 문제 |
| 🟣 **보라색** | 정보 | 업데이트 완료, 설정 저장 |

심각도에 따라 3~8초 후 자동 사라짐. / Auto-fades after 3-8 seconds.

---

## 문제 해결 / Troubleshooting

### 소리가 안 나와요 / No audio

1. **Audio 탭** → Input 장치가 올바른지 확인
2. 왼쪽 **INPUT 미터** 확인 — 움직이면 마이크 입력은 정상
3. 미터가 안 움직이면:
   - Windows 설정 → 개인 정보 → 마이크 접근 허용 확인
   - 다른 앱의 독점 모드 해제
4. **OUT** 버튼이 초록색인지 확인 (빨간색 = 뮤트)
5. **PANIC MUTE** 활성화 여부 확인

### 소리가 끊기거나 지연이 커요 / Crackling or high latency

1. Audio 탭 → **Buffer Size** 올리기 (256 → 512)
2. CPU를 많이 쓰는 플러그인 Bypass 처리
3. WASAPI 대신 **ASIO** 드라이버 사용
4. 하단 상태 바 **CPU %** 확인 — 60% 이상이면 과부하

### 모니터 출력이 안 돼요 / No monitor output

1. Output 탭에서 모니터 **장치 선택** 확인
2. **Enable** 토글이 켜져 있는지 확인
3. **MON** 버튼이 초록색인지 확인
4. 모니터는 별도 WASAPI 장치 — ASIO 모드에서도 동작

### 플러그인 스캔이 오래 걸려요 / Plugin scan takes long

- 별도 프로세스에서 실행되므로 DirectPipe는 멈추지 않음
- 일부 플러그인은 최대 5분까지 걸릴 수 있음
- 크래시 유발 플러그인은 자동 블랙리스트
- 로그: `%AppData%/DirectPipe/scanner-log.txt`

### Receiver VST2에서 끊김 / Receiver audio crackling

1. Receiver VST GUI에서 **Buffer** 한 단계 올리기
2. DirectPipe와 OBS **샘플레이트 동일하게** 맞추기 (48000Hz 권장)
3. CPU 사용량 확인

### 오류가 발생했는데 팝업이 없어요 / No error popup

- 하단 **상태 바** 확인 — 알림이 잠시 표시됨 (빨강/주황/보라)
- **Settings 탭** → 로그 뷰어에서 전체 이벤트 기록 확인
- **Export Log**로 로그 저장 후 문제 신고

---

## 설정 파일 위치 / Config File Locations

| 파일 / File | 경로 / Path |
|---|---|
| 기본 설정 | `%AppData%/DirectPipe/settings.json` |
| 프리셋 슬롯 | `%AppData%/DirectPipe/presets/` |
| 플러그인 캐시 | `%AppData%/DirectPipe/plugin-list.xml` |
| 스캐너 로그 | `%AppData%/DirectPipe/scanner-log.txt` |
| 녹음 폴더 | `Documents/DirectPipe Recordings/` |
| 포터블 모드 | `./config/` (exe 옆 `portable.flag` 필요) |

---

## 관련 문서 / Related Documentation

- [Architecture](ARCHITECTURE.md) — 시스템 설계 / System design
- [Build Guide](BUILDING.md) — 소스 빌드 방법 / Build from source
- [Control API](CONTROL_API.md) — WebSocket / HTTP API 레퍼런스
- [Stream Deck Guide](STREAMDECK_GUIDE.md) — Stream Deck 플러그인 상세
