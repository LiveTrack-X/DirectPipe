# Quick Start: USB 마이크 설정 가이드 / USB Mic Setup Guide

> USB 마이크 사용자를 위한 5분 온보딩 가이드. 자세한 내용은 [User Guide](USER_GUIDE.md) 참조.
>
> 5-minute onboarding guide for USB mic users. See [User Guide](USER_GUIDE.md) for details.

> **핵심: 1~3단계만 하면 바로 깨끗한 마이크 소리를 들을 수 있습니다.** 나머지는 Discord/OBS에 보내고 싶을 때 설정하세요.
>
> **Key point: Complete steps 1-3 to hear cleaner audio right away.** Steps 4+ are for when you want to route audio to Discord/OBS.

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

> **왜 Driver를 선택하나요?** DirectPipe는 운영체제의 오디오 시스템(드라이버)을 통해 마이크에 접근합니다. 대부분의 경우 기본값이 자동 선택되어 있으니, Input에서 마이크만 고르면 됩니다.
>
> **Why select a Driver?** DirectPipe accesses your mic through your OS audio system (driver). In most cases the default is already selected — just pick your mic from the Input dropdown.

```
Audio 탭 / Audio Tab
┌──────────────────────────────┐
│ Driver: [Windows Audio     ] │  ← Windows 기본값 / Windows default
│         [CoreAudio         ] │  ← macOS
│         [ALSA / JACK       ] │  ← Linux
│ Input:  [Blue Yeti         ] │  ← USB 마이크 선택 / Select your mic
│ Output: [CABLE Input       ] │  ← 4단계에서 설정 / Set in Step 4
│ SR: 48000  BS: 256           │
└──────────────────────────────┘
```

---

## 3. [Auto] 클릭 — 바로 차이를 들어보세요! / Click [Auto] — Hear the Difference!

> **VST 플러그인을 몰라도 괜찮습니다!** [Auto] 버튼 하나로 기본적인 마이크 보정이 즉시 적용됩니다. 더 세밀한 조정은 VST 플러그인으로.
>
> **Don't know what VST plugins are? No problem!** The [Auto] button handles essential mic correction instantly. Fine-tune further with VST plugins.

1. 왼쪽 상단 입력 게인 옆의 **[Auto]** 버튼 클릭 / Click the **[Auto]** button next to the input gain slider (top-left)
2. 끝! 3가지 내장 프로세서가 자동 구성됩니다 / Done! 3 built-in processors are configured automatically:
   - **Filter** — 에어컨 소음, 마이크 진동 같은 저주파 잡음을 제거합니다 / Removes low-frequency noise like AC hum and mic rumble
   - **Noise Removal** — AI가 키보드, 팬 소리를 실시간으로 제거합니다 / AI removes keyboard clatter, fan noise in real-time
   - **Auto Gain** — 음량을 자동 조절합니다 (작으면 올리고 크면 줄임) / Auto-levels your volume (boosts quiet, tames loud)

> **여기까지 하면 마이크 소리가 깨끗해진 것을 바로 확인할 수 있습니다!** 헤드폰을 연결하고 5단계(모니터)를 설정하면 직접 들을 수 있고, 아래 4단계는 Discord/OBS 등 다른 앱에 보내고 싶을 때 진행하세요.
>
> **At this point your mic audio is already improved!** Connect headphones and set up Step 5 (Monitor) to hear yourself, or continue to Step 4 when you want to route audio to Discord/OBS.

> 각 프로세서를 클릭하면 세부 파라미터를 조정할 수 있습니다. 우클릭 → Reset to Defaults로 기본값 복원.
> Click each processor to adjust parameters. Right-click → Reset to Defaults to restore defaults.

---

## 4. 출력 경로 선택 (Discord/OBS로 보내기) / Choose Output Path (Send to Discord/OBS)

> **왜 이 단계가 필요한가요?** DirectPipe는 마이크 소리를 처리하지만, 처리된 소리를 Discord나 OBS 같은 다른 앱에서 사용하려면 "출력 경로"를 설정해야 합니다. 혼자 녹음만 한다면 이 단계를 건너뛰어도 됩니다.
>
> **Why is this step needed?** DirectPipe processes your mic audio, but to use it in other apps like Discord or OBS, you need to set up an "output path." If you only want to record locally, you can skip this step.

사용 목적에 따라 아래에서 **해당하는 것만** 설정하세요. / Set up only what you need:

### Discord / Zoom 사용자 (가상 오디오 케이블 필요) / Discord / Zoom Users (Virtual Audio Cable)

> **가상 오디오 케이블이란?** DirectPipe의 출력을 다른 앱에서 마이크처럼 인식하게 해주는 소프트웨어입니다. 한 번 설치하면 계속 사용할 수 있습니다.
>
> **What is a virtual audio cable?** It's software that makes DirectPipe's output appear as a microphone to other apps. Install once and it works permanently.

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

