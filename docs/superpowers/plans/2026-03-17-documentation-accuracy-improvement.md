# Documentation Accuracy & Quality Improvement Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix factual inaccuracies, fill content gaps, and improve consistency across all 12 documentation files.

**Architecture:** Documentation-only changes. No code modifications. Each task targets specific files with exact line numbers. Changes are independent and can be committed per-task.

**Tech Stack:** Markdown, bilingual (Korean/English)

---

## Chunk 1: Factual Accuracy Fixes (ARCHITECTURE.md)

### Task 1: Fix action count and list in ARCHITECTURE.md

ARCHITECTURE.md has stale "15 actions" in two places. Actual enum has 19 actions (verified: `ActionDispatcher.h:40-60`).

**Files:**
- Modify: `docs/ARCHITECTURE.md:39` — "15 actions" → "19 actions" + add 4 missing
- Modify: `docs/ARCHITECTURE.md:67` — ActionDispatcher description "15 actions" → "19 actions"

- [ ] **Step 1: Update line 39 — top-level feature bullet**

Replace:
```
- **15 actions** — Unified action system: PluginBypass, MasterBypass, SetVolume, ToggleMute, LoadPreset, PanicMute, InputGainAdjust, NextPreset, PreviousPreset, InputMuteToggle, SwitchPresetSlot, MonitorToggle, RecordingToggle, SetPluginParameter, IpcToggle. / 15개 통합 액션 시스템.
```
With:
```
- **19 actions** — Unified action system: PluginBypass, MasterBypass, SetVolume, ToggleMute, LoadPreset, PanicMute, InputGainAdjust, NextPreset, PreviousPreset, InputMuteToggle, SwitchPresetSlot, MonitorToggle, RecordingToggle, SetPluginParameter, IpcToggle, XRunReset, SafetyLimiterToggle, SetSafetyLimiterCeiling, AutoProcessorsAdd. / 19개 통합 액션 시스템.
```

- [ ] **Step 2: Update line 67 — ActionDispatcher component description**

Replace the "15 actions" count and list in the ActionDispatcher bullet with "19 actions" and append the 4 new actions. Also note XRunReset's HTTP routing anomaly:
```
- **ActionDispatcher** — Central action routing. 19 actions: ...IpcToggle, XRunReset, SafetyLimiterToggle, SetSafetyLimiterCeiling, AutoProcessorsAdd. ... Note: XRunReset via HTTP bypasses ActionDispatcher (direct `engine_.requestXRunReset()` call); all other actions route through ActionDispatcher from both HTTP and WebSocket.
```

- [ ] **Step 3: Verify no other "15 actions" references remain**

Run: `grep -n "15 actions" docs/ARCHITECTURE.md`
Expected: 0 matches

- [ ] **Step 4: Commit**

```bash
git add docs/ARCHITECTURE.md
git commit -m "docs: fix action count 15→19 in ARCHITECTURE.md"
```

---

### Task 2: Fix Stream Deck action count in ARCHITECTURE.md

ARCHITECTURE.md line 154 says "7 SingletonAction subclasses". Actual count is 10 (verified: manifest.json + src/actions/).

**Files:**
- Modify: `docs/ARCHITECTURE.md:154-161`

- [ ] **Step 1: Update Stream Deck section**

Replace lines 154-161:
```
- 7 SingletonAction subclasses: Bypass Toggle, Panic Mute, Volume Control, Preset Switch, Monitor Toggle, Recording Toggle, IPC Toggle / 7개 액션
- Volume Control supports 3 modes: Mute Toggle, Volume Up (+), Volume Down (-) with configurable step size / 볼륨 제어: 뮤트 토글, 볼륨 +/- 모드
- SD+ dial support for volume adjustment / SD+ 다이얼 지원
```
With:
```
- 10 SingletonAction subclasses: Bypass Toggle, Panic Mute, Volume Control, Preset Switch, Monitor Toggle, Recording Toggle, IPC Toggle, Performance Monitor, Plugin Parameter (SD+), Preset Bar (SD+) / 10개 액션
- Volume Control supports 3 modes: Mute Toggle, Volume Up (+), Volume Down (-) with configurable step size / 볼륨 제어: 뮤트 토글, 볼륨 +/- 모드
- SD+ dial support: Volume Control (dial adjust), Plugin Parameter (dial adjust) / SD+ 다이얼: 볼륨 제어, 플러그인 파라미터 조절
- SD+ touchscreen: Preset Bar (touch layout) / SD+ 터치스크린: Preset Bar
```

