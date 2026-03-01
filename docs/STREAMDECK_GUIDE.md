# DirectPipe Stream Deck Plugin / 스트림 덱 플러그인

## Overview / 개요

The DirectPipe Stream Deck plugin connects to the host via WebSocket and provides 7 button actions for controlling the VST host remotely.

DirectPipe Stream Deck 플러그인은 WebSocket으로 호스트에 연결하여 7가지 버튼 액션으로 VST 호스트를 원격 제어한다.

| Action / 액션 | Description / 설명 |
|---------------|-------------------|
| **Bypass Toggle** | Toggle VST plugin bypass. Long-press for master bypass. / 플러그인 Bypass 토글. 길게 누르면 마스터 Bypass. |
| **Panic Mute** | Mute all outputs instantly. / 전체 출력 즉시 뮤트. |
| **Volume Control** | Mute toggle, volume up/down, or dial adjust. / 뮤트 토글, 볼륨 +/-, 다이얼 조절. |
| **Preset Switch** | Switch preset slot (A-E) or cycle presets. / 프리셋 슬롯 전환 또는 순환. |
| **Monitor Toggle** | Toggle monitor output (headphones) on/off. / 모니터 출력(헤드폰) 켜기/끄기. |
| **Recording Toggle** | Start/stop audio recording to WAV. Shows elapsed time. / 오디오 녹음 시작/중지. 경과 시간 표시. |
| **IPC Toggle** | Toggle IPC output (Receiver VST) on/off. / IPC 출력(리시버 VST) 켜기/끄기. |

---

## Prerequisites / 요구 사항

- **DirectPipe** running with WebSocket server enabled (port 8765) / DirectPipe 실행 중 (WebSocket 서버 활성화)
- **Elgato Stream Deck** software v6.9+ / Stream Deck 소프트웨어 v6.9+
- **Node.js** v20+ (bundled with Stream Deck software) / Node.js v20+

---

## Installation / 설치

### Option A: Packaged Plugin / 패키지 설치

1. Double-click `dist/com.directpipe.directpipe.streamDeckPlugin` / 패키지 파일 더블클릭
2. Stream Deck software installs it automatically / 자동 설치

### Option B: Manual Install / 수동 설치

1. Copy `com.directpipe.directpipe.sdPlugin/` folder to the Stream Deck plugins directory: / 플러그인 디렉토리에 복사:
   ```
   %APPDATA%\Elgato\StreamDeck\Plugins\com.directpipe.directpipe.sdPlugin
   ```

2. Install dependencies: / 의존성 설치:
   ```bash
   cd com.directpipe.directpipe.sdPlugin
   npm install
   ```

3. Restart Stream Deck software. / Stream Deck 소프트웨어 재시작.

---

## Connection / 연결

Connects via WebSocket on `ws://localhost:8765`. / WebSocket으로 연결.

### Auto-Reconnect / 자동 재연결

Fully event-driven — no periodic polling: / 완전 이벤트 기반 — 주기적 폴링 없음:

- **UDP Discovery**: DirectPipe broadcasts `DIRECTPIPE_READY` on UDP port 8767 when its WebSocket server starts. The plugin listens and connects instantly. / DirectPipe가 WebSocket 서버 시작 시 UDP 8767로 알림 전송. 플러그인이 즉시 연결.
- **User action trigger**: Pressing any button or rotating a dial while disconnected triggers an immediate reconnection attempt. / 연결 해제 상태에서 버튼/다이얼 조작 시 즉시 재연결 시도.
- **Startup**: Initial `connect()` on plugin load — connects immediately if DirectPipe is already running. / 플러그인 시작 시 1회 연결 시도 — DirectPipe가 이미 실행 중이면 즉시 연결.
- Pending message queue (cap 50) while disconnected / 연결 해제 중 대기 큐 (최대 50)

While disconnected, all buttons show an alert indicator. / 연결 해제 시 모든 버튼에 경고 표시.

### Custom Port / 포트 변경

Edit `DIRECTPIPE_WS_URL` in `src/plugin.js`: / `src/plugin.js`에서 수정:

```javascript
const DIRECTPIPE_WS_URL = "ws://localhost:8770";
```

---

## Actions / 액션

### Bypass Toggle

**UUID:** `com.directpipe.directpipe.bypass-toggle`

- **Short press** — Toggle bypass for configured plugin index / 짧게 누름 -> 설정된 플러그인 Bypass
- **Long press** (>500ms) — Toggle master bypass / 길게 누름 -> 마스터 Bypass

**Display:** Plugin name + "Bypassed" or "Active". Master bypass shows "MASTER Bypassed". / 플러그인 이름 + 상태 표시.

**Settings (Property Inspector):**
- `pluginNumber` — Plugin number (1-based in PI, converted to 0-based internally) / 플러그인 번호 (PI에서 1부터 시작)

---

### Panic Mute / 패닉 뮤트

**UUID:** `com.directpipe.directpipe.panic-mute`

- **Press** — Toggle panic mute on all outputs / 전체 출력 뮤트 토글

