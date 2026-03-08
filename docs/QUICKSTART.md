# Quick Start: USB 마이크 설정 가이드 / USB Mic Setup Guide

> USB 마이크 사용자를 위한 5분 온보딩 가이드. 자세한 내용은 [User Guide](USER_GUIDE.md) 참조.
>
> 5-minute onboarding guide for USB mic users. See [User Guide](USER_GUIDE.md) for details.

---

## 1. 다운로드 & 실행 / Download & Launch

[최신 릴리즈 다운로드 / Download latest release](https://github.com/LiveTrack-X/DirectPipe/releases/latest) → 플랫폼별 파일 다운로드 / Download for your platform:

| 플랫폼 / Platform | 파일 / File | 실행 / Run |
|---|---|---|
| Windows | `.zip` → 압축 해제 → `DirectPipe.exe` | Unzip → Run `.exe` |
| macOS (beta) | `.dmg` → `DirectPipe.app`을 Applications로 드래그 | Drag `.app` to Applications |
| Linux (experimental) | `.tar.gz` → 압축 해제 → `DirectPipe` 바이너리 | Extract → Run `./DirectPipe` |

> **Windows**: SmartScreen 경고가 나타나면 "추가 정보" → "실행" 클릭 (오픈소스라 코드 서명이 없어서 나타나는 정상적인 경고).
> If SmartScreen warns, click "More info" → "Run anyway" (normal for unsigned open-source apps).
>
> **macOS**: Gatekeeper 경고 시 시스템 설정 → 개인정보 보호 및 보안 → "확인 없이 열기" 클릭. / If Gatekeeper blocks, go to System Settings → Privacy & Security → click "Open Anyway".
>
> **Linux**: 실행 권한 필요: `chmod +x DirectPipe`. / Make executable: `chmod +x DirectPipe`.

---

## 2. USB 마이크 선택 / Select Your USB Mic

1. 오른쪽 **Audio** 탭 → **Driver** 선택 / Right panel **Audio** tab → Select **Driver**:
   - Windows: `Windows Audio` (기본값, 권장 / default, recommended)
   - macOS: `CoreAudio`
   - Linux: `ALSA` 또는 / or `JACK`
2. **Input**: USB 마이크 선택 (예: `Microphone (Blue Yeti)`) / Select your USB mic
3. 왼쪽 **INPUT** 미터가 움직이면 마이크 인식 성공 / If the left **INPUT** meter moves, mic is working

```
Audio 탭 / Audio Tab
┌──────────────────────────────┐
│ Driver: [Windows Audio     ] │  ← Windows 기본값 / Windows default
│         [CoreAudio         ] │  ← macOS
│         [ALSA / JACK       ] │  ← Linux
│ Input:  [Blue Yeti         ] │  ← USB 마이크 선택 / Select your mic
│ Output: [CABLE Input       ] │  ← 3단계에서 설정 / Set in Step 3
│ SR: 48000  BS: 256           │
└──────────────────────────────┘
```

---

## 3. 출력 경로 선택 / Choose Your Output Path

사용 목적에 따라 아래에서 **해당하는 것만** 설정하세요. / Set up only what you need:

### Discord / Zoom 사용자 (가상 오디오 케이블 필요) / Discord / Zoom Users (Virtual Audio Cable)

가상 오디오 케이블 설치 / Install a virtual audio cable:

| 플랫폼 / Platform | 소프트웨어 / Software | 비고 / Notes |
|---|---|---|
| Windows | [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) (무료 / free) | 설치 후 **PC 재부팅** / **Reboot** after install |
| macOS | [BlackHole](https://existential.audio/blackhole/) (무료 / free) 또는 / or [Loopback](https://rogueamoeba.com/loopback/) (유료 / paid) | BlackHole 2ch 권장 / BlackHole 2ch recommended |
| Linux | [PipeWire](https://pipewire.org/) (내장 / built-in) 또는 / or JACK 라우팅 / routing | PipeWire가 최신 배포판 기본 / PipeWire default on modern distros |

설정 / Setup:

1. DirectPipe **Audio** 탭 → **Output**: 가상 케이블 입력 선택 / Select virtual cable input as Output
   - Windows: `CABLE Input (VB-Audio Virtual Cable)`
   - macOS: `BlackHole 2ch`
   - Linux: PipeWire/JACK 가상 장치 / virtual device
2. Discord/Zoom 음성 설정 → 마이크: 가상 케이블 출력 선택 / Set virtual cable output as mic in Discord/Zoom
   - Windows: `CABLE Output (VB-Audio Virtual Cable)`
   - macOS: `BlackHole 2ch`
   - Linux: PipeWire/JACK 해당 장치 / corresponding device

```
USB 마이크 → DirectPipe → 가상 케이블 / Virtual Cable
                                 ↓
               Discord/Zoom ← 가상 케이블 출력 / Virtual Cable Output (마이크로 인식 / recognized as mic)
```

### OBS 사용자 (가상 케이블 불필요) / OBS Users (No Virtual Cable)

1. Receiver 플러그인을 VST2 폴더에 복사 / Copy Receiver plugin to VST2 folder:
   - Windows: `DirectPipe Receiver.dll` → `C:\Program Files\VSTPlugins\`
   - macOS: `DirectPipe Receiver.vst` → `/Library/Audio/Plug-Ins/VST/`
   - Linux: `DirectPipe Receiver.so` → `/usr/lib/vst/` 또는 / or `~/.vst/`
2. DirectPipe 하단 **VST** 버튼 클릭 (초록색 = IPC ON) / Click **VST** button at bottom (green = ON)
3. OBS → 오디오 소스 → 필터 → VST 2.x → **DirectPipe Receiver** 선택 / Select in OBS VST filter

```
USB 마이크 → DirectPipe
      ↓ IPC (공유 메모리 / shared memory)
OBS [DirectPipe Receiver] → 방송/녹화 / stream/record
```

### 둘 다 사용 (권장) / Both (Recommended)

Discord는 VB-Cable로, OBS는 Receiver VST로 보내면 **각 앱의 마이크를 독립 제어** 가능.

Send Discord via VB-Cable, OBS via Receiver VST — **independently control each app's mic**.

---

## 4. VST 플러그인 추가 / Add VST Plugins

1. **"Scan..."** 클릭 → PC에 설치된 VST 플러그인 자동 스캔 (처음 한 번만) / Click "Scan..." to find installed plugins (once)
2. **"+ Add Plugin"** 클릭 → 목록에서 원하는 플러그인 선택 / Click "+ Add Plugin" → select from list
3. **Edit** 버튼으로 플러그인 GUI를 열어 설정 / Click **Edit** to open plugin GUI

**아직 VST 플러그인이 없다면? / No VST plugins yet?**

| 플러그인 / Plugin | 용도 / Purpose | 링크 / Link |
|---|---|---|
| **RNNoise** | AI 노이즈 제거 (키보드, 팬 소음 차단) / AI noise removal | [다운로드 / Download](https://github.com/werman/noise-suppression-for-voice) |
| **ReaPlugs** | EQ, 컴프레서, 게이트 (올인원) / EQ, compressor, gate (all-in-one) | [다운로드 / Download](https://www.reaper.fm/reaplugs/) |
| **TDR Nova** | 다이나믹 EQ / Dynamic EQ | [다운로드 / Download](https://www.tokyodawn.net/tdr-nova/) |

> 모두 무료이며, 64-bit VST2 또는 VST3로 설치하세요. / All free — install as 64-bit VST2 or VST3.

**권장 체인 순서 / Recommended Chain Order:**

```
[노이즈 제거 / Noise Removal] → [EQ] → [컴프레서 / Compressor]
```

---

## 5. 헤드폰 모니터 설정 / Monitor with Headphones

자기 목소리에 이펙트가 적용된 소리를 실시간으로 확인할 수 있습니다.

Hear your processed voice in real-time through headphones.

1. **Output** 탭 → **Monitor** 섹션 / **Output** tab → **Monitor** section
2. **Device**: 헤드폰/이어폰 장치 선택 / Select your headphone device
3. **Enable** 토글 ON / Toggle ON
4. 상태가 **Active** (초록색)이면 정상 / Green **Active** = working

> 반드시 **헤드폰/이어폰**으로만 모니터하세요. 스피커로 모니터하면 피드백(하울링)이 발생합니다.
>
> Always monitor with **headphones/earbuds only**. Speaker monitoring causes feedback.

---

## 6. 프리셋 저장 / Save a Preset

현재 플러그인 체인 설정을 저장해두면 다음에 즉시 불러올 수 있습니다.

Save your current plugin chain to load instantly next time.

- 왼쪽 하단 슬롯 버튼 **A** 클릭 → 현재 체인이 A 슬롯에 저장 / Click slot **A** → saves current chain
- **우클릭** → **Rename** → 이름 지정 (예: "게임", "통화") / Right-click → **Rename** (e.g., "Game", "Chat")
- 슬롯 **B~E**에 다른 설정 저장 → 원클릭으로 전환 / Save different setups to B-E → one-click switch

```
[A|게임]  [B|통화]  [C|노래]  [ D ]  [ E ]
  Game     Chat     Sing    empty   empty
```

---

## 완료! / Done!

DirectPipe가 시스템 트레이에 상주합니다. X 버튼은 종료가 아닌 **트레이 최소화**입니다.

DirectPipe stays in the system tray (Windows/Linux) or menu bar (macOS). The X button **minimizes to tray**, not quit.

| 동작 / Action | 방법 / How |
|---|---|
| 창 복원 / Restore | 트레이 아이콘 더블클릭 (macOS: 메뉴 바 아이콘 클릭) / Double-click tray icon (macOS: click menu bar icon) |
| 완전 종료 / Quit | 트레이 우클릭 → "Quit DirectPipe" (macOS: 메뉴 바 아이콘 → Quit) / Right-click tray → "Quit DirectPipe" |

---

## 다음 단계 / Next Steps

| 하고 싶은 것 / What you want | 참조 / Reference |
|---|---|
| 단축키로 제어 / Control via hotkeys | [User Guide — 키보드 단축키](USER_GUIDE.md#키보드-단축키--keyboard-shortcuts) |
| Stream Deck 연동 / Stream Deck setup | [Stream Deck Guide](STREAMDECK_GUIDE.md) |
| MIDI 컨트롤러 매핑 / MIDI controller mapping | [User Guide — MIDI 제어](USER_GUIDE.md#midi-제어--midi-control) |
| HTTP/WebSocket API 연동 / API integration | [Control API Reference](CONTROL_API.md) |
| ASIO 저지연 설정 (Windows) / ASIO low-latency (Windows) | [User Guide — ASIO vs WASAPI](USER_GUIDE.md#asio-vs-wasapi-선택-가이드--asio-vs-wasapi-decision-guide) |
| 소리 안 남 / No audio | [User Guide — 문제 해결](USER_GUIDE.md#문제-해결--troubleshooting) |