> **왜 OBS는 가상 케이블이 필요 없나요?** DirectPipe에 포함된 Receiver 플러그인이 OBS 안에서 직접 오디오를 수신하기 때문입니다. 더 깔끔하고 지연(레이턴시)도 적습니다.
>
> **Why no virtual cable for OBS?** The included Receiver plugin receives audio directly inside OBS. Cleaner setup with less latency.

1. Receiver 플러그인을 VST2 폴더에 복사 / Copy Receiver plugin to VST2 folder:
   - Windows: `DirectPipe Receiver.dll` → `C:\Program Files\VSTPlugins\`
   - macOS: `DirectPipe Receiver.vst` → `~/Library/Audio/Plug-Ins/VST/`
   - Linux: `DirectPipe Receiver.so` → `/usr/lib/vst/` 또는 / or `~/.vst/`
2. DirectPipe 하단 **VST** 버튼 클릭 (초록색 = IPC ON) / Click **VST** button at bottom (green = ON)
3. OBS → 오디오 소스 → 필터 → VST 2.x → **DirectPipe Receiver** 선택 / Select in OBS VST filter

```
USB 마이크 → DirectPipe
      ↓ IPC (공유 메모리 / shared memory)
OBS [DirectPipe Receiver] → 방송/녹화 / stream/record
```

### 둘 다 사용 (권장) / Both (Recommended)

Discord는 VB-Cable로, OBS는 DirectPipe Receiver로 보내면 **각 앱의 마이크를 독립 제어** 가능.

Send Discord via VB-Cable, OBS via DirectPipe Receiver — **independently control each app's mic**.

---

## 5. 헤드폰 모니터 설정 / Monitor with Headphones

자기 목소리에 이펙트가 적용된 소리를 실시간으로 확인할 수 있습니다. [Auto] 버튼을 눌렀다면, 여기서 그 차이를 직접 들어보세요!

Hear your processed voice in real-time through headphones. If you clicked [Auto], this is where you hear the difference!

1. **Output** 탭 → **Monitor** 섹션 / **Output** tab → **Monitor** section
2. **Device**: 헤드폰/이어폰 장치 선택 / Select your headphone device
3. **Enable** 토글 ON / Toggle ON
4. 상태가 **Active** (초록색)이면 정상 / Green **Active** = working

> 반드시 **헤드폰/이어폰**으로만 모니터하세요. 스피커로 모니터하면 피드백(하울링)이 발생합니다.
> **왜?** 마이크가 스피커 소리를 다시 수음하면서 소리가 무한 반복됩니다.
>
> Always monitor with **headphones/earbuds only**. Speaker monitoring causes feedback.
> **Why?** The mic picks up the speaker output and creates an infinite sound loop.

---

## 6. VST 플러그인 직접 추가 (고급) / Add VST Plugins (Advanced)

> 이미 VST 플러그인(음향 효과 소프트웨어)을 사용하고 있거나, [Auto] 외에 추가 이펙트를 적용하고 싶다면 이 단계를 진행하세요.
>
> If you already use VST plugins (audio effect software), or want to add effects beyond [Auto], follow this step.

1. **"Scan..."** 클릭 → VST 플러그인 자동 스캔 (처음 한 번만) / Click "Scan..." to find plugins (once)
2. **"+ Add Plugin"** 클릭 → 목록에서 선택 / Click "+ Add Plugin" → select from list
3. **Edit** 버튼으로 플러그인 GUI 열기 / Click **Edit** to open plugin GUI

> [Auto]와 VST 플러그인은 같은 체인에 혼합 사용 가능합니다. 예: [Auto] + 추가 EQ.
> Auto and VST plugins can be mixed in the same chain. Example: [Auto] + additional EQ.

<details>
<summary><b>무료 VST 플러그인 추천 / Recommended Free VST Plugins</b></summary>

| 플러그인 / Plugin | 용도 / Purpose | 링크 / Link |
|---|---|---|
| **RNNoise** | AI 노이즈 제거 / AI noise removal | [다운로드 / Download](https://github.com/werman/noise-suppression-for-voice) |
| **ReaPlugs** | EQ, 컴프레서, 게이트 / EQ, compressor, gate | [다운로드 / Download](https://www.reaper.fm/reaplugs/) |
| **TDR Nova** | 다이나믹 EQ / Dynamic EQ | [다운로드 / Download](https://www.tokyodawn.net/tdr-nova/) |

> 모두 무료, 64-bit VST2/VST3. / All free, 64-bit VST2/VST3.
</details>

---

## 7. 프리셋 저장 / Save a Preset

현재 플러그인 체인 설정을 저장해두면 다음에 즉시 불러올 수 있습니다. 상황별로 다른 설정을 만들어두고 한 번에 전환하세요.

Save your current plugin chain to load instantly next time. Create different setups for different situations and switch with one click.

- 왼쪽 슬롯 버튼 **A** 클릭 → 현재 체인이 A 슬롯에 저장 / Click slot **A** → saves current chain
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