- [ ] **Step 2: Verify**

Run: `grep -n "7 SingletonAction" docs/ARCHITECTURE.md`
Expected: 0 matches

- [ ] **Step 3: Commit**

```bash
git add docs/ARCHITECTURE.md
git commit -m "docs: fix Stream Deck action count 7→10 in ARCHITECTURE.md"
```

---

### Task 3: Fix audio Data Flow — add Safety Limiter and correct order

ARCHITECTURE.md Data Flow (lines 163-179) is missing Safety Limiter and has wrong output order. Actual order verified from `AudioEngine.cpp:871-1010`:
```
VST Chain → Safety Limiter → Recording → IPC → Monitor → Main Output
```
Current doc says: VST Chain → Main Output → Recording → IPC → Monitor (wrong).

**Files:**
- Modify: `docs/ARCHITECTURE.md:165-179`

- [ ] **Step 1: Replace the Data Flow code block**

Replace lines 165-179 with:
```
1. Audio driver callback fires (WASAPI/ASIO on Win, CoreAudio on macOS, ALSA/JACK on Linux). ScopedNoDenormals set. / 오디오 드라이버 콜백 발생. ScopedNoDenormals 설정.
2. If muted: zero output, reset levels, return early (fast path) / 뮤트 시: 출력 0, 레벨 리셋, 즉시 반환 (고속 경로)
3. Input PCM float32 copied to pre-allocated work buffer (no heap alloc) / 입력 PCM float32를 사전 할당된 버퍼에 복사 (힙 할당 없음)
4. Channel processing: Mono (average L+R) or Stereo (passthrough) / 채널 처리: Mono (좌우 평균) 또는 Stereo (패스스루)
5. Apply input gain (atomic float) / 입력 게인 적용 (atomic float)
6. Measure input RMS level (every 4th callback — decimation) / 입력 RMS 레벨 측정 (4번째 콜백마다 — 데시메이션)
7. Process through VST chain (graph->processBlock, inline, pre-allocated MidiBuffer) / VST 체인 처리 (인라인, 사전 할당된 MidiBuffer)
8. Safety Limiter (feed-forward, in-place on workBuffer — applied before ALL output paths) / Safety Limiter (피드포워드, workBuffer 인플레이스 — 모든 출력 경로 전에 적용)
9. Write to AudioRecorder (if recording, lock-free) / AudioRecorder에 기록 (녹음 중이면, 락프리)
10. Write to SharedMemWriter (if IPC enabled) / SharedMemWriter에 기록 (IPC 활성화 시)
11. OutputRouter routes to monitor (if enabled) / OutputRouter가 모니터로 라우팅 (활성화 시):
    Monitor -> volume scale -> lock-free AudioRingBuffer -> MonitorOutput (separate audio device)
12. Apply output volume + copy to main output (outputChannelData) / 출력 볼륨 적용 + 메인 출력(outputChannelData)에 복사
13. Measure output RMS level (every 4th callback) / 출력 RMS 레벨 측정 (4번째 콜백마다)
```

Key changes:
- Step 8 added: Safety Limiter (was missing entirely)
- Main output moved from step 8 to step 12 (was incorrectly before Recording/IPC/Monitor)
- Step 12 now mentions output volume application
- All 4 output paths (Recording, IPC, Monitor, Main) receive post-limiter audio

- [ ] **Step 2: Commit**

```bash
git add docs/ARCHITECTURE.md
git commit -m "docs: fix audio Data Flow order — add Safety Limiter, correct output sequence"
```

---

## Chunk 2: README.md Fixes

### Task 4: Add "(= Panic Mute)" note to input mute hotkey

README.md line 159: hotkey table shows "입력 뮤트 토글 / Input mute" which looks like an independent feature. Actually `InputMuteToggle` calls `doPanicMute()` — they're the same thing.

**Files:**
- Modify: `README.md:159`

- [ ] **Step 1: Update hotkey table row**

Replace:
```
  | Ctrl+Shift+N | 입력 뮤트 토글 / Input mute |
```
With:
```
  | Ctrl+Shift+N | 입력 뮤트 토글 (= 패닉 뮤트) / Input mute (= Panic Mute) |
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: clarify input mute = panic mute in README hotkey table"
```

