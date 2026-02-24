# DirectPipe Stream Deck Plugin / 스트림 덱 플러그인

## Overview / 개요

The DirectPipe Stream Deck plugin connects to the host via WebSocket and provides 4 button actions for controlling the VST host remotely.

DirectPipe Stream Deck 플러그인은 WebSocket으로 호스트에 연결하여 4가지 버튼 액션으로 VST 호스트를 원격 제어한다.

| Action / 액션 | Description / 설명 |
|---------------|-------------------|
| **Bypass Toggle** | Toggle VST plugin bypass. Long-press for master bypass. / 플러그인 Bypass 토글. 길게 누르면 마스터 Bypass. |
| **Panic Mute** | Mute all outputs instantly. / 전체 출력 즉시 뮤트. |
| **Volume Control** | Mute toggle, volume up/down, or dial adjust. / 뮤트 토글, 볼륨 +/-, 다이얼 조절. |
| **Preset Switch** | Switch preset slot (A-E) or cycle presets. / 프리셋 슬롯 전환 또는 순환. |

---

## Prerequisites / 요구 사항

- **DirectPipe** running / DirectPipe 실행 중
- **Elgato Stream Deck** software v6.9+ / Stream Deck 소프트웨어 v6.9+
- **Node.js** v20+ / Node.js v20+

---

## Installation / 설치

### Option A: Packaged Plugin / 패키지 설치

1. Double-click `dist/com.directpipe.directpipe.streamDeckPlugin` / 패키지 파일 더블클릭
2. Stream Deck software installs it automatically / 자동 설치

### Option B: Manual Install / 수동 설치

1. Copy `streamdeck-plugin/` folder to the Stream Deck plugins directory: / 플러그인 디렉토리에 복사:
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

If DirectPipe is not running, the plugin reconnects automatically: / DirectPipe 미실행 시 자동 재연결:

- Initial delay: 2s / 초기 딜레이: 2초
- Max delay: 30s / 최대 딜레이: 30초
- Backoff factor: 1.5x / 백오프 팩터: 1.5배

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

- **Short press** — Toggle bypass for configured plugin index / 짧게 누름 → 설정된 플러그인 Bypass
- **Long press** (>500ms) — Toggle master bypass / 길게 누름 → 마스터 Bypass

**Display:** Plugin name + "Bypassed" or "Active". Master bypass shows "MASTER Bypassed". / 플러그인 이름 + 상태 표시.

**Settings:** `pluginIndex` (number, default: 0) — Set via Property Inspector / PI에서 설정

---

### Panic Mute / 패닉 뮤트

**UUID:** `com.directpipe.directpipe.panic-mute`

- **Press** — Toggle panic mute on all outputs / 전체 출력 뮤트 토글

**Display:** State 0 = "MUTE" (normal), State 1 = "MUTED" (red indicator) / 상태 표시

No settings required. / 설정 불필요.

---

### Volume Control / 볼륨 제어

**UUID:** `com.directpipe.directpipe.volume-control`

- **Mute Toggle mode** — Press to toggle mute for target / 뮤트 토글 모드 — 대상 뮤트 토글
- **Volume Up mode** — Press to increase volume by step size / 볼륨 Up 모드 — 스텝만큼 증가
- **Volume Down mode** — Press to decrease volume by step size / 볼륨 Down 모드 — 스텝만큼 감소
- **Dial rotate** (Stream Deck+) — Adjust volume ±5% per tick / 다이얼로 볼륨 ±5% 조절

**Display:** Target name + volume % or "MUTED". Volume Up/Down shows +/- indicator. / 대상 이름 + 볼륨 % 또는 "MUTED". Up/Down 시 +/- 표시.

**Settings:**
- `target` — `"monitor"`, `"input"`, or `"virtual_mic"` / 대상 선택
- `mode` — `"mute"` (default), `"volume_up"`, or `"volume_down"` / 버튼 동작 모드
- `step` — 1–25% (default: 5%) / 볼륨 스텝 크기

---

### Preset Switch / 프리셋 전환

**UUID:** `com.directpipe.directpipe.preset-switch`

- **With slot configured** — Switch to specific slot / 슬롯 설정 시 → 해당 슬롯으로 전환
- **Without config** — Cycle to next preset / 미설정 시 → 다음 프리셋 순환

**Display:** Slot label (A-E) with active indicator / 슬롯 라벨 + 활성 표시

**Settings:** `slotIndex` (0–4 for A-E, or "cycle"). Set via Property Inspector. / PI에서 슬롯 선택.

---

## File Structure / 파일 구조

```
streamdeck-plugin/
  manifest.json               SDK v2 plugin manifest / 플러그인 매니페스트
  package.json                Node.js package (@elgato/streamdeck, ws)
  .sdignore                   Files excluded from packaging / 패키징 제외 파일
  src/
    plugin.js                 Main entry, streamDeck.connect() + DirectPipeClient / 메인 진입점
    websocket-client.js       WebSocket client with auto-reconnect / 자동 재연결 WS 클라이언트
    actions/
      bypass-toggle.js        Bypass toggle SingletonAction / Bypass 토글 액션
      panic-mute.js           Panic mute SingletonAction / 패닉 뮤트 액션
      volume-control.js       Volume control SingletonAction (mute/up/down modes) / 볼륨 제어 액션
      preset-switch.js        Preset switch SingletonAction / 프리셋 전환 액션
    inspectors/
      bypass-pi.html          Bypass settings (sdpi-components v4) / Bypass 설정 UI
      volume-pi.html          Volume settings (target, mode, step) / 볼륨 설정 UI
      preset-pi.html          Preset settings (slot selector) / 프리셋 설정 UI
  images/                     Button icons (PNG + @2x) / 버튼 아이콘
  icons-src/                  SVG icon sources / SVG 아이콘 원본
  scripts/
    generate-icons.mjs        SVG → PNG generation script (sharp) / 아이콘 생성 스크립트
dist/
  com.directpipe.directpipe.streamDeckPlugin   Packaged plugin / 패키지 파일
```

---

## Troubleshooting / 문제 해결

**All buttons show alert? / 모든 버튼에 경고 표시?**
- DirectPipe is not running or WebSocket connection failed. / DirectPipe 미실행 또는 WebSocket 연결 실패.
- Check WebSocket port (default: 8765). / 포트 확인.

**Plugin keeps restarting? / 플러그인이 계속 재시작?**
- Check logs: `%APPDATA%\Elgato\StreamDeck\logs\` / 로그 확인
- Ensure `node_modules/` exists in the installed plugin folder. / 설치 폴더에 node_modules 확인.
- Node.js v20+ required. / Node.js v20+ 필요.

---

## Packaging / 패키징

### Build Icons / 아이콘 빌드

```bash
cd streamdeck-plugin
npm run icons    # SVG → PNG generation (requires sharp)
```

### Package Plugin / 플러그인 패키징

```bash
npm install -g @elgato/cli
cd streamdeck-plugin
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