**Display:** State 0 = "MUTE" (normal), State 1 = "MUTED" (red indicator) / 상태 표시

No settings required. / 설정 불필요.

---

### Volume Control / 볼륨 제어

**UUID:** `com.directpipe.directpipe.volume-control`

Supports both Keypad and SD+ Encoder (dial). / 키패드와 SD+ 인코더 (다이얼) 모두 지원.

**Keypad modes:**
- **Mute Toggle mode** — Press to toggle mute for target / 뮤트 토글 모드
- **Volume Up mode** — Press to increase volume by step size / 볼륨 Up 모드
- **Volume Down mode** — Press to decrease volume by step size / 볼륨 Down 모드

**Encoder (Stream Deck+):**
- **Dial rotate** — Adjust volume +/-5% per tick with optimistic local update (instant LCD feedback) / 다이얼 회전 -> 볼륨 +-5% 조절 (즉시 LCD 반영)
- **Dial press** — Mute toggle for target / 다이얼 누름 -> 뮤트 토글
- **LCD display** — Shows target name, volume value, progress indicator. Shows "MUTED" when muted. / LCD에 대상 이름, 볼륨, 프로그레스 바 표시. 뮤트 시 "MUTED".

**Display:** Target name + volume % (or x multiplier for input) or "MUTED". Input gain shows x0.00-x2.00 format. / 대상 이름 + 볼륨 % (입력은 x배수) 또는 "MUTED".

**Settings (Property Inspector):**
- `target` — `"monitor"` (default), `"output"`, or `"input"` / 대상 선택
- `mode` — `"mute"` (default), `"volume_up"`, or `"volume_down"` / 버튼 동작 모드
- `step` — 1-25% (default: 5%) / 볼륨 스텝 크기

**Input mute detection:** When target is `"input"`, checks both `muted` and `input_muted` fields from state. / 입력 대상일 때 `muted`와 `input_muted` 모두 확인.

---

### Monitor Toggle / 모니터 토글

**UUID:** `com.directpipe.directpipe.monitor-toggle`

- **Press** — Toggle monitor output (headphones) on/off / 모니터 출력 토글

**Display:** State 0 = "MON ON", State 1 = "MON OFF" / 상태 표시

No settings required. / 설정 불필요.

---

### Preset Switch / 프리셋 전환

**UUID:** `com.directpipe.directpipe.preset-switch`

- **With slot configured** — Switch to specific slot (A-E) / 슬롯 설정 시 -> 해당 슬롯으로 전환
- **With "cycle" or no config** — Cycle to next preset / 미설정 또는 "cycle" -> 다음 프리셋 순환
- **With preset index** — Load preset by index / 프리셋 인덱스로 로드

**Display:** Slot label (A-E) with "[A] Active" for active slot / 슬롯 라벨 + 활성 표시

**Settings (Property Inspector):**
- `slotIndex` — 0-4 for specific slot (A-E), or `"cycle"` for cycling / 슬롯 번호 또는 "cycle"

---

### Recording Toggle / 녹음 토글

**UUID:** `com.directpipe.directpipe.recording-toggle`

- **Press** — Start or stop recording processed audio to WAV / 녹음 시작/중지

**Display:** State 0 = "REC" (idle), State 1 = "REC" + elapsed time in mm:ss format (e.g., "REC\n01:23") / 상태 0 = "REC" (대기), 상태 1 = "REC" + 경과 시간

No settings required. / 설정 불필요.

---

### IPC Toggle / IPC 토글

**UUID:** `com.directpipe.directpipe.ipc-toggle`

- **Press** -- Toggle IPC output (Receiver VST) on/off / IPC 출력(리시버 VST) 켜기/끄기 토글

**Display:** State 0 = "IPC ON" (enabled), State 1 = "IPC OFF" (disabled) / 상태 표시

No settings required. / 설정 불필요.

---

## Technical Details / 기술 세부사항

### SDK Version

Built with `@elgato/streamdeck` v2.0.1 (npm), SDKVersion 3 in manifest, plugin version 3.7.0.0. Uses `SingletonAction` class-based architecture. / `@elgato/streamdeck` v2.0.1 (npm), manifest SDKVersion 3, 플러그인 버전 3.7.0.0. SingletonAction 클래스 기반 아키텍처.

### WebSocket Client (`websocket-client.js`)

- EventEmitter-based: `connected`, `disconnected`, `state`, `error`, `message` events / 이벤트 기반
- Ping keepalive every 15s / 15초마다 핑 유지
- Pending message queue (cap 50) during disconnection / 연결 해제 중 대기 큐 (최대 50)
- Event-driven reconnection via `reconnectNow()` — triggered by UDP discovery or user action (no polling) / UDP 디스커버리 또는 사용자 조작으로 즉시 재연결 (폴링 없음)

### Settings Cache / 설정 캐시

All actions cache settings from `onWillAppear`/`onDidReceiveSettings` events. The 30 Hz state broadcast loop uses cached settings (synchronous) instead of `await action.getSettings()` (async IPC) for instant display updates with zero event loop congestion.