---

### Task 5: Expand README FAQ with common issues

Current FAQ has 7 items. Missing critical scenarios users frequently encounter.

**Files:**
- Modify: `README.md` — after existing FAQ entries (after line ~460)

- [ ] **Step 1: Add 5 new FAQ entries**

Add after the last existing `</details>` block:

```markdown
<details>
<summary><b>ASIO를 선택했는데 다른 앱에서 소리가 안 나요 / Other apps lose audio when ASIO is selected</b></summary>

ASIO 드라이버는 오디오 장치를 **독점(Exclusive)** 으로 사용합니다. DirectPipe가 ASIO로 오디오 장치를 열면, 다른 앱(Discord, 브라우저 등)이 같은 장치에 접근할 수 없습니다.

**해결 방법:**
1. **WASAPI Shared 사용 (권장)** — Audio 탭 > Driver Type을 "Windows Audio"로 변경. 다른 앱과 장치를 공유할 수 있습니다.
2. **ASIO4ALL 사용** — 여러 장치를 하나의 가상 ASIO 장치로 묶을 수 있지만, 지연이 다소 증가합니다.
3. **전용 오디오 인터페이스 사용** — Focusrite, SSL 등 전용 인터페이스의 ASIO 드라이버는 해당 장치만 독점하므로, 내장 사운드카드는 다른 앱이 사용할 수 있습니다.

---

ASIO drivers use **exclusive access** to the audio device. When DirectPipe opens a device via ASIO, other apps (Discord, browser, etc.) cannot access it.

**Solutions:**
1. **Use WASAPI Shared (recommended)** — Change Driver Type to "Windows Audio" in Audio tab. Shares the device with other apps.
2. **Use ASIO4ALL** — Combines multiple devices into one virtual ASIO device, but adds some latency.
3. **Use a dedicated audio interface** — Focusrite, SSL, etc. Their ASIO driver only locks the interface, leaving the built-in sound card free for other apps.
</details>

<details>
<summary><b>플러그인이 스캔에서 안 보여요 / Plugin not showing in scan results</b></summary>

DirectPipe는 **64-bit VST2/VST3 플러그인만** 지원합니다. 32-bit 플러그인은 표시되지 않습니다.

**체크리스트:**
1. **64-bit인지 확인** — 32-bit 전용 플러그인은 지원되지 않습니다
2. **VST 경로 확인** — Audio 탭 > Scan Plugins > 스캔 경로에 플러그인 폴더가 포함되어 있는지 확인
3. **블랙리스트 확인** — 이전 스캔에서 크래시한 플러그인은 블랙리스트에 등록됩니다. Settings > Clear Plugin Cache로 초기화 후 재스캔
4. **플러그인 재설치** — 간혹 손상된 DLL/VST3 번들은 스캐너가 건너뜁니다

---

DirectPipe only supports **64-bit VST2/VST3 plugins**. 32-bit plugins won't appear.

**Checklist:**
1. **Verify 64-bit** — 32-bit-only plugins are not supported
2. **Check VST paths** — Audio tab > Scan Plugins > verify plugin folder is in scan paths
3. **Check blacklist** — Plugins that crashed during previous scans are blacklisted. Reset via Settings > Clear Plugin Cache, then re-scan
4. **Reinstall plugin** — Corrupted DLL/VST3 bundles may be silently skipped
</details>

<details>
<summary><b>프리셋 전환할 때 소리가 잠깐 끊겨요 / Brief audio gap when switching presets</b></summary>

프리셋 전환 시 **~10-50ms의 매우 짧은 오디오 갭**이 발생할 수 있습니다. 이것은 **Keep-Old-Until-Ready** 메커니즘 때문입니다 — 새 플러그인 체인이 백그라운드에서 완전히 로드될 때까지 이전 체인이 계속 오디오를 처리하고, 준비가 되면 원자적으로 교체합니다.

이 10-50ms 갭은 정상 동작이며, v3의 1-3초 무음 갭에서 크게 개선된 것입니다. 프리로드 캐시가 활성화되어 있으면 (다른 슬롯 플러그인을 미리 로드) 더 짧아질 수 있습니다.

---

A **very brief audio gap (~10-50ms)** may occur during preset switches. This is due to the **Keep-Old-Until-Ready** mechanism — the old plugin chain continues processing audio while the new chain loads in the background, then swaps atomically when ready.

This 10-50ms gap is normal and a major improvement over v3's 1-3 second mute gap. With the preload cache active (pre-loads other slots' plugins), the gap can be even shorter.
</details>

<details>
<summary><b>CPU 사용량이 높아요 / High CPU usage</b></summary>

**원인 진단:**
1. **Performance Monitor 확인** — 상태 바의 CPU % 확인. 70% 이상이면 플러그인 체인 최적화 필요
2. **무거운 플러그인 식별** — 각 플러그인을 하나씩 Bypass하면서 CPU 변화 확인
3. **오버샘플링 확인** — 일부 플러그인(리미터, 이퀄라이저)은 내부 오버샘플링(2x-8x)이 있어 CPU를 크게 사용

**해결 방법:**
- 무거운 플러그인을 더 가벼운 대안으로 교체 (예: FabFilter Pro-Q 3 → TDR Nova)
- 불필요한 플러그인 제거 또는 Bypass
- 버퍼 크기 늘리기 (Audio 탭) — 지연이 증가하지만 CPU 부하 감소
- Auto 모드의 내장 프로세서는 최적화되어 있어 CPU 사용량이 매우 낮음

---

**Diagnosis:**
1. **Check Performance Monitor** — Look at CPU % in status bar. Above 70% means the plugin chain needs optimization
2. **Identify heavy plugins** — Bypass plugins one by one and observe CPU change
3. **Check oversampling** — Some plugins (limiters, EQs) have internal oversampling (2x-8x) that significantly increases CPU

**Solutions:**
- Replace heavy plugins with lighter alternatives (e.g., FabFilter Pro-Q 3 → TDR Nova)
- Remove or Bypass unused plugins
- Increase buffer size (Audio tab) — adds latency but reduces CPU load
- Auto mode's built-in processors are optimized and use very little CPU
</details>

<details>
<summary><b>모니터에서 지연이 느껴져요 / Hearing delay in monitor output</b></summary>

모니터 출력은 메인 출력과 **별도의 오디오 장치**를 사용하므로, 추가 지연이 발생할 수 있습니다.

**원인:**
1. **모니터 장치의 버퍼 크기** — WASAPI Shared 모드는 장치 기본 버퍼를 사용합니다 (보통 10-20ms)
2. **샘플레이트 불일치** — 메인 장치와 모니터 장치의 샘플레이트가 다르면 리샘플링 지연 발생
3. **모니터용 링버퍼** — 두 오디오 스레드 간 데이터 전달에 추가 버퍼 사용

**해결 방법:**
- 메인 장치와 모니터 장치의 샘플레이트를 일치시키기 (Audio 탭에서 확인)
- ASIO를 사용한다면, 같은 인터페이스의 다른 출력을 모니터로 사용 (추가 장치 불필요)
- 모니터 지연은 자기 목소리 모니터링용이므로, 방송/녹음 품질에는 영향 없음

---

Monitor output uses a **separate audio device** from the main output, which can introduce additional latency.

**Causes:**
1. **Monitor device buffer size** — WASAPI Shared mode uses the device's default buffer (typically 10-20ms)
2. **Sample rate mismatch** — Different sample rates between main and monitor devices cause resampling delay
3. **Ring buffer** — Inter-device audio transfer uses an additional buffer

**Solutions:**
- Match sample rates between main and monitor devices (check in Audio tab)
- If using ASIO, use a different output of the same interface as monitor (no extra device needed)
- Monitor delay only affects self-monitoring; it doesn't affect broadcast/recording quality
</details>
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: add 5 FAQ entries — ASIO exclusive, plugin scan, preset gap, CPU, monitor delay"
```

