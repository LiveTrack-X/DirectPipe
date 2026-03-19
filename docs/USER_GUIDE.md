# DirectPipe User Guide / 사용자 가이드

> **Version 4.0.1** — [GitHub Releases](https://github.com/LiveTrack-X/DirectPipe/releases)

## 시작하기 / Getting Started

처음 사용하신다면 [Quick Start 가이드](QUICKSTART.md)를 먼저 읽어보세요. 3단계면 됩니다:

1. **DirectPipe 실행** → Audio 탭에서 마이크 선택
2. **[Auto] 버튼 클릭** → 노이즈 제거 + 자동 볼륨 + 필터 즉시 적용
3. **Discord/OBS에서 사용하려면** 출력 경로 설정 (가상 케이블 또는 Receiver)

If you're new, start with the [Quick Start guide](QUICKSTART.md). 3 steps:

1. **Launch DirectPipe** → Select your mic in Audio tab
2. **Click [Auto]** → Noise removal + auto volume + filter applied instantly
3. **To use in Discord/OBS**, set up output routing (virtual cable or Receiver)

> VST 플러그인(음향 효과 소프트웨어)을 몰라도 괜찮습니다 — [Auto] 버튼 하나로 기본적인 마이크 보정이 완료됩니다. 더 세밀한 조정은 VST 플러그인으로.
>
> Don't know what VST plugins (audio effect software) are? No worries — the [Auto] button handles essential mic correction. Fine-tune further with VST plugins.

---

## DirectPipe란? / What is DirectPipe?

DirectPipe는 실시간 VST2/VST3 호스트입니다. USB 마이크 입력에 노이즈 제거, EQ, 컴프레서 등 VST 플러그인을 걸어 실시간으로 처리한 뒤, Discord·Zoom·OBS 등 다른 앱에서 사용할 수 있도록 출력합니다. 시스템 트레이(macOS: 메뉴 바)에 상주하며, 키보드 단축키·MIDI·Stream Deck·HTTP API로 원격 제어할 수 있습니다.

> **플랫폼 지원 / Platform Support**: Windows (정식), macOS (베타), Linux (실험적)

DirectPipe is a real-time VST2/VST3 host. It processes your USB microphone input through VST plugins (noise removal, EQ, compressor, etc.) and routes the output to other apps like Discord, Zoom, or OBS. It runs in the system tray (macOS: menu bar) and can be remotely controlled via hotkeys, MIDI, Stream Deck, or HTTP API.

> **Platform Support**: Windows (stable), macOS (beta), Linux (experimental)

```
USB 마이크 → DirectPipe (VST 플러그인 체인) → Main Output (가상 케이블 → Discord/Zoom)
                                                 ├→ Monitor Output (헤드폰)
                                                 ├→ IPC Output (OBS DirectPipe Receiver)
                                                 └→ WAV Recording
```

---

## 빠른 시작 / Quick Start

> 더 자세한 단계별 가이드는 [Quick Start 가이드](QUICKSTART.md)를 참조하세요. / For a detailed step-by-step walkthrough, see the [Quick Start guide](QUICKSTART.md).

### 1단계: 기본 설정 / Step 1: Basic Setup

1. **DirectPipe 실행** — 처음 실행 시 보안 경고가 나타날 수 있습니다 (오픈소스라 코드 서명이 없어서 나타나는 정상적인 경고):
   - **Windows**: SmartScreen → "추가 정보" → "실행" 클릭
   - **macOS**: Gatekeeper → 시스템 설정 → 보안 및 개인 정보 → "확인 없이 열기"
   - **Linux**: `chmod +x DirectPipe` 후 실행
2. **Audio 탭**에서 드라이버 선택 (대부분 기본값으로 자동 선택됨):
   - **Windows Audio (WASAPI)** — 대부분의 Windows 사용자에게 권장 (기본값)
   - **ASIO** — 저지연이 필요한 경우 (오디오 인터페이스 필요, Windows 전용)
   - **CoreAudio** — macOS 기본 드라이버
   - **ALSA / JACK** — Linux 드라이버
3. **Input** — USB 마이크 선택
4. **[Auto] 클릭** — 노이즈 제거 + 자동 볼륨 + 필터 즉시 적용 (VST 플러그인을 몰라도 OK)
5. **Output** — Discord/OBS에 보내려면 가상 오디오 케이블 선택 (예: Windows `VB-Cable`, macOS `BlackHole`, Linux `PipeWire`)

---

1. **Launch DirectPipe** — A security warning may appear on first run (normal for unsigned open-source apps):
   - **Windows**: SmartScreen → click "More info" → "Run anyway"
   - **macOS**: Gatekeeper → System Settings → Privacy & Security → "Open Anyway"
   - **Linux**: `chmod +x DirectPipe` then run
2. **Audio tab** — Select driver (usually auto-selected by default):
   - **Windows Audio (WASAPI)** — Recommended for most Windows users (default)
   - **ASIO** — For lowest latency (requires audio interface, Windows only)
   - **CoreAudio** — Default macOS driver
   - **ALSA / JACK** — Linux drivers
3. **Input** — Select your USB microphone
4. **Click [Auto]** — Noise removal + auto volume + filter applied instantly (no VST knowledge needed)
5. **Output** — To send to Discord/OBS, select a virtual audio cable (e.g., Windows `VB-Cable`, macOS `BlackHole`, Linux `PipeWire`)

### 2단계: 플러그인 추가 (고급) / Step 2: Add Plugins (Advanced)

> [Auto] 버튼만으로 충분하다면 이 단계를 건너뛰어도 됩니다. / If [Auto] is enough for your needs, you can skip this step.

1. **"Scan..."** 클릭 → 설치된 VST 플러그인(음향 효과 소프트웨어) 스캔 (처음 한 번만 필요)
2. **"+ Add Plugin"** 클릭 → 목록에서 원하는 플러그인 선택
3. 플러그인이 체인에 추가됨 — **Edit** 버튼으로 플러그인 GUI 열기

**추천 무료 플러그인 / Recommended free plugins:**
- [ReaPlugs](https://www.reaper.fm/reaplugs/) — EQ, 컴프레서, 게이트 (무료) / EQ, compressor, gate (free)
- [RNNoise](https://github.com/werman/noise-suppression-for-voice) — AI 노이즈 제거 (무료) / AI noise removal (free)
- [TDR Nova](https://www.tokyodawn.net/tdr-nova/) — 다이나믹 EQ (무료) / Dynamic EQ (free)

---

1. Click **"Scan..."** → Scans installed VST plugins (only needed once)
2. Click **"+ Add Plugin"** → Select a plugin from the list
3. Plugin is added to the chain — Click **Edit** to open the plugin GUI

### 3단계: 다른 앱에서 사용 / Step 3: Use in Other Apps

**Discord / Zoom / Google Meet:**

가상 오디오 케이블을 설치하면 DirectPipe의 출력을 다른 앱에서 마이크로 인식하게 할 수 있습니다. / Install a virtual audio cable so other apps see DirectPipe's output as a microphone.

| 플랫폼 / Platform | 가상 케이블 / Virtual Cable | 설치 / Install |
|---|---|---|
| **Windows** | [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) | 설치 후 PC 재부팅 / Install + reboot |
| **macOS** | [BlackHole](https://existential.audio/blackhole/) | Homebrew: `brew install blackhole-2ch` |
| **Linux** | PipeWire (내장 / built-in) | `pw-loopback`으로 가상 장치 생성 / Create virtual device with `pw-loopback` |

1. [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) (Windows), [BlackHole](https://existential.audio/blackhole/) (macOS), 또는 PipeWire (Linux) 설치 / Install for your platform
2. DirectPipe Audio 탭: Output → 가상 케이블 입력 선택 (Windows: `CABLE Input`, macOS: `BlackHole 2ch`, Linux: PipeWire loopback) / Audio tab: Output → select virtual cable input
3. Discord/Zoom 음성 설정: 마이크 → 가상 케이블 출력 선택 / Voice settings: mic → select virtual cable output

```
USB 마이크 → DirectPipe → 가상 케이블 (VB-Cable / BlackHole / PipeWire)
USB Mic   → DirectPipe → Virtual Cable Input
                                ↓
              Discord/Zoom ← 가상 케이블 출력 (마이크로 인식 / recognized as mic)
```

**OBS (Receiver VST2 — 가상 케이블 불필요 / no virtual cable needed):**
1. Receiver 플러그인을 VST2 폴더에 복사 / Copy the Receiver plugin to a VST2 folder:
   - **Windows**: `DirectPipe Receiver.dll` → `C:\Program Files\VSTPlugins\` (권장 / recommended), `C:\Program Files\Common Files\VST2\`, 또는 / or `C:\Program Files\Steinberg\VstPlugins\`
   - **macOS**: `DirectPipe Receiver.vst` → `~/Library/Audio/Plug-Ins/VST/`
   - **Linux**: `DirectPipe Receiver.so` → `~/.vst/` 또는 / or `/usr/lib/vst/`
2. DirectPipe에서 **VST** 버튼 클릭 (IPC 출력 켜기) / Click **VST** button in DirectPipe (enable IPC output)
3. OBS → 오디오 소스 → 필터 → VST 2.x 플러그인 → **DirectPipe Receiver** 선택 / OBS → audio source → Filters → VST 2.x Plugin → select **DirectPipe Receiver**

```
USB 마이크 → DirectPipe (VST 이펙트)
USB Mic   → DirectPipe (VST effects)
      ↓ IPC (공유 메모리 / shared memory)
OBS [DirectPipe Receiver 필터 / filter] → 방송/녹화 / stream/record
```

> **Tip**: 가상 케이블과 DirectPipe Receiver를 **동시에** 사용할 수 있습니다. Discord는 가상 케이블로, OBS는 DirectPipe Receiver로 각각 보내면 됩니다.
> You can use a virtual cable and DirectPipe Receiver **simultaneously**. Send Discord via virtual cable and OBS via DirectPipe Receiver.

### 가상 케이블 vs DirectPipe Receiver 선택 가이드 / Virtual Cable vs DirectPipe Receiver: Which to Use

| | 가상 케이블 / Virtual Cable | DirectPipe Receiver (IPC) |
|---|---|---|
| **대상 앱 / Target apps** | Discord, Zoom, Teams 등 **마이크 입력을 선택하는 앱** / Apps that **select mic input** | OBS 등 **VST 필터를 지원하는 앱** / Apps that **support VST filters** |
| **설치 / Setup** | 가상 케이블 드라이버 설치 필요 (Windows: VB-Cable, macOS: BlackHole, Linux: PipeWire) / Install virtual cable driver | 플러그인 파일 복사만 하면 됨 (Windows: .dll, macOS: .vst/.component, Linux: .so). OBS는 VST2만 지원 / Just copy the plugin file. OBS only supports VST2 |
| **원리 / How it works** | DirectPipe → 가상 케이블 → 앱이 "마이크"로 인식 / App recognizes as "mic" | DirectPipe → 공유 메모리 IPC → 앱 내 Receiver 플러그인이 수신 / Receiver plugin receives via shared memory |
| **지연 / Latency** | 가상 케이블 버퍼 + 오디오 버퍼에 따라 다름 / Depends on virtual cable + audio buffers | 공유 메모리 IPC, 버퍼 설정에 따라 5~85ms / Shared memory IPC, 5~85ms depending on buffer |
| **장점 / Pros** | 어떤 앱이든 "마이크"로 인식 가능 / Any app can use it as "mic" | 가상 케이블 설치 불필요, 시스템 깔끔 / No virtual cable needed, clean system |
| **제한 / Cons** | 가상 오디오 드라이버 설치 필요 / Requires virtual audio driver | VST 필터 지원 앱에서만 사용 가능. OBS는 모든 플랫폼에서 VST2만 지원 / Only in apps supporting VST filters. OBS supports VST2 only on all platforms |

**동시 사용 (권장)**: Discord는 가상 케이블로, OBS는 DirectPipe Receiver로 보내면 **각 앱의 마이크를 OUT/VST 버튼으로 독립 제어** 가능.

**Dual use (recommended)**: Send Discord via virtual cable, OBS via DirectPipe Receiver — independently control each app's mic with OUT/VST buttons.

### Receiver VST2 설치 및 OBS 설정 상세 가이드 / Receiver VST2 Installation & OBS Setup

#### 1단계: DLL 설치 / Step 1: Install DLL

Receiver 플러그인 파일을 아래 VST2 폴더 **중 하나**에 복사합니다.

Copy the Receiver plugin file to **one** of these VST2 folders:

**Windows** (`DirectPipe Receiver.dll`):

| 경로 / Path | 비고 / Note |
|---|---|
| `C:\Program Files\VSTPlugins\` | **권장** / Recommended |
| `C:\Program Files\Common Files\VST2\` | 대체 경로 / Alternative |
| `C:\Program Files\Steinberg\VstPlugins\` | Steinberg 기본 / Steinberg default |

**macOS** (`DirectPipe Receiver.vst`):

| 경로 / Path | 비고 / Note |
|---|---|
| `~/Library/Audio/Plug-Ins/VST/` | **권장** / Recommended |

**Linux** (`DirectPipe Receiver.so`):

| 경로 / Path | 비고 / Note |
|---|---|
| `~/.vst/` | 사용자 디렉토리 / User directory |
| `/usr/lib/vst/` | 시스템 전체 / System-wide |

> Receiver 플러그인은 GitHub 릴리즈에 DirectPipe와 함께 포함되어 있습니다. / The Receiver plugin is included in the GitHub release alongside DirectPipe.

#### 2단계: OBS 설정 / Step 2: Configure OBS

1. OBS에서 **오디오 소스** 추가 (마이크 캡처 또는 오디오 입력 캡처) — 어떤 장치를 선택해도 상관없음 (Receiver가 무시함)
2. 해당 소스 → **필터** 클릭
3. **"+"** → **"VST 2.x 플러그인"** 선택
4. 플러그인 목록에서 **"DirectPipe Receiver"** 선택
5. **"플러그인 인터페이스 열기"** 클릭

1. Add an **audio source** in OBS (mic capture or audio input) — any device works (Receiver ignores it)
2. Click **Filters** on that source
3. Click **"+"** → **"VST 2.x Plugin"**
4. Select **"DirectPipe Receiver"** from the dropdown
5. Click **"Open Plugin Interface"**

> **중요**: DirectPipe Receiver는 **입력 버스가 없는 출력 전용 플러그인**입니다. OBS 오디오 소스의 마이크 선택은 의미가 없습니다 — Receiver는 소스의 오디오를 무시하고, DirectPipe에서 IPC로 전송된 오디오만 출력합니다.
>
> **Important**: DirectPipe Receiver is an **output-only plugin with no input bus**. The OBS source's device selection doesn't matter — Receiver ignores source audio and only outputs what DirectPipe sends via IPC.

#### 3단계: DirectPipe에서 IPC 켜기 / Step 3: Enable IPC in DirectPipe

DirectPipe 하단의 **VST** 버튼 클릭 → 초록색으로 바뀌면 IPC 출력 활성화.

Click the **VST** button at the bottom of DirectPipe → green = IPC output enabled.

#### 4단계: 연결 확인 / Step 4: Verify Connection

Receiver 플러그인 GUI에서 다음을 확인합니다:

Check the Receiver plugin GUI for:

| 표시 / Indicator | 의미 / Meaning |
|---|---|
| 🟢 **Connected** | DirectPipe와 정상 연결 / Connected to DirectPipe |
| 🔴 **Disconnected** | DirectPipe 미실행 또는 IPC 꺼짐 / DirectPipe not running or IPC disabled |
| **48000Hz 2ch** | 수신 중인 오디오 포맷 / Receiving audio format |
| **SR mismatch: 48000 vs 44100** | 샘플레이트 불일치 경고 (아래 참조) / Sample rate mismatch warning (see below) |

#### 5단계: 버퍼 크기 선택 / Step 5: Choose Buffer Size

Receiver 플러그인 GUI의 **Buffer** 드롭다운에서 지연과 안정성의 균형을 선택합니다.

Select the balance between latency and stability in the Receiver plugin GUI **Buffer** dropdown.

| 프리셋 / Preset | 샘플 / Samples | @48kHz | 권장 용도 / Recommended Use |
|---|---|---|---|
| **Ultra Low** | 256 | ~5ms | 최소 지연, 고사양 PC / Minimum latency, high-end PC |
| **Low** (기본 / default) | 512 | ~10ms | **대부분의 사용자 / Most users** |
| **Medium** | 1024 | ~21ms | 안정적 / Stable |
| **High** | 2048 | ~42ms | CPU 부하 높을 때 / High CPU load |
| **Safe** | 4096 | ~85ms | 끊김 자주 발생 시 / Frequent crackling |

#### 6단계: 샘플레이트 맞추기 / Step 6: Match Sample Rates

DirectPipe와 OBS의 샘플레이트가 다르면 **피치/속도 변동**이 발생합니다. 반드시 동일하게 맞추세요.

If DirectPipe and OBS sample rates differ, **pitch/speed artifacts** occur. They must match.

| 확인 위치 / Where | 설정 경로 / Setting Path |
|---|---|
| **DirectPipe** | Audio 탭 → Sample Rate / Audio tab → Sample Rate |
| **OBS** | 설정 → 오디오 → 샘플 레이트 / Settings → Audio → Sample Rate |

> 권장: **48000Hz** (대부분의 스트리밍 표준). / Recommended: **48000Hz** (standard for most streaming).

#### 완성된 신호 흐름 / Complete Signal Flow

```
USB 마이크
    ↓
DirectPipe (VST 이펙트 체인: 노이즈 제거, EQ, 컴프 ...)
    ↓ IPC (공유 메모리, 가상 케이블 불필요)
OBS [오디오 소스 → 필터: DirectPipe Receiver]
    ↓
방송 / 녹화
```

---

## UI 구성 / UI Layout

DirectPipe는 2컬럼 레이아웃입니다. / DirectPipe uses a two-column layout.

**왼쪽 컬럼 / Left Column:**
- INPUT 레벨 미터 (dB 로그 스케일, 좌측 가장자리) / INPUT level meter (dB log scale, left edge)
- 입력 게인 슬라이더 (0.0x ~ 2.0x) + [Auto] 버튼 / Input gain slider (0.0x ~ 2.0x) + [Auto] button
- Save Preset / Load Preset 버튼 / Save Preset / Load Preset buttons
- Quick Preset Slots (A ~ E)
- VST 플러그인 체인 (드래그 앤 드롭 순서 변경, 하단에 Add Plugin / Scan / Remove 버튼) / VST plugin chain (drag-and-drop reorder, Add Plugin / Scan / Remove buttons at bottom)
- 뮤트 인디케이터 (OUT / MON / VST) / Mute indicators (OUT / MON / VST)
- PANIC MUTE 버튼 / PANIC MUTE button

**오른쪽 컬럼 / Right Column:**
- 4개 탭: **Audio** / **Output** / **Controls** / **Settings** / 4 tabs
- OUTPUT 레벨 미터 / OUTPUT level meter

**하단 상태 바 / Status Bar:**
- 레이턴시 (ms), CPU 사용률 (%) + XRun 카운터 (60초간), 오디오 포맷, 포터블 모드 표시 (Audio Only 시 주황색) / Latency (ms), CPU usage (%) + XRun counter (60s window), audio format, portable mode indicator (orange in Audio Only)
- XRun 발생 시 빨간색으로 강조 / XRun count highlighted in red when > 0
- 오류/경고/정보 알림 (자동 페이드) / Error/warning/info notifications (auto-fade)
- 버전 정보 + 업데이트 알림 / Version info + update notification

---

## 오디오 설정 (Audio 탭) / Audio Settings

### 드라이버 타입 / Driver Type

Audio 탭의 **Driver** 드롭다운에서 드라이버를 선택할 수 있습니다. 사용 가능한 드라이버는 플랫폼에 따라 다릅니다. / Select a driver from the Audio tab Driver dropdown. Available drivers depend on your platform.

#### Windows 드라이버 / Windows Drivers

| | Windows Audio | Low Latency | Exclusive Mode | ASIO |
|---|---|---|---|---|
| **지연** / Latency | **3-10ms** | 1-3ms (지원 시) | 10-20ms | **2-5ms** |
| **설치** / Setup | 없음 / None | 없음 / None | 없음 / None | 드라이버 필요 / Driver required |
| **마이크 공유** / Shared | O | O | X | 장치에 따라 다름 / Depends on device |
| **추천** / Best for | **대부분의 사용자 / Most users** | 제한적 / Limited | 녹음 전용 / Recording only | 전문가 / Professionals |

- **DirectSound** — 레거시 API. 사용하지 마세요. / Legacy API, don't use.
- **Windows Audio** (추천) — WASAPI 공유 모드. 대부분의 사용자에게 가장 안정적 / WASAPI Shared mode, most reliable for most users
- **Windows Audio (Low Latency)** — IAudioClient3 기반. **많은 USB 마이크가 제대로 지원하지 않아** 일반 Windows Audio보다 오히려 버퍼가 크게 나올 수 있음 / IAudioClient3-based. Many USB mics don't properly support it — may have higher buffers than standard mode
- **Windows Audio (Exclusive Mode)** — 독점 모드. 다른 앱이 해당 장치를 사용할 수 없음 / Exclusive mode, no other apps can use the device
- **ASIO** — 오디오 인터페이스의 네이티브 ASIO 드라이버 필요. 최저 지연 / Requires native ASIO driver, lowest latency

#### macOS 드라이버 / macOS Drivers (Beta)

- **CoreAudio** — macOS 기본 오디오 시스템. 별도 드라이버 없이 저지연 지원 / Default macOS audio system. Low latency without extra drivers

#### Linux 드라이버 / Linux Drivers (Experimental)

- **ALSA** — Linux 기본 오디오 레이어 / Default Linux audio layer
- **JACK** — 전문 오디오용 저지연 서버 (PipeWire JACK 호환 포함) / Low-latency audio server for professional use (including PipeWire JACK compatibility)

### 드라이버 선택 가이드 / Driver Selection Guide

**Windows:**

| 상황 / Situation | 권장 드라이버 / Recommended | 이유 / Why |
|---|---|---|
| USB 마이크만 사용 / USB mic only | **Windows Audio** | 별도 드라이버 불필요, 안정적 / No extra driver needed, stable |
| 오디오 인터페이스 보유 (Focusrite, MOTU 등) / Audio interface (Focusrite, MOTU, etc.) | **ASIO** | 최저 지연 (~2ms), 인터페이스 네이티브 드라이버 활용 / Lowest latency (~2ms), native driver |
| 자기 목소리 모니터링 필요 (최소 지연) / Self-monitoring with minimum latency | **ASIO** | 입출력이 하나의 장치에서 처리되어 별도 모니터 장치 불필요 / I/O on one device, no separate monitor needed |
| 다른 앱과 마이크 공유 필요 / Need to share mic with other apps | **Windows Audio** | WASAPI Shared = 비독점 접근 / WASAPI Shared = non-exclusive access |
| USB 마이크 + 최소 지연 원함 / USB mic + want lowest latency | **Windows Audio** | Low Latency 모드는 USB 마이크에서 제대로 지원 안 되는 경우 많음 / Low Latency mode often unsupported by USB mics |

**macOS:** CoreAudio가 유일한 드라이버이며 대부분의 상황에서 충분한 저지연 성능을 제공합니다. / CoreAudio is the only driver and provides sufficient low-latency performance for most situations.

**Linux:** USB 마이크는 ALSA, 오디오 인터페이스는 JACK (또는 PipeWire JACK) 권장. / ALSA for USB mics, JACK (or PipeWire JACK) for audio interfaces.

> **ASIO 모니터링 장점 (Windows)**: ASIO 드라이버를 사용하면 입력(마이크)과 출력(헤드폰)이 하나의 ASIO 장치에서 처리됩니다. 별도 WASAPI 모니터 장치 없이도 자기 목소리를 최소 지연(~2ms)으로 들을 수 있습니다. Output 탭의 Monitor 설정은 불필요합니다.
>
> **ASIO monitoring advantage (Windows)**: With an ASIO driver, input (mic) and output (headphones) share a single ASIO device. You can hear yourself with minimal latency (~2ms) without a separate WASAPI monitor device. The Output tab Monitor setup is unnecessary.

### 샘플레이트 & 버퍼 크기 / Sample Rate & Buffer Size

> **Audio 탭의 샘플레이트가 전체 시스템에 적용됩니다**: VST 체인, 모니터 출력, IPC(DirectPipe Receiver) 모두 이 값을 따릅니다. 모니터 출력은 메인 SR에 맞지 않으면 Error 상태가 됩니다.
>
> **Audio tab sample rate applies globally**: VST chain, monitor output, and IPC (DirectPipe Receiver) all follow this value. Monitor output will show Error if its device cannot match the main SR.

- **WASAPI** (Windows): 장치가 지원하는 크기만 표시. 지원하지 않는 크기 선택 시 가장 가까운 값으로 자동 전환 + 알림 표시 / Shows device-supported sizes only. Auto-fallback to closest supported size with notification
- **ASIO** (Windows): 장치가 지원하는 값만 표시. ASIO Control Panel에서 설정 가능 / Shows device-supported values only. Configurable via ASIO Control Panel
- **CoreAudio** (macOS): 장치가 지원하는 범위 내에서 선택 / Select within device-supported range
- **ALSA/JACK** (Linux): 장치 및 서버 설정에 따라 다름 / Depends on device and server configuration

**버퍼 크기 가이드 / Buffer size guide:**

| 버퍼 / Buffer | 지연 (@48kHz) | 권장 / Recommended |
|---|---|---|
| 128 samples | ~2.7ms | 고사양 PC, 최소 지연 / High-end PC, minimum latency |
| 256 samples | ~5.3ms | 일반적인 시작점 / Common starting point |
| 512 samples | ~10.7ms | 안정적, 저사양 PC / Stable, low-end PC |
| 1024 samples | ~21.3ms | CPU 부하 높을 때 / High CPU load |

### 샘플레이트 맞추기 체크리스트 / Sample Rate Matching Checklist

DirectPipe, OBS, Discord의 샘플레이트가 서로 다르면 **피치 변동, 끊김, 속도 변화**가 발생할 수 있습니다. 아래 3곳을 모두 동일하게 맞추세요 (권장: **48000Hz**).

Mismatched sample rates between DirectPipe, OBS, and Discord can cause **pitch shifts, crackling, or speed changes**. Match all 3 locations below (recommended: **48000Hz**).

| # | 확인 위치 / Where | 설정 경로 / Setting Path |
|---|---|---|
| 1 | **DirectPipe** | Audio 탭 → Sample Rate 드롭다운 / Audio tab → Sample Rate dropdown |
| 2 | **OBS** | 설정 → 오디오 → 샘플 레이트 (General) / Settings → Audio → Sample Rate (General) |
| 3 | **Discord** | 설정 → 음성 및 비디오 → 고급 → (Discord는 48kHz 고정, 별도 설정 없음) / Settings → Voice & Video → Advanced → (Discord fixed at 48kHz, no setting) |
| 4 | **OS 사운드 설정 / OS Sound Settings** | **Windows**: 설정 → 시스템 → 사운드 → 장치 속성 → 고급 → 기본 형식 / Settings → System → Sound → Device Properties → Advanced → Default Format |
| | | **macOS**: 시스템 설정 → 사운드 (또는 Audio MIDI Setup 앱) / System Settings → Sound (or Audio MIDI Setup app) |
| | | **Linux**: PipeWire/PulseAudio 설정 또는 `pavucontrol` / PipeWire/PulseAudio settings or `pavucontrol` |

> **DirectPipe Receiver SR 경고**: DirectPipe의 SR과 OBS의 SR이 다르면 Receiver 플러그인 GUI에 주황색 `"SR mismatch"` 경고가 표시됩니다. 이 경고가 보이면 위 체크리스트를 확인하세요. / If DirectPipe Receiver shows an orange `"SR mismatch"` warning, check the list above.

### 채널 모드 / Channel Mode

- **Stereo** (기본값) — 입력 채널이 그대로 L/R로 통과 / Input channels pass through as-is
- **Mono** — 모든 입력 채널을 합산하여 모노로 만든 뒤 양쪽(L/R)으로 출력. 이후 VST 체인·메인 출력·모니터·IPC·녹음 모두 스테레오로 처리됨. 마이크 1개만 사용하면 볼륨 손실 없음. 2개 이상 입력 시 합산 (클리핑 발생 시 입력 게인으로 조절) / Sums all input channels to mono, then outputs to both L/R. Everything downstream (VST chain, main output, monitor, IPC, recording) processes as stereo. No volume loss for single-mic input. Multiple inputs are summed at full gain (adjust input gain if clipping occurs)

---

## VST 플러그인 체인 / VST Plugin Chain

VST2와 VST3 플러그인을 직렬 체인으로 연결합니다. / Connects VST2 and VST3 plugins in a serial chain.

> **플러그인 파일 형식 / Plugin file formats**: Windows: `.dll` (VST2) / `.vst3`, macOS: `.vst` (VST2) / `.vst3` (Component), Linux: `.so` (VST2) / `.vst3`

| 동작 / Action | 방법 / How |
|---|---|
| **추가** / Add | "+ Add Plugin" 클릭 → 목록에서 선택 / Click "+ Add Plugin" → select from list |
| **제거** / Remove | 플러그인 행의 X 버튼 또는 하단 "Remove" / X button on plugin row or "Remove" at bottom |
| **순서 변경** / Reorder | 드래그 앤 드롭 / Drag and drop |
| **Bypass** | 플러그인 행의 Bypass 토글 클릭 / Click Bypass toggle on plugin row |
| **편집** / Edit | "Edit" 클릭 → 플러그인 네이티브 GUI 열기 / Click "Edit" → open plugin native GUI |

### 자동 저장 / Auto-Save

플러그인 파라미터(EQ, 컴프레서 설정 등)는 자동으로 저장/복원됩니다:
- 설정 변경 후 1초 뒤 자동 저장 / Auto-saves 1 second after change
- 프리셋 슬롯 전환 시 / On preset slot switch
- 앱 종료 시 / On app exit

Plugin parameters are auto-saved/restored on setting change (1-second debounce), preset slot switch, and app exit.

### 플러그인 체인 순서 가이드 / Plugin Chain Order Guide

플러그인 순서는 음질에 큰 영향을 줍니다. 일반적인 오디오 엔지니어링 권장 순서:

Plugin order significantly affects sound quality. General audio engineering recommended order:

```
마이크 입력 → [1. 게이트] → [2. 디에서] → [3. EQ] → [4. 컴프레서] → [5. 리미터] → 출력
Mic Input  → [1. Gate]  → [2. De-esser] → [3. EQ] → [4. Compressor] → [5. Limiter] → Output
```

| 순서 / Order | 플러그인 / Plugin | 역할 / Role | 왜 이 순서인가 / Why this order |
|---|---|---|---|
| **1** | **노이즈 게이트 / Noise Gate** | 일정 볼륨 이하의 소음 차단 (키보드, 팬 소리) / Blocks noise below threshold (keyboard, fan) | 다른 플러그인이 소음을 증폭하기 전에 먼저 차단 / Block noise before other plugins amplify it |
| **2** | **디에서 / De-esser** | 치찰음(ㅅ, ㅆ) 억제 / Reduces sibilance | EQ/컴프가 치찰음을 강조하기 전에 제거 / Remove before EQ/comp emphasize it |
| **3** | **EQ** | 주파수 밸런스 조정 (저음 컷, 중고음 부스트 등) / Frequency balance (low cut, mid-high boost, etc.) | 깨끗한 소스에서 EQ해야 자연스러움 / EQ on clean source sounds natural |
| **4** | **컴프레서 / Compressor** | 음량 편차 줄임 (큰 소리 억제, 작은 소리 부스트) / Reduces volume variation (tames loud, boosts quiet) | EQ로 정돈된 소리를 일정 볼륨으로 통일 / Unify EQ'd sound to consistent volume |
| **5** | **리미터 / Limiter** | 최대 볼륨 제한 (클리핑 방지) / Caps maximum volume (prevents clipping) | 마지막 안전장치 — 출력이 0dB를 넘지 않도록 / Final safety net — keeps output below 0dB |

**간단 버전 (3개만) / Simple setup (3 plugins):**
```
[노이즈 제거 / Noise Removal] → [EQ] → [컴프레서 / Compressor]
```

**방송용 풀 체인 / Broadcast full chain:**
```
[AI 노이즈 제거 / AI Noise Removal (RNNoise)] → [게이트 / Gate] → [디에서 / De-esser] → [EQ] → [컴프레서 / Compressor] → [리미터 / Limiter]
```

> **Tip**: 같은 종류를 여러 개 쓰면 세밀한 조정이 가능합니다 (예: EQ 2개 = 서지컬 EQ + 톤 셰이핑 EQ). 하지만 CPU 사용량이 늘어나므로 하단 상태 바의 CPU %를 확인하세요.
>
> **Tip**: Using multiple instances of the same type enables finer control (e.g., 2 EQs = surgical + tone shaping). Watch CPU % in the status bar.

> **체인 순서 변경**: 플러그인을 **드래그 앤 드롭**으로 순서를 바꿀 수 있습니다. Ctrl+Shift+0 (마스터 Bypass)으로 체인 전체 ON/OFF를 비교하면서 순서를 조정하세요. / Reorder plugins by **drag and drop**. Use Ctrl+Shift+0 (master bypass) to A/B compare the entire chain while adjusting order.

### VST 스캐너 / Plugin Scanner

- **별도 프로세스**에서 안전하게 스캔 — DirectPipe가 멈추지 않음 / Scans in a **separate process** — DirectPipe won't freeze
- 크래시를 유발하는 플러그인은 자동 **블랙리스트** 등록 (최대 10회 재시도) / Crash-causing plugins auto-**blacklisted** (up to 10 retries)
- **검색/정렬**: 이름·벤더·포맷으로 실시간 필터링 및 컬럼 정렬 / **Search/sort**: real-time filter and column sort by name, vendor, format
- 스캔 로그 / Scan log:
  - Windows: `%AppData%/DirectPipe/scanner-log.txt`
  - macOS: `~/Library/Application Support/DirectPipe/scanner-log.txt`
  - Linux: `~/.config/DirectPipe/scanner-log.txt`

Out-of-process scanner — crashes won't affect DirectPipe. Blacklists problematic plugins. Search and sort by name, vendor, or format.

---

## 퀵 프리셋 슬롯 (A~E) / Quick Preset Slots

5개의 VST 체인 구성을 빠르게 저장하고 전환할 수 있습니다. / Save and switch between 5 VST chain configurations instantly.

- **슬롯 클릭** → 비어있으면 현재 체인 저장, 차있으면 해당 슬롯 로드 / **Click slot** → saves current chain if empty, loads slot if occupied
- **같은 플러그인** → 파라미터만 변경되어 **즉시 전환** / **Same plugins** → only parameters change for **instant switch**
- **다른 플러그인** → 백그라운드 **비동기 로딩** (Keep-Old-Until-Ready: 로딩 중 이전 체인이 오디오 처리를 유지하여 **끊김 없이** 전환) — Async loading with Keep-Old-Until-Ready: old chain keeps processing audio during loading for **seamless** transition
- **활성 슬롯**: 보라색 / Active slot: purple — **사용 중**: 밝은색 / In use: bright — **빈 슬롯**: 어두운색 / Empty slot: dark
- **Save Preset / Load Preset** → `.dppreset` 파일로 내보내기/불러오기 / Export/import as `.dppreset` file
- **슬롯 우클릭 → 이름 변경** — "Rename" 메뉴로 슬롯에 이름 부여 (예: "게임", "토크"). 버튼에 `A|게임` 형식으로 표시. 최대 8자, 초과 시 ".." 생략. Clear로 이름 제거 — Right-click slot → "Rename" to set a custom name (e.g., "Game", "Talk"). Displayed as `A|Game` on the button. Max 8 chars, truncated with "..". Clear to remove name
- **슬롯 우클릭 → 복제** — "Copy A → B/C/D/E" 메뉴로 슬롯 간 플러그인 체인 복제 (활성 슬롯은 라이브 상태 캡처 후 복제, 이름도 함께 복사) — Right-click slot → "Copy" submenu to duplicate chain to another slot (active slot captures live state before copy, name is also copied)
- **슬롯 우클릭 → 삭제** — "Delete" 메뉴로 슬롯 데이터 삭제. 활성 슬롯 삭제 시 로드된 체인도 자동 클리어. 이름도 초기화 — Right-click slot → "Delete" to clear slot data and name. Deleting the active slot also clears the loaded chain
- **슬롯 우클릭 → 내보내기** — "Export" 메뉴로 개별 슬롯을 `.dppreset` 파일로 저장. 다른 사람에게 프리셋 전달 가능 — Right-click slot → "Export" to save individual slot as `.dppreset` file. Share presets with others
- **슬롯 우클릭 → 가져오기** — "Import" 메뉴로 `.dppreset` 파일을 선택한 슬롯에 로드. 빈 슬롯에서도 사용 가능 — Right-click slot → "Import" to load a `.dppreset` file into the selected slot. Works on empty slots too

슬롯은 **체인 데이터 + 이름**을 저장합니다 (플러그인, 순서, Bypass, 파라미터, 슬롯 이름). 오디오/출력 설정은 영향 없음.

Slots store **chain data + name** (plugins, order, bypass, parameters, slot name). Audio/output settings are not affected.

**활용 예시 / Example setup:**
- **A|게임**: 노이즈 제거만 — Noise removal only
- **B|노래방**: 리버브 + 컴프레서 — Reverb + compressor
- **C|회의**: 노이즈 제거 + EQ — Noise removal + EQ
- **D|방송**: 풀 체인 (게이트 + 디에서 + EQ + 컴프) — Full chain (gate + de-esser + EQ + comp)

---

## Auto 모드 (내장 프로세서) / Auto Mode (Built-in Processors)

VST 플러그인 없이 기본적인 마이크 처리를 제공하는 내장 프로세서 3종. / 3 built-in processors for basic microphone processing without VST plugins.

### [Auto] 버튼 / Auto Button

입력 게인 슬라이더 옆의 **[Auto]** 버튼은 A-E와 별도인 **특수 프리셋 슬롯**(인덱스 5)입니다. / The **[Auto]** button next to input gain is a **special preset slot** (index 5), separate from A-E.

- **첫 클릭**: Filter + Noise Removal + Auto Gain 기본 체인 생성 / First click: creates default chain
- **이후 클릭**: 마지막 저장된 Auto 슬롯 상태 로드 / Subsequent: loads saved Auto preset
- **우클릭 → Reset to Defaults**: 기본 3개 프로세서로 초기화 / Right-click to reset
- **활성 시 녹색** 표시, A-E 슬롯은 비선택 상태로 전환 / Green when active, A-E deselected
- Next/Previous 프리셋 순환에서 **제외** (A→B→C→D→E→A만 순환) / Excluded from preset cycling

### 내장 프로세서 / Built-in Processors

일반 VST 플러그인과 동일하게 체인에 추가/제거/바이패스/순서 변경 가능. "Add Plugin" → Built-in 메뉴에서 개별 추가도 가능. / Work like regular VST plugins. Also addable individually via "Add Plugin" → Built-in submenu.

| 프로세서 | 기능 | 기본 설정 |
|---------|------|----------|
| **Filter** | HPF (하이패스) + LPF (로우패스) | HPF ON 60Hz, LPF OFF |
| **Noise Removal** | RNNoise AI 노이즈 제거 + VAD 게이팅 | Standard (VAD 0.70), Hold 300ms |
| **Auto Gain** | LUFS 기반 자동 볼륨 레벨러 | Target -15 LUFS, Max Gain 22 dB |

### Noise Removal 강도 프리셋 / Strength Presets

| 프리셋 | VAD 임계값 | 특징 |
|--------|-----------|------|
| 약 (Light) | 0.50 | 최소 게이팅. 잔여 노이즈 약간 남지만 음성 손실 최소 |
| **중 (Standard)** | **0.70** | **기본값**. 대부분의 환경에 적합 |
| 강 (Aggressive) | 0.90 | 강한 게이팅. 매우 깨끗하지만 작은 소리 잘릴 수 있음 |

고급 설정에서 VAD Threshold를 직접 조절 가능. / Advanced: adjust VAD threshold directly.

### Auto Gain 설정 / Auto Gain Settings

- **Target LUFS**: 목표 라우드니스 (-24 ~ -6 LUFS, 기본 -15)
- **Freeze Level**: 이 레벨 이하의 블록은 게인 유지 (현재 게인을 그대로 유지, 숨소리/키보드 증폭 방지, 기본 -45 dBFS)
- **Low/High Correct**: hold↔full correction 블렌드 비율 (기본 50%/90%). 0=현재 게인 유지, 1=완전 보정
- **Max Gain**: 최대 증폭 한도 (기본 22 dB)

> **참고**: 48kHz가 아닌 샘플레이트에서는 Noise Removal이 비활성화됩니다 (향후 리샘플링 지원 예정).
> **Note**: Noise Removal is inactive at non-48kHz sample rates (resampling support planned).

### 내장 프로세서 상세 가이드 / Built-in Processor Details

플러그인 체인에서 각 내장 프로세서를 클릭하면 편집 패널이 열립니다. / Click any built-in processor in the chain to open its edit panel.

#### Filter (하이패스 + 로우패스 필터 / HPF + LPF)

- **HPF (High-Pass Filter)**: 저음역 잡음 제거 (에어컨 소음, 저주파 험 등). 기본값 60Hz ON / Removes low-frequency noise. Default: 60Hz ON
  - 프리셋 / Presets: Off, 40Hz, 60Hz, 80Hz, 100Hz, 120Hz, Custom
  - Custom 선택 시 슬라이더로 20-500Hz 범위 조절 / Custom: slider for 20-500Hz range
- **LPF (Low-Pass Filter)**: 고음역 잡음 제거 (치찰음, 고주파 잡음 등). 기본값 16kHz OFF / Removes high-frequency noise. Default: 16kHz OFF
  - 프리셋 / Presets: Off, 8kHz, 12kHz, 16kHz, 20kHz, Custom
  - 대부분 OFF 또는 16kHz 권장. 음성 위주라면 12kHz도 적합 / OFF or 16kHz recommended. 12kHz fine for voice-only

#### Noise Removal (노이즈 제거 / RNNoise)

AI 기반 노이즈 억제. 배경 소음(키보드, 팬, 환경 소음)을 실시간 제거합니다. / AI noise suppression. Removes background noise in real-time.

- **Strength (강도)**: 3단계 프리셋 / 3 presets
  - **Light (가벼움)**: VAD 0.50. 약한 배경 소음 제거. 음성 왜곡 최소 / Mild noise. Minimal voice distortion
  - **Standard (표준)**: VAD 0.70. 일반적인 사용에 권장 (**기본값**) / Typical use (**default**)
  - **Aggressive (강력)**: VAD 0.90. 시끄러운 환경. 작은 목소리가 잘릴 수 있음 / Noisy environments. May clip quiet speech
- ⚠️ **48kHz 전용**: Audio 탭에서 샘플레이트를 48kHz로 설정하세요. 다른 SR에서는 자동 패스스루됩니다 / **48kHz only**: set SR to 48kHz in Audio tab. Auto-bypassed at other rates

#### Auto Gain (자동 게인 / LUFS AGC)

LUFS 기반 자동 볼륨 조절. 작은 목소리를 올리고 큰 소리를 줄여 일정한 출력 레벨을 유지합니다. / LUFS-based automatic volume leveling.

| 파라미터 / Parameter | 기본값 / Default | 범위 / Range | 설명 / Description |
|---|---|---|---|
| **Target LUFS** | -15.0 | -24 ~ -6 | 목표 라우드니스. 방송용 -14~-16 권장 / Target loudness. -14~-16 for streaming |
| **Freeze Level** | -45 dBFS | — | 이 레벨 이하 입력 시 현재 게인을 유지 (무음 증폭 방지) / Holds current gain below this level (prevents silence amplification) |
| **Low Correct** | 0.50 | 0.0 ~ 1.0 | 부스트 블렌드: 0=현재 게인 유지, 1=완전 보정 / Boost blend: 0=hold current gain, 1=full correction |
| **High Correct** | 0.90 | 0.0 ~ 1.0 | 컷 블렌드: 0=현재 게인 유지, 1=완전 보정 / Cut blend: 0=hold current gain, 1=full correction |
| **Max Gain** | 22 dB | — | 최대 부스트 한계. 과도한 증폭 방지 / Maximum boost limit |

**권장 설정 / Recommended Settings:**
- 일반 방송 / General streaming: Target -15, Low 0.50, High 0.90, Max Gain 22 dB
- 조용한 환경 / Quiet room: Target -14, Low 0.60, High 0.80, Max Gain 18 dB
- 게임 + 음성 / Gaming + voice: Target -16, Low 0.40, High 0.80, Max Gain 22 dB

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

> 모니터는 메인 드라이버와 **독립된 별도 출력 장치**를 사용합니다. Windows에서는 별도 WASAPI 장치, macOS에서는 별도 CoreAudio 장치를 사용합니다. ASIO 모드(Windows)에서도 정상 동작합니다.
>
> Monitor uses a **separate output device**, independent from the main driver. On Windows it uses a separate WASAPI device; on macOS, a separate CoreAudio device. Works even with ASIO (Windows).

> **⚠️ 모니터 지연(레이턴시) 안내**: 모니터 출력은 별도 오디오 장치를 경유하기 때문에 메인 출력 대비 **~15-20ms 추가 지연**이 발생합니다. 이는 듀얼 디바이스 구조의 하한선이며, 소프트웨어로 줄일 수 없는 한계입니다. 실시간 모니터링 시 지연이 거슬린다면 다음을 권장합니다:
> - **ASIO 드라이버 사용 (Windows)** — 입력과 출력이 하나의 ASIO 디바이스에서 처리되어 별도 모니터 장치 없이 최소 지연으로 자기 목소리를 들을 수 있습니다
> - **하드웨어 다이렉트 모니터링** — 오디오 인터페이스 자체의 Direct Monitor 기능을 사용하면 컴퓨터를 거치지 않아 지연이 0입니다
>
> **⚠️ Monitor latency note**: Monitor output routes through a separate audio device, adding **~15-20ms extra latency** compared to the main output. This is the inherent floor of the dual-device architecture and cannot be reduced by software. If monitoring latency is noticeable, consider:
> - **ASIO driver (Windows)** — Input and output share a single ASIO device, so you can hear yourself with minimal latency without a separate monitor device
> - **Hardware direct monitoring** — Use your audio interface's built-in Direct Monitor feature for zero latency (bypasses the computer entirely)

### VST Receiver (IPC) / VST 리시버

- **Enable VST Receiver Output** — IPC 출력 켜기/끄기 (공유 메모리로 DirectPipe Receiver에 오디오 전송) / Toggle IPC output on/off (sends audio to DirectPipe Receiver via shared memory)
- 기본값은 **꺼짐(OFF)**. DirectPipe Receiver를 사용하는 앱(예: OBS)이 있을 때만 켜면 됩니다 / Default is **OFF**. Only enable when an app (e.g., OBS) is using DirectPipe Receiver
- 메인 화면 **VST** 버튼, MIDI, Stream Deck, HTTP API, 사용자 정의 단축키로도 제어 가능 / Also controllable via main **VST** button, MIDI, Stream Deck, HTTP API, or user-defined hotkey
- **샘플레이트 불일치 경고**: DirectPipe의 SR과 호스트 앱(OBS 등)의 SR이 다르면 Receiver 플러그인 GUI에 경고 메시지가 표시됩니다. SR이 다르면 피치/속도가 변합니다. 호스트 앱의 샘플레이트를 DirectPipe Audio 탭과 동일하게 맞춰주세요.
- **SR mismatch warning**: If DirectPipe SR differs from the host app (e.g., OBS) SR, a warning is shown in the Receiver plugin GUI. Mismatched SR causes pitch/speed artifacts. Match the host app's audio sample rate to the DirectPipe Audio tab setting.

### 녹음 / Recording

처리된 오디오(VST 체인 이후)를 WAV 파일로 녹음합니다. / Record post-chain audio to WAV.

| 기능 / Feature | 설명 / Description |
|---|---|
| **REC / STOP** | 녹음 시작/중지 / Start/stop recording |
| **경과 시간 / Elapsed** | mm:ss 형식으로 표시 / Displayed in mm:ss format |
| **Play** | 마지막 녹음 파일을 기본 플레이어로 재생 / Play last recording in default player |
| **Open Folder** | 녹음 폴더를 파일 관리자에서 열기 / Open recording folder in file manager |
| **... (폴더 변경 / Change folder)** | 녹음 폴더 변경 (자동 저장) / Change recording folder (auto-saved) |

- **기본 폴더 / Default folder**: `Documents/DirectPipe Recordings`
- **파일명 / Filename**: `DirectPipe_YYYYMMDD_HHMMSS.wav`
- **외부 제어 / External control**: Stream Deck (경과 시간 표시 / elapsed time display), HTTP API (`/api/recording/toggle`), WebSocket (`recording_toggle`)
- 녹음은 lock-free(실시간 안전) — 오디오 처리 성능에 영향 없음 / Recording is lock-free (RT-safe) — no impact on audio performance

---

## IPC 출력 & DirectPipe Receiver / IPC Output & DirectPipe Receiver

### OBS에서 Receiver VST2 사용하기 / Using Receiver VST2 in OBS

OBS에서는 DirectPipe Receiver (VST2)를 사용하면 **가상 케이블 없이** DirectPipe 오디오를 직접 받을 수 있습니다.

> **중요**: DirectPipe Receiver는 **입력 버스가 없는 출력 전용 플러그인**입니다. OBS 필터 체인에서 Receiver 앞에 있는 오디오 소스나 다른 필터의 오디오는 **모두 무시됩니다**. Receiver는 DirectPipe에서 IPC(공유 메모리)로 전송된 오디오만 출력합니다. 따라서 OBS의 마이크 입력이나 다른 오디오 필터는 Receiver에 영향을 주지 않습니다 — DirectPipe에서 VST 체인을 거쳐 처리된 최종 오디오만 받게 됩니다.
>
> **Important**: DirectPipe Receiver is an **output-only plugin with no input bus**. Any audio from the OBS audio source or filters before the Receiver in the filter chain is **completely ignored**. The Receiver only outputs audio sent from DirectPipe via IPC (shared memory). This means OBS microphone input or other audio filters have no effect on the Receiver — you only get the final processed audio from DirectPipe's VST chain.

설치 및 설정 방법은 위의 [Receiver VST2 설치 및 OBS 설정 상세 가이드](#receiver-vst2-설치-및-obs-설정-상세-가이드--receiver-vst2-installation--obs-setup) 참조.

For installation and setup, see the [detailed Receiver VST2 guide](#receiver-vst2-설치-및-obs-설정-상세-가이드--receiver-vst2-installation--obs-setup) above.

### IPC 켜기/끄기 (5가지 방법) / Toggle IPC (5 ways)

1. 메인 화면 하단 **VST** 버튼 / **VST** button at the bottom of main screen
2. Output 탭 → **Enable VST Receiver Output** 체크 / Output tab → **Enable VST Receiver Output** checkbox
3. 사용자 정의 단축키 (Controls > Hotkeys에서 IPC Toggle 액션에 단축키 할당) / User-defined hotkey (assign a key combo to IPC Toggle action in Controls > Hotkeys)
4. Stream Deck **IPC Toggle** 버튼 / Stream Deck **IPC Toggle** button
5. HTTP API: `curl http://localhost:8766/api/ipc/toggle`

### Receiver 버퍼 크기 설정 / Receiver Buffer Size

Receiver 플러그인 GUI에서 Buffer 드롭다운으로 선택합니다. / Select in Receiver plugin GUI.

| 프리셋 / Preset | 지연 / Latency | 권장 / Recommended |
|---|---|---|
| **Ultra Low** | ~5ms | 최소 지연 (끊김 가능성) / Minimum latency (may crackle) |
| **Low** (기본 / default) | ~10ms | 대부분의 상황 / Most situations |
| **Medium** | ~21ms | 안정적 / Stable |
| **High** | ~42ms | CPU 부하 높을 때 / High CPU load |
| **Safe** | ~85ms | 끊김 자주 발생 시 / Frequent crackling |

> DirectPipe와 OBS의 **샘플레이트를 동일하게** 맞추세요 (예: 둘 다 48000Hz).
> Match the **sample rates** of DirectPipe and OBS (e.g., both 48000Hz).

---

## 시스템 트레이 / 메뉴 바 / System Tray / Menu Bar

DirectPipe는 시스템 트레이(Windows/Linux) 또는 메뉴 바(macOS)에 상주합니다. X 버튼은 종료가 아닌 **트레이/메뉴 바 최소화**입니다.

DirectPipe is a tray-resident app (Windows/Linux: system tray, macOS: menu bar). The X button **minimizes to tray/menu bar**, not quit.

| 동작 / Action | 방법 / How |
|---|---|
| **창 복원** / Restore | 트레이/메뉴 바 아이콘 클릭 / Click tray/menu bar icon (Windows: double-click) |
| **메뉴** / Menu | 아이콘 우클릭 → Show / Auto Start / Quit / Right-click icon (macOS: click) |
| **완전 종료** / Quit | 메뉴 → "Quit DirectPipe" / Menu → "Quit DirectPipe" |

**트레이 아이콘 고정하기 / Pin tray icon:**

- **Windows 11**: 설정 → 개인 설정 → 작업 표시줄 → 기타 시스템 트레이 아이콘 → DirectPipe **켬** / Settings → Personalization → Taskbar → Other system tray icons → DirectPipe **On**
- **macOS**: 메뉴 바에 자동 표시 / Automatically shown in menu bar
- **Linux**: 데스크탑 환경에 따라 다름 (GNOME은 AppIndicator 확장 필요) / Depends on desktop environment (GNOME needs AppIndicator extension)

### 자동 시작 / Auto Start

트레이 메뉴 또는 Settings 탭에서 설정. 시스템 시작 시 트레이/메뉴 바에서 자동 실행됩니다. / Toggle via tray menu or Settings tab. Auto-starts in tray/menu bar on system startup.

- **Windows**: 레지스트리 (`HKCU\...\Run`)를 통한 시작 프로그램 등록 / Registry-based startup entry
- **macOS**: Launch Agent를 통한 로그인 항목 / Login item via Launch Agent
- **Linux**: `~/.config/autostart/` XDG 자동 시작 / XDG autostart entry

### 포터블 모드 / Portable Mode

DirectPipe 실행 파일 옆에 빈 `portable.flag` 파일을 만들면 포터블 모드가 활성화됩니다. 설정이 시스템 기본 경로 대신 실행 파일 옆 `./config/` 폴더에 저장되어 USB 메모리 등에서 휴대 사용이 가능합니다.

Place an empty `portable.flag` file next to the DirectPipe executable to activate portable mode. Config is stored in `./config/` next to the executable instead of the system default location, making it suitable for USB drives.

> **기본 설정 경로 / Default config paths**: Windows: `%AppData%/DirectPipe/`, macOS: `~/Library/Application Support/DirectPipe/`, Linux: `~/.config/DirectPipe/`

**설정 방법 / Setup:**

1. [`portable.flag` 다운로드](../tools/portable.flag)하여 DirectPipe 실행 파일이 있는 폴더에 넣습니다 / [Download `portable.flag`](../tools/portable.flag) and place it next to the DirectPipe executable

   또는 직접 빈 파일을 만들어도 됩니다 (내용 없어도 됨) / Or create an empty file manually (contents don't matter):
   ```bash
   # Windows
   echo. > portable.flag
   # macOS / Linux
   touch portable.flag
   ```

2. DirectPipe를 실행하면 자동으로 `./config/` 폴더가 생성됩니다 / Launch DirectPipe and the `./config/` folder is created automatically

3. 상태 바 좌측 하단에 보라색 **"Portable"** 표시가 나타나면 정상 / A purple **"Portable"** indicator appears in the bottom status bar

**폴더 구조 / Folder Structure:**
```
DirectPipe/
├── DirectPipe(.exe/.app)     ← 실행 파일 / executable
├── portable.flag              ← 이 파일이 포터블 모드를 활성화 / activates portable mode
└── config/                    ← 자동 생성 / auto-created
    ├── settings.dppreset      ← 오디오/출력 설정 / audio & output settings
    ├── directpipe-controls.json  ← 핫키/MIDI/서버 설정 / hotkey, MIDI, server config
    ├── plugin-cache.xml       ← 플러그인 스캔 캐시 / plugin scan cache
    ├── scanner-log.txt        ← 스캐너 로그 / scanner log
    └── Slots/                 ← 프리셋 슬롯 A-E / preset slots A-E
        ├── slot_0.dppreset
        ├── slot_1.dppreset
        └── ...
```

**포터블 모드에 포함되지 않는 항목 / Not included in portable mode:**

| 항목 / Item | 경로 / Path | 비고 / Note |
|---|---|---|
| 녹음 파일 / Recording files | `Documents/DirectPipe Recordings/` | 별도 관리 / Managed separately |

> **참고 / Note:** 포터블 모드를 해제하려면 `portable.flag` 파일을 삭제하면 됩니다. 기존 `./config/` 폴더는 그대로 남지만, 앱은 시스템 기본 경로의 설정을 사용합니다. / To disable portable mode, simply delete the `portable.flag` file. The `./config/` folder remains but the app will use the system default config location instead.

#### 다중 인스턴스 / Multi-Instance

포터블 모드에서는 여러 DirectPipe를 동시에 실행할 수 있습니다. 단축키·MIDI·WebSocket·HTTP 등 외부 제어는 하나의 인스턴스만 사용하며, 나머지는 오디오만 처리합니다.

In portable mode, multiple DirectPipe instances can run simultaneously. External controls (hotkeys, MIDI, WebSocket, HTTP) are owned by one instance only — others run in audio-only mode.

| 먼저 실행 / Running First | 나중 실행 / Launched Second | 나중 실행 상태 / Second Instance State |
|---|---|---|
| 일반 설치 / Normal | 일반 설치 / Normal | ❌ 차단 (실행 불가) / Blocked (cannot run) |
| 일반 설치 / Normal | 포터블 / Portable | 🔊 Audio Only (외부 제어 없음) / Audio Only (no external control) |
| 포터블 (제어 중) / Portable (controlling) | 일반 설치 / Normal | ❌ 차단 (다이얼로그 → 종료) / Blocked (dialog → quit) |
| 포터블 (제어 중) / Portable (controlling) | 포터블 / Portable | 🔊 Audio Only / Audio Only |
| 일반 설치 / Normal | 일반 설치 (동일 폴더) / Normal (same folder) | ❌ 차단 (중복 감지) / Blocked (duplicate detected) |

> **Audio Only 모드**: 오디오 처리만 가능. WebSocket/HTTP/Hotkey 서버가 시작되지 않으므로 Stream Deck, 핫키, API 제어 불가.
> **Audio Only mode**: Audio processing works but WebSocket/HTTP/Hotkey servers don't start. No Stream Deck, hotkey, or API control.

**UI 표시 / UI Indicators:**

- **타이틀 바 / Title bar**: Audio Only 모드일 때 "DirectPipe (Audio Only)" 표시 / Shows "DirectPipe (Audio Only)" in audio-only mode
- **상태 바 / Status bar**: 포터블 모드 표시 — 보라색 "Portable" (제어 활성) 또는 주황색 "Portable" (Audio Only) / Purple "Portable" (controls active) or orange "Portable" (Audio Only)
- **트레이 툴팁 / Tray tooltip**: "DirectPipe (Portable)" 또는 "DirectPipe (Portable/Audio Only)" 표시 / Shows mode in tooltip
- **Settings 탭 / Settings tab**: **Quit** 버튼으로 개별 인스턴스 종료 가능 / **Quit** button to close individual instance

> **참고 / Note:** 같은 폴더에서 두 번 실행하면 기존처럼 중복 실행이 차단됩니다. 다중 인스턴스는 서로 다른 폴더의 포터블 복사본에서만 가능합니다. / Running twice from the same folder is still blocked. Multi-instance only works with portable copies in different folders.

---

## 뮤트 컨트롤 / Mute Controls

메인 화면 하단에 3개의 뮤트 인디케이터와 PANIC MUTE 버튼이 있습니다.

3 mute indicators and PANIC MUTE button at the bottom of the main screen.

| 버튼 / Button | 기능 / Function | 초록색 / Green | 빨간색 / Red |
|---|---|---|---|
| **OUT** | 메인 출력 뮤트 / Main output mute | 출력 정상 / Output normal | 뮤트됨 / Muted |
| **MON** | 모니터 출력 / Monitor output | 모니터 켜짐 / Monitor on | 모니터 꺼짐 / Monitor off |
| **VST** | IPC 출력 (Receiver) / IPC output (Receiver) | IPC 켜짐 / IPC on | IPC 꺼짐 / IPC off |

### PANIC MUTE / 패닉 뮤트

**전체 출력을 즉시 뮤트**합니다. 갑자기 큰 소리가 나거나 피드백이 발생했을 때 사용하세요.

- 패닉 뮤트 중에는 OUT/MON/VST 버튼과 **모든 외부 제어가 잠금**됩니다 / During panic mute, OUT/MON/VST buttons and **all external controls are locked**
- **녹음 중이면 자동 중지**됩니다 (해제 시 자동 재시작 안 함) / **Active recording is automatically stopped** (does not auto-restart on unmute)
- 해제하면 이전 상태(모니터 켜짐/꺼짐, 출력 뮤트 등)가 **자동 복원**됩니다 / Previous state (monitor on/off, output mute, etc.) is **auto-restored** on unmute
- 단축키 / Shortcut: **Ctrl+Shift+M**

Immediately mutes all outputs and stops active recording. During panic mute, all buttons and external controls are locked — only PanicMute/unmute can change state. Previous state is restored on unmute (recording does not auto-restart).

### 활용 가이드 / Usage Guide

DirectPipe의 3개 출력(OUT/VST/MON)은 각각 **독립적으로 제어**됩니다. 뮤트, 볼륨, 프리셋을 조합하면 다양한 워크플로우를 구성할 수 있습니다.

DirectPipe's 3 outputs (OUT/VST/MON) are each **independently controlled**. Combine mute, volume, and presets for flexible workflows.

```
USB 마이크 → DirectPipe (VST 이펙트 체인)
    ├─ OUT → 가상 케이블 → Discord/Zoom (음성 통화)
    ├─ VST → IPC 공유 메모리 → OBS Receiver (방송/녹화)
    ├─ MON → 헤드폰 (셀프 모니터링)
    └─ REC → WAV 파일 (녹음)
```

---

#### 시나리오 1: 듀얼 앱 설정 (Discord + OBS 동시 사용) / Dual App Setup

하나의 마이크로 **Discord 통화와 OBS 방송을 동시에** 운영할 수 있습니다. 각 앱으로 가는 오디오를 개별적으로 켜고 끌 수 있다는 것이 핵심입니다.

Run **Discord voice chat and OBS streaming simultaneously** from a single mic. The key advantage is independent on/off control for each app's audio feed.

| 출력 / Output | 경로 / Path | 용도 / Purpose |
|---|---|---|
| **OUT** | DirectPipe → 가상 케이블 / Virtual Cable → Discord | 음성 통화 마이크 / Voice chat mic |
| **VST** | DirectPipe → IPC → OBS Receiver | 방송/녹화 마이크 / Stream/recording mic |
| **MON** | DirectPipe → 헤드폰 / Headphones | 자기 목소리 확인 / Self-monitoring |

> **DirectPipe Receiver는 입력 버스가 없는 출력 전용 플러그인**입니다. OBS 오디오 소스의 마이크 입력이나 앞단 필터는 무시되고, DirectPipe에서 IPC로 전송된 오디오만 출력됩니다. / DirectPipe Receiver is an output-only plugin with no input bus — it ignores the OBS source's own mic input and only outputs audio sent from DirectPipe via IPC.

---

#### 시나리오 2: 출력별 개별 뮤트 / Independent Mute per Output

**OBS만 뮤트 (Discord 유지)** — 방송 중 잠시 마이크를 끄고 싶지만 통화는 계속할 때

Mute OBS only (keep Discord) — mute stream mic while keeping voice chat active

| 버튼 / Button | 상태 / State | 결과 / Result |
|---|---|---|
| **OUT** | 🟢 ON | Discord 통화 유지 / Discord chat active |
| **VST** | 🔴 OFF | OBS 방송 마이크 뮤트 / OBS stream mic muted |
| **MON** | 🟢 ON | 헤드폰 모니터 유지 / Headphone monitor active |

**Discord만 뮤트 (OBS 유지)** — 통화 음소거하면서 방송에서는 계속 말할 때

Mute Discord only (keep OBS) — mute voice chat while keeping stream mic active

| 버튼 / Button | 상태 / State | 결과 / Result |
|---|---|---|
| **OUT** | 🔴 OFF | Discord 마이크 뮤트 / Discord mic muted |
| **VST** | 🟢 ON | OBS 방송 마이크 유지 / OBS stream mic active |
| **MON** | 🟢 ON | 헤드폰 모니터 유지 / Headphone monitor active |

**모니터만 끄기** — 스피커에서 하울링(피드백)이 발생할 때 모니터만 끄고 출력은 유지

Monitor off only — disable self-monitoring to stop feedback while keeping all outputs active

| 버튼 / Button | 상태 / State | 결과 / Result |
|---|---|---|
| **OUT** | 🟢 ON | Discord 유지 / Discord active |
| **VST** | 🟢 ON | OBS 유지 / OBS active |
| **MON** | 🔴 OFF | 셀프 모니터 끔 / Self-monitor off |

---

#### 시나리오 3: 패닉 뮤트 — 긴급 전체 차단 / Panic Mute — Emergency Kill All

갑작스런 소음, 피드백, 또는 사생활 보호가 필요할 때 **Ctrl+Shift+M 한 번**으로 모든 출력을 즉시 차단합니다.

Press **Ctrl+Shift+M** once to instantly kill all outputs when unexpected noise, feedback, or privacy protection is needed.

| 버튼 / Button | 상태 / State | 결과 / Result |
|---|---|---|
| **PANIC** | 🔴 활성 / Active | OUT + VST + MON **전부 즉시 뮤트 / all instantly muted** |

- 패닉 뮤트 중에는 OUT/MON/VST 버튼과 **모든 외부 제어가 잠금** — 실수로 해제 불가
- **녹음 중이면 자동 중지** — 마이크 관련 출력이 전부 차단되므로 녹음도 함께 중지
- 해제하면 이전 ON/OFF 상태가 **자동 복원** (예: VST만 OFF였다면 그 상태로 돌아감. 단, 녹음은 자동 재시작 안 함)
- During panic mute, all buttons and external controls are **locked** — prevents accidental unmute
- **Active recording is automatically stopped** — all mic-related output is killed, so recording stops too
- On unmute, previous ON/OFF states are **automatically restored** (recording does not auto-restart)

> **사용 예**: 방송 중 갑자기 집 초인종이 울리거나, 피드백 소음이 터졌을 때. 하나씩 끌 필요 없이 한 번에 차단하고, 돌아오면 한 번에 복원. / Example: Doorbell rings during stream, or feedback loop occurs. Kill everything at once, restore everything when you're back.

---

#### 시나리오 4: 볼륨 개별 조절 / Independent Volume Control

각 출력의 볼륨을 **독립적으로** 설정할 수 있습니다. 통화는 작게, 방송은 크게 — 또는 반대로.

Set each output's volume **independently**. Lower for voice chat, louder for stream — or vice versa.

| 출력 / Output | 조절 위치 / Where | 예시 / Example |
|---|---|---|
| **OUT** (Discord) | Audio 탭 (AudioSettings) → Output Volume | 70% — 통화 상대에게 적당한 볼륨 / Comfortable volume for chat partner |
| **VST** (OBS) | IPC는 체인 출력 그대로 전달 / IPC passes chain output as-is | 100% — 방송은 풀 볼륨 / Full volume for stream |
| **MON** (헤드폰 / Headphones) | Output 탭 → Monitor Volume | 30% — 자기 목소리는 작게 / Keep self-monitoring quiet |
| **INPUT** (입력 게인 / Input gain) | 왼쪽 게인 슬라이더 / Left gain slider | 1.2x — 마이크가 작을 때 부스트 / Boost when mic is too quiet |

> **Tip**: Stream Deck+ 다이얼이 있으면 **Volume Control** 액션으로 OUT/MON/INPUT 볼륨을 물리적 노브로 실시간 조절할 수 있습니다. MIDI CC 매핑도 가능합니다. / With a Stream Deck+ dial, use the **Volume Control** action to adjust volumes with a physical knob in real-time. MIDI CC mapping also works.

---

#### 시나리오 5: 상황별 프리셋 전환 / Preset Switching by Activity

퀵 슬롯(A~E)에 상황별 VST 체인을 저장해두면 **원클릭으로 이펙트 설정을 전환**할 수 있습니다. 프리셋 전환은 모든 출력(OUT/VST/MON)에 동시에 적용됩니다.

Save activity-specific VST chains in Quick Slots (A-E) and **switch effects with one click**. Preset changes apply to all outputs (OUT/VST/MON) simultaneously.

| 슬롯 / Slot | 이름 / Name | VST 체인 구성 / VST Chain | 용도 / Use |
|---|---|---|---|
| **A\|게임** | 게임 / Game | 노이즈 게이트 + 노이즈 제거 / Noise gate + noise removal | 게임 중 키보드/마우스 소음 차단 / Block keyboard/mouse noise during gaming |
| **B\|노래** | 노래방 / Karaoke | 리버브 + 컴프레서 + EQ / Reverb + compressor + EQ | 노래 방송 / Singing stream |
| **C\|회의** | 회의 / Meeting | 노이즈 제거 + EQ (보이스 부스트) / Noise removal + EQ (voice boost) | 업무 미팅 / Work meetings |
| **D\|방송** | 풀 체인 / Full chain | 게이트 + 디에서 + EQ + 컴프 + 리미터 / Gate + de-esser + EQ + comp + limiter | 전문 방송 / Professional streaming |
| **E\|ASMR** | ASMR | 게이트(약) + EQ(저음 부스트) + 컴프(부드럽게) / Gate(light) + EQ(bass boost) + comp(gentle) | ASMR 콘텐츠 / ASMR content |

**전환 방법 (5가지) / Switching methods (5 ways):**
- 메인 화면 슬롯 버튼 클릭 / Click slot button on main screen
- 단축키 / Shortcut **Ctrl+Shift+F1~F5**
- Stream Deck **Preset Switch** 버튼 / button
- MIDI 매핑 / MIDI mapping
- HTTP API: `curl http://localhost:8766/api/preset/0` (슬롯 A / Slot A)

> **끊김 없는 전환**: 같은 플러그인 구성이면 파라미터만 변경되어 즉시 전환됩니다. 다른 플러그인이면 **Keep-Old-Until-Ready** — 로딩 중 이전 체인이 오디오를 계속 처리하여 무음 구간 없이 전환됩니다. / **Seamless switching**: Same plugin layout = instant parameter swap. Different plugins = Keep-Old-Until-Ready (old chain keeps processing during background load, no audio gap).

---

#### 시나리오 6: 방송 중 녹음 / Recording While Streaming

OBS 방송 + Discord 통화를 하면서 **동시에 WAV 녹음**을 할 수 있습니다. 녹음은 VST 체인 이후 처리된 오디오를 캡처합니다.

Record to WAV **while streaming on OBS and chatting on Discord**. Recording captures post-chain processed audio.

```
USB 마이크 → DirectPipe (VST 이펙트)
    ├─ OUT → 가상 케이블 → Discord  ✅ 동시
    ├─ VST → IPC → OBS Receiver    ✅ 동시
    ├─ MON → 헤드폰                ✅ 동시
    └─ REC → WAV 파일              ✅ 동시
```

- Output 탭 → Recording → **Record** 버튼 또는 단축키/Stream Deck/HTTP API / Output tab → Recording → **Record** button or shortcut/Stream Deck/HTTP API
- 녹음 파일은 지정된 폴더에 타임스탬프 이름으로 자동 저장 / Recording files auto-saved with timestamp name in designated folder
- 녹음 중 프리셋 전환, 뮤트 제어 모두 독립적으로 가능 / Preset switching and mute controls work independently during recording

> **활용**: 방송 사고 시 증거 보존, 노래 녹음 저장, 팟캐스트 소스 파일 확보 등. / Use cases: preserving stream highlights, saving vocal recordings, podcast source files, etc.

---

#### 시나리오 7: Stream Deck / MIDI로 핸즈프리 제어 / Hands-Free Control

방송 중에는 키보드에서 손을 떼기 어렵습니다. Stream Deck이나 MIDI 컨트롤러를 사용하면 **물리 버튼 하나로** 모든 기능을 제어할 수 있습니다.

During streams, taking hands off the keyboard is difficult. Use Stream Deck or MIDI controllers to control **everything with physical buttons**.

**Stream Deck 레이아웃 예시 / Stream Deck layout example:**

| 버튼 / Button | 액션 / Action | 용도 / Purpose |
|---|---|---|
| 🔴 큰 버튼 / Big button | Panic Mute | 긴급 패닉 뮤트 (전체 출력 차단) / Emergency kill all |
| 🎙️ | IPC Toggle | OBS 마이크 ON/OFF / OBS mic ON/OFF |
| 🔇 | Toggle Mute | Discord 마이크 ON/OFF / Discord mic ON/OFF |
| 🎧 | Monitor Toggle | 헤드폰 모니터 ON/OFF / Headphone monitor ON/OFF |
| A ~ E | Preset Switch | 상황별 프리셋 전환 / Activity-based preset switch |
| 🔄 다이얼 / Dial | Volume Control | 볼륨 실시간 조절 / Real-time volume control |
| ⏺️ | Recording Toggle | 녹음 시작/정지 / Start/stop recording |

**MIDI 컨트롤러 예시 / MIDI controller example:**

| 컨트롤 / Control | 매핑 / Mapping | 용도 / Purpose |
|---|---|---|
| 페이더 / Fader 1 | Output Volume | 메인 출력 볼륨 / Main output volume |
| 페이더 / Fader 2 | Monitor Volume | 모니터 볼륨 / Monitor volume |
| 페이더 / Fader 3 | Input Gain | 입력 게인 / Input gain |
| 노브 / Knob 1 | VST 파라미터 / VST Parameter (EQ Freq) | EQ 주파수 실시간 조절 / Real-time EQ frequency |
| 패드 / Pad 1~5 | Preset Slot A~E | 프리셋 전환 / Preset switch |
| 패드 / Pad 6 | Panic Mute | 긴급 뮤트 / Emergency mute |

> **Tip**: MIDI 탭에서 **[+ Add Param]**으로 VST 플러그인의 개별 파라미터(EQ 주파수, 컴프 쓰레숄드 등)도 MIDI CC에 매핑할 수 있습니다. / Use **[+ Add Param]** in the MIDI tab to map individual VST parameters (EQ frequency, compressor threshold, etc.) to MIDI CC.

---

#### 시나리오 8: 플러그인 Bypass로 빠른 A/B 비교 / Quick A/B with Plugin Bypass

이펙트 적용 전/후를 **즉시 비교**할 수 있습니다. 방송 중에도 실시간으로 가능합니다.

**Instantly compare** before/after effects. Works in real-time, even during streams.

- **개별 Bypass / Individual Bypass**: 플러그인 행의 Bypass 토글 또는 **Ctrl+Shift+1~9** / Bypass toggle on plugin row or **Ctrl+Shift+1~9**
- **마스터 Bypass / Master Bypass**: **Ctrl+Shift+0** — 전체 VST 체인 한 번에 우회 (원본 마이크 소리) / Bypass entire VST chain at once (raw mic sound)
- 모니터(헤드폰)로 실시간 확인하면서 OUT/VST 출력은 계속 유지 / Monitor in real-time via headphones while OUT/VST outputs remain active

> **활용**: EQ 세팅 조정 중 원본과 비교, 노이즈 제거 효과 확인, 컴프레서 세기 미세 조정 시. / Use cases: comparing with original during EQ tweaking, verifying noise removal effect, fine-tuning compressor settings.

---

> **Tip**: 위의 모든 제어(뮤트, 볼륨, 프리셋, Bypass, 녹음)는 **단축키, MIDI, Stream Deck, HTTP API** 4가지 방법으로 조작 가능합니다. 방송 환경에 맞는 방법을 선택하세요. / All controls above (mute, volume, preset, bypass, recording) work via **hotkeys, MIDI, Stream Deck, and HTTP API**. Choose what fits your setup.

---

## 외부 제어 / External Control

DirectPipe는 최소화 상태에서도 다양한 방법으로 제어할 수 있습니다.

DirectPipe can be controlled in multiple ways, even when minimized.

### 키보드 단축키 / Keyboard Shortcuts

모든 단축키는 **Controls > Hotkeys** 탭에서 변경할 수 있습니다. **드래그앤드롭**으로 순서 변경, **[Set]** 버튼으로 단축키 재설정, **[Cancel]** 버튼으로 녹음 취소가 가능합니다.

All shortcuts customizable in Controls > Hotkeys tab. **Drag-and-drop** to reorder, **[Set]** to re-assign key combo, **[Cancel]** to abort recording.

| 단축키 / Shortcut | 동작 / Action |
|---|---|
| Ctrl+Shift+M | 패닉 뮤트 / Panic mute |
| Ctrl+Shift+0 | 마스터 Bypass (전체 체인) / Master bypass (entire chain) |
| Ctrl+Shift+1~3 | 플러그인 1-3 Bypass 토글 / Plugin 1-3 bypass toggle |
| Ctrl+Shift+F6 | 입력 뮤트 (= 패닉 뮤트) / Input mute (= Panic Mute) |
| Ctrl+Shift+H | 모니터 토글 / Monitor toggle |
| Ctrl+Shift+F1~F5 | 프리셋 슬롯 A~E / Preset slots A~E |

> 모든 단축키는 Controls > Hotkeys 탭에서 변경/추가 가능 / All shortcuts are customizable in Controls > Hotkeys tab

### MIDI 제어 / MIDI Control

1. **Controls > MIDI** 탭에서 MIDI 장치 선택 / Select MIDI device in **Controls > MIDI** tab
2. 매핑할 동작 옆의 **[Learn]** 클릭 (진행 중 **[Cancel]**로 취소 가능) / Click **[Learn]** next to the action to map (**[Cancel]** to abort)
3. MIDI 컨트롤러의 노브/버튼/슬라이더 조작 / Move knob/button/slider on your MIDI controller
4. 매핑 자동 저장 / Mapping auto-saved

**매핑 타입 / Mapping types**: Toggle, Momentary, Continuous, NoteOnOff
**핫플러그 / Hot-plug**: MIDI 장치 연결 후 **[Rescan]** 클릭 / Click **[Rescan]** after connecting MIDI device
**HTTP 테스트 / HTTP test**: MIDI 하드웨어 없이 `tools/midi-test.py`로 HTTP API 통해 테스트 가능 / Test via HTTP API with `tools/midi-test.py` without MIDI hardware

#### MIDI 플러그인 파라미터 매핑 / Plugin Parameter Mapping

MIDI CC로 VST 플러그인의 개별 파라미터를 직접 제어할 수 있습니다.

Control individual VST plugin parameters directly via MIDI CC.

1. MIDI 탭에서 **[+ Add Param]** 클릭 / Click **[+ Add Param]** in MIDI tab
2. 팝업에서 **플러그인** 선택 / Select **plugin** from popup
3. 제어할 **파라미터** 선택 / Select **parameter** to control
4. MIDI 컨트롤러 **노브/슬라이더** 조작으로 할당 / Assign by moving MIDI controller **knob/slider**

### Stream Deck

Elgato Marketplace에서 무료 설치 / Free install from Elgato Marketplace: **[DirectPipe Stream Deck Plugin](https://marketplace.elgato.com/product/directpipe-29f7cbb8-cb90-425d-9dbc-b2158e7ea8b3)**

10가지 액션 / 10 actions: Bypass Toggle, Volume Control (SD+ 다이얼 / dial), Preset Switch, Monitor Toggle, Panic Mute, Recording Toggle, IPC Toggle, Performance Monitor, Plugin Parameter (SD+), Preset Bar (SD+)

자세한 내용 / Details: [Stream Deck Guide](STREAMDECK_GUIDE.md)

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
curl http://localhost:8766/api/status
```

전체 엔드포인트 / Full endpoint reference: [Control API Reference](CONTROL_API.md)

### WebSocket

포트 8765에서 양방향 실시간 통신. 상태 변경 시 자동 푸시. / Bidirectional real-time on port 8765 with auto state push.

---

## Settings 탭 / Settings Tab

우측 패널의 4번째 탭. / The 4th tab in the right panel.

### 앱 설정 / Application

- **Auto Start** — 시스템 시작 시 트레이/메뉴 바에서 자동 실행 / Auto-start in system tray/menu bar on system startup (Windows: 레지스트리, macOS: Launch Agent, Linux: XDG autostart)

### 설정 저장/불러오기 / Settings Export/Import

오디오, 출력, 컨트롤 매핑(핫키, MIDI) 설정을 `.dpbackup` 파일로 저장/복원합니다. VST 체인과 프리셋 슬롯은 포함되지 않습니다 (슬롯 A-E에서 별도 관리).

Save/load audio, output, and control settings as `.dpbackup` files. VST chain and preset slots are NOT included (managed separately via slots A-E).

- **Save Settings** → `.dpbackup` 파일로 저장 (설정만, 플랫폼 정보 포함) / Save to `.dpbackup` file (settings only, includes platform info)
- **Load Settings** → `.dpbackup` 파일에서 복원 (설정만). **같은 OS끼리만 가능** — 다른 OS에서 만든 백업은 차단됨 / Restore from `.dpbackup` file (settings only). **Same OS only** — backups from a different OS are blocked

### 로그 뷰어 / Log Viewer

앱의 모든 이벤트(오디오 엔진, 플러그인, WebSocket, HTTP 등)를 실시간으로 확인합니다.

View all app events (audio engine, plugins, WebSocket, HTTP, etc.) in real-time.

- 타임스탬프 + 고정폭 폰트 / Timestamped + monospaced font
- **카테고리 태그 / Category tags**: `[APP]` `[AUDIO]` `[VST]` `[PRESET]` `[ACTION]` `[HOTKEY]` `[MIDI]` `[WS]` `[HTTP]` `[MONITOR]` `[IPC]` `[REC]` `[CONTROL]` — 서브시스템별 구분 / per subsystem
- **Export Log** — `.txt` 파일로 저장 (문제 신고 시 유용) / Save to `.txt` file (useful for bug reports)
- **Clear Log** — 로그 화면 지우기 / Clear the log display

### 유지보수 / Maintenance

모든 작업은 확인 대화상자가 먼저 표시됩니다. / All actions show a confirmation dialog.

| 기능 / Function | 설명 / Description |
|---|---|
| **Full Backup** | 전체 백업 (설정 + VST 체인 + 슬롯 A-E + 컨트롤) → `.dpfullbackup` 파일. 백업에 플랫폼 정보 포함 / Full backup (settings + VST chain + slots A-E + controls) → `.dpfullbackup` file. Includes platform info |
| **Full Restore** | `.dpfullbackup` 파일에서 전체 복원. **같은 OS끼리만 가능** (Windows 백업은 Windows에서만, macOS 백업은 macOS에서만 복원). 다른 OS에서 복원 시 경고 다이얼로그 표시 / Restore from `.dpfullbackup`. **Same OS only** (Windows backup → Windows, macOS → macOS). Cross-OS restore is blocked with a warning dialog |
| **Clear Plugin Cache** | 스캔된 플러그인 목록 삭제 (다음 Scan 시 재스캔) / Delete scanned plugin list (re-scan on next Scan) |
| **Clear All Presets** | 퀵 슬롯 A~E + Auto 슬롯 + 사용자 프리셋 전체 삭제, 현재 체인도 클리어 / Delete all quick slots (A-E + Auto) + user presets, clears active chain |
| **Factory Reset** | 공장 초기화 — 모든 데이터 삭제 (설정, 컨트롤, 프리셋(A-E + Auto), 플러그인 캐시, 녹음 설정) / Factory reset — deletes all data (settings, controls, presets (A-E + Auto), plugin cache, recording config) |

---

## 인앱 업데이트 / In-App Update

DirectPipe는 실행 시 자동으로 GitHub에서 최신 버전을 확인합니다. / Checks for updates on startup.

1. **새 버전 발견** → 하단 credit 라벨에 **"NEW vX.Y.Z"** 주황색 표시 / **New version found** → orange **"NEW vX.Y.Z"** on credit label
2. **라벨 클릭** → 업데이트 다이얼로그 / **Click label** → update dialog:
   - **Update Now** — GitHub에서 자동 다운로드 → exe 교체 → 앱 재시작 / Auto-download from GitHub → replace exe → restart app
   - **View on GitHub** — 브라우저에서 릴리즈 페이지 열기 / Open release page in browser
   - **Later** — 닫기 (다음 실행 시 다시 알림) / Dismiss (reminds again on next launch)
3. **업데이트 완료** → 재시작 후 **"Updated to vX.Y.Z successfully!"** 알림 (보라색) / **Update complete** → purple **"Updated to vX.Y.Z successfully!"** notification after restart

> **인터넷 미연결 시**: 오류 없이 기존 버전이 정상 동작합니다.
>
> **Offline**: No error, app works normally with current version.

---

## 오류 알림 / Error Notifications

상태 바에 비침습적 알림이 자동으로 표시됩니다. / Non-intrusive notifications in the status bar.

| 색상 / Color | 의미 / Meaning | 예시 / Example |
|---|---|---|
| 🔴 **빨간색** / Red | 오류 / Error | 오디오 장치 실패, 플러그인 로드 실패 / Audio device failure, plugin load failure |
| 🟠 **주황색** / Orange | 경고 / Warning | 장치 변경, 설정 문제 / Device change, settings issue |
| 🟣 **보라색** / Purple | 정보 / Info | 업데이트 완료, 설정 저장 / Update complete, settings saved |

심각도에 따라 3~8초 후 자동 사라짐. / Auto-fades after 3-8 seconds depending on severity.

---

## 문제 해결 / Troubleshooting

### 소리가 안 나와요 / No audio

1. **Audio 탭** → Input 장치가 올바른지 확인 / **Audio tab** → verify correct Input device
2. 왼쪽 **INPUT 미터** 확인 — 움직이면 마이크 입력은 정상 / Check left **INPUT meter** — if moving, mic input is OK
3. 미터가 안 움직이면 / If meter doesn't move:
   - **Windows**: 설정 → 개인 정보 → 마이크 접근 허용 확인 / Settings → Privacy → allow microphone access
   - **macOS**: 시스템 설정 → 개인 정보 보호 및 보안 → 마이크 → DirectPipe 허용 / System Settings → Privacy & Security → Microphone → allow DirectPipe
   - **Linux**: PulseAudio/PipeWire 장치 권한 확인 / Check PulseAudio/PipeWire device permissions
   - 다른 앱의 독점 모드 해제 (Windows) / Disable exclusive mode in other apps (Windows)
4. **OUT** 버튼이 초록색인지 확인 (빨간색 = 뮤트) / Check **OUT** button is green (red = muted)
5. **PANIC MUTE** 활성화 여부 확인 / Check if **PANIC MUTE** is active

### 소리가 끊기거나 지연이 커요 / Crackling or high latency

1. Audio 탭 → **Buffer Size** 올리기 (256 → 512) / Audio tab → increase **Buffer Size** (256 → 512)
2. CPU를 많이 쓰는 플러그인 Bypass 처리 / Bypass CPU-heavy plugins
3. 오디오 인터페이스가 있다면 **ASIO** (Windows) 또는 **JACK** (Linux) 드라이버 사용 / Use **ASIO** (Windows) or **JACK** (Linux) driver if you have an audio interface
4. 하단 상태 바 **CPU %** 및 **XRun** 수치 확인 — CPU 60% 이상이면 과부하, XRun은 60초간 버퍼 언더런 횟수 / Check **CPU %** and **XRun** in status bar — CPU above 60% means overload, XRun counts buffer underruns over 60s
5. **Windows Audio (Low Latency)** 에서 끊기면 → **Windows Audio**로 변경 시도 (LL 모드가 제대로 지원되지 않는 장치가 많음, Windows 전용) / If crackling on **Low Latency** → try **Windows Audio** (many devices don't properly support LL mode, Windows only)

### 오디오 장치가 갑자기 끊겼어요 / Audio device disconnected

DirectPipe는 USB 오디오 장치가 분리되면 **자동 재연결**을 시도합니다. / DirectPipe auto-reconnects when USB audio devices are unplugged.

- **분리 시** → 하단 상태 바에 **주황색 경고** 표시 / Orange warning on disconnect
- **재연결 시** → 원하는 장치가 다시 나타나면 자동 재연결 + **보라색 알림** (무기한 대기, 다른 장치로 폴백하지 않음) / Auto-reconnects when desired device reappears + purple notification (waits indefinitely, no fallback)
- **설정 보존** → 샘플레이트, 버퍼 크기, 채널 라우팅 유지 / SR, buffer size, and channel routing preserved
- **모니터 장치** → 메인 장치와 독립적으로 자동 재연결 / Monitor device reconnects independently

> **Tip**: 재연결이 안 되면 Audio 탭에서 장치 콤보박스를 클릭하세요 — 클릭 시 자동으로 장치 목록을 새로고침합니다. / If reconnection fails, click the device combo box in the Audio tab — it auto-refreshes the device list on click.

### 모니터 출력이 안 돼요 / No monitor output

1. Output 탭에서 모니터 **장치 선택** 확인 / Check monitor **device selection** in Output tab
2. **Enable** 토글이 켜져 있는지 확인 / Verify **Enable** toggle is on
3. **MON** 버튼이 초록색인지 확인 / Check **MON** button is green
4. 모니터는 별도 오디오 장치를 사용 (Windows: WASAPI, macOS: CoreAudio) — ASIO 모드(Windows)에서도 동작 / Monitor uses a separate audio device (Windows: WASAPI, macOS: CoreAudio) — works even with ASIO (Windows)

#### 모니터 출력 무음 (SR 불일치) / Monitor Silent (Sample Rate Mismatch)

메인 오디오 장치와 모니터 장치의 샘플레이트가 다르면 모니터 출력이 무음이 됩니다. 알림이 표시됩니다.

If the main audio device and monitor device have different sample rates, monitor output will be silent. A notification is shown.

해결 방법 / Solution:
1. **Windows**: 사운드 설정 → 모니터 장치 → 속성 → 고급 → 기본 형식을 메인 SR과 동일하게 변경 / Sound Settings → Monitor device → Properties → Advanced → Change default format to match main SR
2. **macOS**: Audio MIDI Setup → 모니터 장치 → 포맷을 메인 SR과 동일하게 / Audio MIDI Setup → Match monitor format to main SR
3. 또는 메인 SR을 모니터 장치에 맞추기 / Or match main SR to monitor device

> DirectPipe에서 모니터 장치의 SR을 직접 변경할 수 없습니다 (WASAPI Shared 모드 제한).
> DirectPipe cannot change the monitor device's SR directly (WASAPI Shared mode limitation).

### 플러그인 스캔이 오래 걸려요 / Plugin scan takes long

- 별도 프로세스에서 실행되므로 DirectPipe는 멈추지 않음 / Runs in separate process, DirectPipe won't freeze
- 일부 플러그인은 최대 5분까지 걸릴 수 있음 / Some plugins may take up to 5 minutes
- 크래시 유발 플러그인은 자동 블랙리스트 / Crash-causing plugins auto-blacklisted
- 로그 / Log: Windows `%AppData%/DirectPipe/scanner-log.txt`, macOS `~/Library/Application Support/DirectPipe/scanner-log.txt`, Linux `~/.config/DirectPipe/scanner-log.txt`

### DirectPipe Receiver에서 끊김 / Receiver audio crackling

1. Receiver 플러그인 GUI에서 **Buffer** 한 단계 올리기 / Increase **Buffer** one step in Receiver plugin GUI
2. DirectPipe와 OBS **샘플레이트 동일하게** 맞추기 (48000Hz 권장) / Match DirectPipe and OBS **sample rates** (48000Hz recommended)
3. CPU 사용량 확인 / Check CPU usage

### 오류가 발생했는데 팝업이 없어요 / No error popup

- 하단 **상태 바** 확인 — 알림이 잠시 표시됨 (빨강/주황/보라) / Check bottom **status bar** — notifications appear briefly (red/orange/purple)
- **Settings 탭** → 로그 뷰어에서 전체 이벤트 기록 확인 / **Settings tab** → check full event log in log viewer
- **Export Log**로 로그 저장 후 문제 신고 / Save log with **Export Log** for bug reports

### 내 목소리가 두 번 들려요 (에코) / I hear myself twice (echo)

모니터 출력을 스피커로 재생하면 마이크가 스피커 소리를 다시 수음하여 **에코/피드백 루프**가 발생합니다.

Playing monitor output through speakers creates an **echo/feedback loop** as the mic picks up the speaker sound.

**해결 방법 / Solutions:**

| 원인 / Cause | 해결 / Solution |
|---|---|
| 모니터 + 스피커 동시 사용 / Monitor + speaker together | 모니터는 반드시 **헤드폰**으로 사용 (스피커 X) / Always use **headphones** for monitor (not speakers) |
| Discord에서 "내 목소리 듣기" 활성화 / Discord "hear myself" enabled | Discord 음성 설정 → "내 목소리를 되돌려 듣기" 끄기 / Discord voice settings → disable "echo my audio" |
| OS 마이크 루프백 활성화 / OS mic loopback enabled | **Windows**: 사운드 설정 → 마이크 속성 → "수신 대기" 탭 → 체크 해제 / Sound → mic properties → "Listen" tab → uncheck. **macOS**: 해당 없음 / N/A |
| 가상 케이블 출력이 DirectPipe 입력에 연결 / Virtual cable output connected to DirectPipe input | Audio 탭 Input이 **실제 마이크**인지 확인 (가상 케이블 출력이 아님) / Verify Audio tab Input is your **real mic** (not virtual cable output) |
| OBS 모니터링 활성화 / OBS monitoring enabled | OBS 오디오 소스 → 고급 오디오 속성 → 오디오 모니터링 → "끄기" / OBS audio source → Advanced Audio Properties → Audio Monitoring → "Off" |

> **핵심 원칙**: DirectPipe의 모니터 출력은 **자기 목소리 확인용**입니다. 반드시 **헤드폰/이어폰**으로만 사용하세요. 스피커로 모니터하면 피드백이 발생합니다.
>
> **Key principle**: DirectPipe's monitor output is for **self-monitoring only**. Always use **headphones/earbuds**. Speaker monitoring causes feedback.

### OBS에서 DirectPipe Receiver가 목록에 안 보여요 / DirectPipe Receiver not showing in OBS

1. Receiver 플러그인이 올바른 VST2 폴더에 있는지 확인 / Verify Receiver plugin is in the correct VST2 folder:
   - **Windows**: `C:\Program Files\VSTPlugins\` (권장), `C:\Program Files\Common Files\VST2\`, `C:\Program Files\Steinberg\VstPlugins\`
   - **macOS**: `~/Library/Audio/Plug-Ins/VST/`
   - **Linux**: `~/.vst/` 또는 / or `/usr/lib/vst/`
2. OBS를 **완전히 종료 후 재시작** (트레이에서도 종료) / **Fully restart OBS** (close from tray too)
3. OBS의 **비트** 확인 — DirectPipe Receiver는 **64비트** 플러그인. 32비트 OBS에서는 안 보임 / Check OBS **bitness** — Receiver is a **64-bit** plugin. Won't appear in 32-bit OBS
4. 그래도 안 보이면: OBS → 도구 → 설정 → VST 플러그인 경로에 플러그인이 있는 폴더 추가 / If still missing: OBS → Tools → Settings → add the plugin folder to VST plugin paths

### DirectPipe Receiver가 "Disconnected"로 표시돼요 / Receiver shows "Disconnected"

1. **DirectPipe가 실행 중**인지 확인 (시스템 트레이/메뉴 바 아이콘)
2. DirectPipe 하단의 **VST** 버튼이 **초록색**(IPC ON)인지 확인
3. OBS의 VST 필터에서 **"DirectPipe Receiver"**가 선택되어 있는지 확인
4. DirectPipe를 **먼저 실행**한 뒤 OBS를 시작하면 자동 연결됨
5. OBS가 먼저 실행된 경우: Receiver가 100 오디오 블록마다 자동 재연결을 시도합니다 (버퍼 512 @ 48kHz 기준 ~1초). DirectPipe 실행 후 잠시 대기

1. Check **DirectPipe is running** (system tray / menu bar icon)
2. Check the **VST** button at bottom of DirectPipe is **green** (IPC ON)
3. Verify **"DirectPipe Receiver"** is selected in OBS VST filter
4. Start **DirectPipe first**, then OBS — auto-connects
5. If OBS started first: Receiver auto-retries every ~1 second, just wait after launching DirectPipe

### DirectPipe 종료/크래시 시 각 출력의 동작 / What happens when DirectPipe closes or crashes

| 출력 / Output | DirectPipe 종료 시 / When DirectPipe closes | 복구 방법 / Recovery |
|---|---|---|
| **Main OUT** (가상 케이블 → Discord) | Discord 마이크 즉시 무음 (가상 케이블에 오디오 전송 중단) / Discord mic goes silent immediately (audio to virtual cable stops) | DirectPipe 재시작 → 자동 복구 / Restart DirectPipe → auto-recovery |
| **VST** (IPC → OBS Receiver) | DirectPipe Receiver가 연결 해제 감지 → **부드러운 페이드아웃** (~20 샘플) → 무음 → "Disconnected" 표시 / Receiver detects disconnect → **smooth fadeout** (~20 samples) → silence → "Disconnected" | DirectPipe 재시작 + IPC 켜기 → 자동 재연결 / Restart DirectPipe + enable IPC → auto-reconnect |
| **MON** (모니터 → 헤드폰 / Monitor → Headphones) | 즉시 무음 (오디오 콜백 중단) / Immediate silence (audio callback stops) | DirectPipe 재시작 → 자동 복구 / Restart DirectPipe → auto-recovery |
| **REC** (WAV 녹음 / WAV Recording) | 녹음 파일 자동 마무리 (FIFO 플러시 후 파일 닫기). 녹음 중이었다면 중단 시점까지 저장됨 / Recording file auto-finalized (FIFO flushed, file closed). Saved up to the point of closure | 재시작 후 수동 녹음 시작 / Manually start recording after restart |

> **OBS Receiver 자동 재연결**: DirectPipe를 다시 시작하면 DirectPipe Receiver가 100 오디오 블록마다 공유 메모리 연결을 시도합니다 (버퍼 크기에 따라 ~0.5-2초). DirectPipe에서 IPC를 켜면(VST 버튼 초록) 자동으로 재연결됩니다 — OBS를 재시작할 필요 없음.
>
> **OBS Receiver auto-reconnect**: After restarting DirectPipe, DirectPipe Receiver attempts shared memory connection every ~1 second. Once IPC is enabled (VST button green), it reconnects automatically — no need to restart OBS.

> **Discord 자동 복구**: DirectPipe가 재시작되면 가상 케이블로의 오디오 출력이 자동으로 재개됩니다. Discord에서 별도 조작 없이 마이크가 다시 작동합니다.
>
> **Discord auto-recovery**: When DirectPipe restarts, audio output to the virtual cable resumes automatically. Microphone works again in Discord without any action.

---

## 설정 파일 위치 / Config File Locations

설정 파일의 기본 경로는 플랫폼에 따라 다릅니다. 아래 표에서 `<CONFIG>`는 다음과 같습니다:
- **Windows**: `%AppData%/DirectPipe/`
- **macOS**: `~/Library/Application Support/DirectPipe/`
- **Linux**: `~/.config/DirectPipe/`

Config file base path varies by platform. `<CONFIG>` below refers to the paths listed above.

| 파일 / File | 경로 / Path |
|---|---|
| 기본 설정 / Main settings | `<CONFIG>/settings.dppreset` |
| 프리셋 슬롯 / Preset slots | `<CONFIG>/Slots/` |
| 플러그인 캐시 / Plugin cache | `<CONFIG>/plugin-cache.xml` |
| 스캐너 로그 / Scanner log | `<CONFIG>/scanner-log.txt` |
| 녹음 폴더 / Recording folder | `Documents/DirectPipe Recordings/` |
| 포터블 모드 / Portable mode | `./config/` (실행 파일 옆 `portable.flag` 필요 / requires `portable.flag` next to executable) |

---

## 관련 문서 / Related Documentation

- [Quick Start](QUICKSTART.md) — USB 마이크 5분 설정 가이드 / 5-minute USB mic setup guide
- [Architecture](ARCHITECTURE.md) — 시스템 설계 / System design
- [Build Guide](BUILDING.md) — 소스 빌드 방법 / Build from source
- [Control API](CONTROL_API.md) — WebSocket / HTTP API 레퍼런스
- [Stream Deck Guide](STREAMDECK_GUIDE.md) — Stream Deck 플러그인 상세