모든 액션은 `onWillAppear`/`onDidReceiveSettings` 이벤트에서 설정을 캐시한다. 30 Hz 상태 브로드캐스트 루프는 비동기 IPC 대신 캐시된 설정을 동기적으로 사용하여 이벤트 루프 정체 없이 즉시 디스플레이를 갱신한다.

---

## File Structure / 파일 구조

```
com.directpipe.directpipe.sdPlugin/
  manifest.json               SDK v3 plugin manifest / 플러그인 매니페스트
  package.json                Node.js package (@elgato/streamdeck, ws)
  .sdignore                   Files excluded from packaging / 패키징 제외 파일
  src/
    plugin.js                 Main entry, streamDeck.connect() + DirectPipeClient / 메인 진입점
    websocket-client.js       WebSocket client with event-driven reconnect / 이벤트 기반 재연결 WS 클라이언트
    actions/
      bypass-toggle.js        Bypass toggle SingletonAction / Bypass 토글 액션
      panic-mute.js           Panic mute SingletonAction / 패닉 뮤트 액션
      volume-control.js       Volume control SingletonAction / 볼륨 제어 액션
      preset-switch.js        Preset switch SingletonAction / 프리셋 전환 액션
      monitor-toggle.js       Monitor toggle SingletonAction / 모니터 토글 액션
      recording-toggle.js     Recording toggle SingletonAction / 녹음 토글 액션
      ipc-toggle.js           IPC toggle SingletonAction / IPC 토글 액션
    inspectors/
      bypass-pi.html          Bypass settings (sdpi-components v4) / Bypass 설정 UI
      volume-pi.html          Volume settings (target, mode, step) / 볼륨 설정 UI
      preset-pi.html          Preset settings (slot selector) / 프리셋 설정 UI
  images/                     Button icons (PNG + @2x) / 버튼 아이콘
  icons-src/                  SVG icon sources / SVG 아이콘 원본
  scripts/
    generate-icons.mjs        SVG -> PNG generation script (sharp) / 아이콘 생성 스크립트
dist/
  com.directpipe.directpipe.streamDeckPlugin   Packaged plugin / 패키지 파일
```

---

## Troubleshooting / 문제 해결

**All buttons show alert? / 모든 버튼에 경고 표시?**
- DirectPipe is not running or WebSocket connection failed. / DirectPipe 미실행 또는 WebSocket 연결 실패.
- Check WebSocket port (default: 8765). / 포트 확인.
- Ensure WebSocket server is enabled in DirectPipe Controls > StreamDeck tab. / Controls > StreamDeck 탭에서 서버 활성화 확인.

**Plugin keeps restarting? / 플러그인이 계속 재시작?**
- Check logs: `%APPDATA%\Elgato\StreamDeck\logs\` / 로그 확인
- Ensure `node_modules/` exists in the installed plugin folder. / 설치 폴더에 node_modules 확인.
- Node.js v20+ required. / Node.js v20+ 필요.

**Bypass shows wrong plugin? / Bypass가 잘못된 플러그인을 표시?**
- Plugin number in Property Inspector is 1-based (Plugin #1 = first). / PI에서 플러그인 번호는 1부터 시작.
- Internally converted to 0-based index. / 내부적으로 0-based 인덱스로 변환.

---

## Packaging / 패키징

### Build Icons / 아이콘 빌드

```bash
cd com.directpipe.directpipe.sdPlugin
npm run icons    # SVG -> PNG generation (requires sharp)
```

### Package Plugin / 플러그인 패키징

**Important:** Must use official `streamdeck pack` CLI. Custom ZIP will be rejected by Maker Console (SDKVersion 3 requires CLI packaging).

**중요:** 반드시 공식 `streamdeck pack` CLI로 패키징해야 함. 커스텀 ZIP은 Maker Console에서 거부됨 (SDKVersion 3은 CLI 패키징 필수).

```bash
npm install -g @elgato/cli
cd com.directpipe.directpipe.sdPlugin
npm run build                  # Rollup bundle src/ -> bin/plugin.js
streamdeck validate .          # Validate manifest and structure
streamdeck pack . --output ../dist/ --force  # Create .streamDeckPlugin
```

Output: `dist/com.directpipe.directpipe.streamDeckPlugin`

### Maker Console Submission / Maker Console 제출

The `dist/` folder contains marketplace assets: / `dist/` 폴더에 마켓플레이스 에셋 포함:
- `directpipe-marketplace-288x288.png` — Marketplace icon / 마켓플레이스 아이콘
- `directpipe-thumbnail-1920x960.png` — Thumbnail / 썸네일
- `gallery-1-bypass.png`, `gallery-2-volume.png`, `gallery-3-presets.png` — Gallery images / 갤러리 이미지

---

## API Reference / API 레퍼런스

See [CONTROL_API.md](./CONTROL_API.md) for the complete WebSocket and HTTP API. / 전체 API는 CONTROL_API.md 참조.