---

## Chunk 3: CONTROL_API.md Error Spec

### Task 6: Add error response specification table to CONTROL_API.md

HTTP error responses are briefly mentioned (lines 374-380) but lack a comprehensive error scenario table. WebSocket has no error response documentation at all.

**Files:**
- Modify: `docs/CONTROL_API.md` — after existing error response section (~line 380)

- [ ] **Step 1: Read current error section**

Run: Read `docs/CONTROL_API.md` lines 370-390 to see exact insertion point.

- [ ] **Step 2: Add HTTP error scenario table**

Insert after the existing error response lines:

```markdown
### HTTP 에러 시나리오 / HTTP Error Scenarios

| 시나리오 / Scenario | HTTP 상태 / Status | 응답 / Response |
|---|---|---|
| 존재하지 않는 엔드포인트 / Unknown endpoint | 404 | `{"error": "Not found"}` |
| GET 외 메서드 (OPTIONS 제외) / Non-GET method (except OPTIONS) | 405 | `{"error": "Method not allowed"}` |
| 범위 밖 플러그인 인덱스 / Plugin index out of range | 400 | `{"error": "Index out of range"}` |
| 비숫자 값 입력 / Non-numeric value | 400 | `{"error": "Invalid value"}` |
| 볼륨 범위 밖 (0.0~1.0) / Volume out of range | 400 | `{"error": "Value out of range"}` |
| 리미터 ceiling 범위 밖 (-6.0~0.0) / Ceiling out of range | 400 | `{"error": "Value out of range"}` |
| 패닉 뮤트 중 차단된 액션 / Action blocked during panic mute | 200 | `{"ok": true}` (액션은 ActionHandler에서 무시됨 / action silently ignored by ActionHandler) |
| CORS preflight | 200 | Empty body + CORS headers |

> **Note**: 패닉 뮤트 중 대부분의 액션은 HTTP 200을 반환하지만 ActionHandler 내부에서 `engine_.isMuted()` 체크에 의해 무시됩니다. 차단 여부를 확인하려면 WebSocket 상태를 모니터링하세요.
>
> Most actions return HTTP 200 during panic mute but are silently ignored by ActionHandler's `engine_.isMuted()` check. Monitor WebSocket state to detect if actions are being blocked.

### WebSocket 에러 응답 / WebSocket Error Responses

WebSocket 메시지에 대한 에러 응답은 없습니다 — 잘못된 메시지는 무시됩니다. 성공한 액션의 결과는 다음 상태 브로드캐스트에 반영됩니다.

WebSocket messages have no error response — invalid messages are silently ignored. Successful action results are reflected in the next state broadcast.
```

- [ ] **Step 3: Commit**

```bash
git add docs/CONTROL_API.md
git commit -m "docs: add HTTP error scenario table and WebSocket error behavior to CONTROL_API"
```

---

## Chunk 4: USER_GUIDE Improvements

### Task 7: Add Built-in Processors detailed parameter guide to USER_GUIDE

USER_GUIDE has basic Auto mode info (lines 455-494) but lacks user-facing parameter adjustment guidance. Users need to know "how do I change noise removal strength?" or "what does Freeze Level do?"

**Files:**
- Modify: `docs/USER_GUIDE.md` — expand existing Built-in Processors section (~line 469)

- [ ] **Step 1: Read current section**

Run: Read `docs/USER_GUIDE.md` lines 455-500 to see exact current state.

- [ ] **Step 2: Expand Built-in Processors section**

After the existing parameter tables, add detailed per-processor guides:

```markdown
### 내장 프로세서 상세 가이드 / Built-in Processor Details

#### Filter (하이패스 + 로우패스 필터 / HPF + LPF)

플러그인 체인에서 Filter를 클릭하면 편집 패널이 열립니다. / Click the Filter in the plugin chain to open the edit panel.

- **HPF (High-Pass Filter)**: 저음역 잡음 제거 (에어컨 소음, 저주파 험 등). 기본값 60Hz ON.
  - 프리셋: Off, 40Hz, 60Hz, 80Hz, 100Hz, 120Hz, Custom
  - Custom 선택 시 슬라이더로 20-500Hz 범위 조절 가능
- **LPF (Low-Pass Filter)**: 고음역 잡음 제거 (치찰음, 고주파 잡음 등). 기본값 16kHz OFF.
  - 프리셋: Off, 8kHz, 12kHz, 16kHz, 20kHz, Custom
  - 대부분의 경우 OFF 또는 16kHz 권장. 음성 위주라면 12kHz도 적합

HPF removes low-frequency noise (air conditioning hum, rumble). Default: 60Hz ON. LPF removes high-frequency noise (sibilance, hiss). Default: 16kHz OFF. Select a preset or choose Custom for slider control.

#### Noise Removal (노이즈 제거 / RNNoise)

AI 기반 노이즈 억제. 배경 소음(키보드, 팬, 환경 소음)을 제거합니다. / AI-based noise suppression. Removes background noise (keyboard, fan, ambient).

- **Strength (강도)**: 3단계 프리셋
  - **Light (가벼움)**: VAD 임계값 0.50. 약한 배경 소음 제거. 음성 왜곡 최소
  - **Standard (표준)**: VAD 임계값 0.70. 일반적인 사용에 권장 (기본값)
  - **Aggressive (강력)**: VAD 임계값 0.90. 강한 배경 소음 환경. 소리가 작은 음성이 잘릴 수 있음
- **⚠️ 48kHz 전용**: 현재 RNNoise는 48kHz 샘플레이트에서만 동작합니다. 다른 샘플레이트에서는 자동으로 패스스루됩니다. Audio 탭에서 샘플레이트를 48kHz로 설정하세요.

Three strength presets. Light (0.50) for mild noise, Standard (0.70, default) for typical use, Aggressive (0.90) for noisy environments. **48kHz only** — automatically bypassed at other sample rates.

#### Auto Gain (자동 게인 / LUFS AGC)

LUFS 기반 자동 볼륨 조절. 작은 목소리를 올리고 큰 소리를 줄여 일정한 출력 레벨을 유지합니다. / LUFS-based automatic volume control. Boosts quiet speech and reduces loud sounds for consistent output.

- **Target LUFS (목표 레벨)**: 기본 -15.0 LUFS (범위: -24 ~ -6). 방송용은 -14~-16 권장
- **Freeze Level (프리즈 레벨)**: 기본 -45 dBFS. 이 레벨 이하의 입력은 무시 (게인 변경 안 함). 무음 구간에서 잡음이 증폭되는 것을 방지. -65 dBFS 이하면 자동 비활성화
- **Low Correct (부스트 속도)**: 기본 0.50. 작은 소리를 올리는 속도. 0.0(느림)~1.0(빠름). 낮을수록 부드럽게 올림
- **High Correct (컷 속도)**: 기본 0.75. 큰 소리를 줄이는 속도. 0.0(느림)~1.0(빠름). 높을수록 빠르게 줄임
- **Max Gain (최대 증폭)**: 기본 24 dB. 최대 부스트 한계. 과도한 증폭 방지

**권장 설정 / Recommended Settings:**
- 일반 방송: Target -15, Low 0.50, High 0.75, Max Gain 24 dB
- 조용한 환경: Target -14, Low 0.60, High 0.80, Max Gain 18 dB
- 게임 + 음성: Target -16, Low 0.40, High 0.70, Max Gain 24 dB (게임 소리와 분리)
```

- [ ] **Step 3: Commit**

```bash
git add docs/USER_GUIDE.md
git commit -m "docs: add detailed Built-in Processors parameter guide to USER_GUIDE"
```

---

### Task 8: Add portable mode interaction matrix to USER_GUIDE

Portable mode + multi-instance behavior is complex and only described in text. A table would make it instantly clear.

**Files:**
- Modify: `docs/USER_GUIDE.md` — find portable/multi-instance section and add matrix

- [ ] **Step 1: Read portable mode section**

Search USER_GUIDE for portable mode section to find insertion point.

- [ ] **Step 2: Add interaction matrix table**

Insert a table in the portable mode section:

```markdown
### 다중 인스턴스 동작 매트릭스 / Multi-Instance Behavior Matrix

| 먼저 실행 / Running First | 나중 실행 / Launched Second | 나중 실행 상태 / Second Instance State |
|---|---|---|
| 일반 설치 / Normal | 일반 설치 / Normal | ❌ 차단 (실행 불가) / Blocked |
| 일반 설치 / Normal | 포터블 / Portable | 🔊 Audio Only (외부 제어 없음) / Audio Only (no external control) |
| 포터블 (제어 중) / Portable (controlling) | 일반 설치 / Normal | ❌ 차단 / Blocked |
| 포터블 (제어 중) / Portable (controlling) | 포터블 / Portable | 🔊 Audio Only / Audio Only |

> **Audio Only 모드**: 오디오 처리만 가능하고 WebSocket/HTTP/Hotkey 서버가 시작되지 않습니다. Stream Deck, 핫키, API 제어 불가.
>
> **Audio Only mode**: Audio processing works but WebSocket/HTTP/Hotkey servers don't start. No Stream Deck, hotkey, or API control.
```

- [ ] **Step 3: Commit**

```bash
git add docs/USER_GUIDE.md
git commit -m "docs: add portable mode multi-instance interaction matrix to USER_GUIDE"
```

---

## Chunk 5: Terminology & Minor Fixes

### Task 9: Unify Korean terminology across key documents

Inconsistent Korean translations for the same English terms across documents. Affects searchability (Ctrl+F).

**Files:**
- Modify: `README.md`, `docs/USER_GUIDE.md`, `docs/QUICKSTART.md`, `docs/CONTROL_API.md`

- [ ] **Step 1: Search and audit current usage**

For each term, grep across all docs to find variants:
```bash
grep -rn "패닉 뮤트\|패닉뮤트\|전체 뮤트" docs/ README.md
grep -rn "프리셋 슬롯\|퀵 프리셋\|퀵프리셋" docs/ README.md
grep -rn "안전 리미터\|세이프티 리미터" docs/ README.md
grep -rn "가상 오디오 케이블\|가상 케이블\|가상오디오" docs/ README.md
```

- [ ] **Step 2: Standardize to canonical forms**

Apply consistent Korean terms across all user-facing docs:

| English | Canonical Korean | Variants to replace |
|---|---|---|
| Panic Mute | 패닉 뮤트 | 패닉뮤트, 전체 뮤트 |
| Preset Slot | 프리셋 슬롯 | 퀵 프리셋 슬롯 (허용하되, 최소한 첫 등장에서 "프리셋 슬롯"과 동일함을 명시) |
| Safety Limiter | Safety Limiter | 안전 리미터 → Safety Limiter로 통일 (기술 용어이므로 영문 유지) |
| Virtual Cable | 가상 케이블 | 가상 오디오 케이블 → "가상 케이블 (Virtual Cable)"로 통일 |

- [ ] **Step 3: Commit**

```bash
git add README.md docs/USER_GUIDE.md docs/QUICKSTART.md docs/CONTROL_API.md
git commit -m "docs: unify Korean terminology — 패닉 뮤트, Safety Limiter, 가상 케이블"
```

---

### Task 10: Add CONTRIBUTING.md and SECURITY.md

GPL-3.0 open-source project needs basic contributor and security guidelines.

**Files:**
- Create: `CONTRIBUTING.md`
- Create: `SECURITY.md`

- [ ] **Step 1: Create CONTRIBUTING.md**

```markdown
# Contributing to DirectPipe / DirectPipe 기여 가이드

DirectPipe에 기여해 주셔서 감사합니다! / Thank you for contributing to DirectPipe!

## 개발 환경 설정 / Development Setup

[빌드 가이드](docs/BUILDING.md) 참조. / See [Build Guide](docs/BUILDING.md).

## 코드 스타일 / Code Style

- C++17, JUCE 7.0.12 conventions
- 4 spaces indentation (no tabs)
- `camelCase` for methods/variables, `PascalCase` for classes
- `snake_case_` for member variables (trailing underscore)
- Bilingual comments: Korean first, English second (한국어 → 영어 순서)

## Pull Request 규칙 / PR Guidelines

1. `main` 브랜치에서 feature 브랜치를 생성하세요 / Create a feature branch from `main`
2. 커밋 메시지: `type: description` 형식 (예: `feat:`, `fix:`, `docs:`, `refactor:`, `test:`)
3. 테스트 통과 확인: `cmake --build build --config Release && ctest --test-dir build -C Release`
4. 오디오 콜백(RT thread)에서 힙 할당, 뮤텍스, 로깅 절대 금지
5. 새 Action 추가 시 `CLAUDE.md` Maintenance Recipes 체크리스트를 따르세요

## 테스트 / Testing

- Google Test 기반 233+ 테스트
- `tools/pre-release-test.sh` — 빌드 + 단위 테스트 + API 테스트
- `tools/pre-release-dashboard.html` — 수동 테스트 대시보드

## 버그 리포트 / Bug Reports

[GitHub Issues](../../issues)에 버그를 보고해 주세요. 재현 가능한 단계, OS, 오디오 드라이버 정보를 포함해 주세요.
```

- [ ] **Step 2: Create SECURITY.md**

```markdown
# Security Policy / 보안 정책

## 보안 취약점 보고 / Reporting Security Vulnerabilities

DirectPipe에서 보안 취약점을 발견하셨다면, **공개 이슈로 보고하지 마세요**. 대신 아래 방법으로 비공개 보고해 주세요:

If you discover a security vulnerability in DirectPipe, **please do NOT report it as a public issue**. Instead, use one of these private channels:

- **GitHub Security Advisory**: [Report a vulnerability](../../security/advisories/new)
- **Email**: (maintainer email here)

48시간 이내에 응답하겠습니다. / We will respond within 48 hours.

## 범위 / Scope

- WebSocket/HTTP 서버 취약점 (인증 우회, 명령 주입 등)
- IPC 공유 메모리 접근 제어
- 자동 업데이터 보안 (다운로드 무결성)
- VST 플러그인 로딩 경로 주입

## 지원 버전 / Supported Versions

| Version | Supported |
|---|---|
| v4.0.x (alpha) | ✅ |
| v3.10.x (stable) | ✅ |
| < v3.10 | ❌ |
```

- [ ] **Step 3: Commit**

```bash
git add CONTRIBUTING.md SECURITY.md
git commit -m "docs: add CONTRIBUTING.md and SECURITY.md"
```

---

## Chunk 6: BUILDING.md CI/CD Section

### Task 11: Add CI/CD information to BUILDING.md

No CI/CD documentation exists. Contributors don't know how builds are validated.

**Files:**
- Modify: `docs/BUILDING.md` — add new section at the end

- [ ] **Step 1: Read current BUILDING.md end**

Check end of BUILDING.md for insertion point.

- [ ] **Step 2: Add CI/CD section**

```markdown
## CI/CD (GitHub Actions)

### 빌드 워크플로우 / Build Workflow

GitHub Actions가 모든 push와 PR에서 자동 빌드를 실행합니다. / GitHub Actions runs automated builds on every push and PR.

**워크플로우 파일 / Workflow file:** `.github/workflows/build.yml`

**빌드 매트릭스 / Build Matrix:**

| Platform | OS | Compiler | Notes |
|---|---|---|---|
| Windows | windows-latest | MSVC 2022 | ASIO SDK + VST2 SDK (GitHub Secrets) |
| macOS | macos-14 (ARM) | Apple Clang | Universal binary (arm64+x86_64) |
| Linux | ubuntu-22.04 | GCC 12 | ALSA + JACK dev libraries |

**GitHub Secrets (필수 / Required):**
- `VST2_SDK_B64` — Base64-encoded VST2 SDK archive
- `ASIO_SDK_B64` — Base64-encoded Steinberg ASIO SDK archive

> ⚠️ VST2/ASIO SDK는 라이선스 제약으로 리포지토리에 포함되지 않습니다. CI에서는 GitHub Secrets에서 디코딩하여 사용합니다.
>
> VST2/ASIO SDKs are not included in the repository due to licensing. CI decodes them from GitHub Secrets.

### 릴리스 / Release

- `v4.x.x` 태그 push 시 자동으로 pre-release 생성
- 릴리스 아티팩트: `DirectPipe-v{VERSION}-Windows.zip`, `DirectPipe-v{VERSION}-macOS.dmg`, `DirectPipe-v{VERSION}-Linux.tar.gz`
- v4는 반드시 `--prerelease` 플래그로 릴리스 (v3 자동 업데이터 보호)

### 로컬에서 CI 재현 / Reproducing CI Locally

```bash
# Windows (PowerShell)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

# macOS/Linux
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
```

- [ ] **Step 3: Commit**

```bash
git add docs/BUILDING.md
git commit -m "docs: add CI/CD section to BUILDING.md — workflow, secrets, release process"
```

---

## Execution Order

| Task | Description | Dependencies | Estimated Effort |
|---|---|---|---|
| 1 | Fix action count 15→19 | None | 2 min |
| 2 | Fix SD action count 7→10 | None | 2 min |
| 3 | Fix audio Data Flow order | None | 3 min |
| 4 | input_mute = PanicMute note | None | 1 min |
| 5 | Expand README FAQ (+5) | None | 5 min |
| 6 | CONTROL_API error spec table | None | 3 min |
| 7 | USER_GUIDE Built-in Processors guide | None | 5 min |
| 8 | USER_GUIDE portable mode matrix | None | 2 min |
| 9 | Terminology unification | Tasks 1-8 (avoid conflicts) | 10 min |
| 10 | CONTRIBUTING.md + SECURITY.md | None | 3 min |
| 11 | BUILDING.md CI/CD section | None | 3 min |

Total: 11 tasks, ~39 minutes.

Tasks 1-8 and 10-11 are independent. Task 9 should run last (touches multiple files that other tasks also modify).
