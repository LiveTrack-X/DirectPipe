# DirectPipe Stream Deck Plugin / 스트림 덱 플러그인

## Overview / 개요

The DirectPipe Stream Deck plugin connects to the host via WebSocket and provides 4 button actions for controlling the VST host remotely.

DirectPipe Stream Deck 플러그인은 WebSocket으로 호스트에 연결하여 4가지 버튼 액션으로 VST 호스트를 원격 제어한다.

| Action / 액션 | Description / 설명 |
|---------------|-------------------|
| **Bypass Toggle** | Toggle VST plugin bypass. Long-press for master bypass. / 플러그인 Bypass 토글. 길게 누르면 마스터 Bypass. |
| **Panic Mute** | Mute all outputs instantly. / 전체 출력 즉시 뮤트. |
| **Volume Control** | Press to toggle mute, dial to adjust volume. / 눌러서 뮤트 토글, 다이얼로 볼륨 조절. |
| **Preset Switch** | Switch preset slot (A-E) or cycle presets. / 프리셋 슬롯 전환 또는 순환. |

---

## Prerequisites / 요구 사항

- **DirectPipe** running / DirectPipe 실행 중
- **Elgato Stream Deck** software v6.4+ / Stream Deck 소프트웨어 v6.4+
- **Node.js** v18+ / Node.js v18+

---

## Installation / 설치

1. Copy `streamdeck-plugin/` folder to the Stream Deck plugins directory: / 플러그인 디렉토리에 복사:
   ```
   %APPDATA%\Elgato\StreamDeck\Plugins\com.directpipe.sdPlugin
   ```

2. Install dependencies: / 의존성 설치:
   ```bash
   cd com.directpipe.sdPlugin
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

**UUID:** `com.directpipe.bypass-toggle`

- **Short press** — Toggle bypass for configured plugin index / 짧게 누름 → 설정된 플러그인 Bypass
- **Long press** (>500ms) — Toggle master bypass / 길게 누름 → 마스터 Bypass

**Display:** Plugin name + "Bypassed" or "Active". Master bypass shows "MASTER Bypassed". / 플러그인 이름 + 상태 표시.

**Settings:** `pluginIndex` (number, default: 0) — Set via Property Inspector / PI에서 설정

---

### Panic Mute / 패닉 뮤트

**UUID:** `com.directpipe.panic-mute`

- **Press** — Toggle panic mute on all outputs / 전체 출력 뮤트 토글

**Display:** State 0 = "MUTE" (normal), State 1 = "MUTED" (red indicator) / 상태 표시

No settings required. / 설정 불필요.

---

### Volume Control / 볼륨 제어

**UUID:** `com.directpipe.volume-control`

- **Press** — Toggle mute for target / 대상 뮤트 토글
- **Dial rotate** (Stream Deck+) — Adjust volume ±5% per tick / 다이얼로 볼륨 ±5% 조절

**Display:** Target name + volume % or "MUTED" / 대상 이름 + 볼륨 % 또는 "MUTED"

**Settings:** `target` — `"monitor"`, `"input"`, or `"virtual_mic"`. Set via Property Inspector. / PI에서 대상 선택.

---

### Preset Switch / 프리셋 전환

**UUID:** `com.directpipe.preset-switch`

- **With slot configured** — Switch to specific slot / 슬롯 설정 시 → 해당 슬롯으로 전환
- **Without config** — Cycle to next preset / 미설정 시 → 다음 프리셋 순환

**Display:** Slot label (A-E) with active indicator / 슬롯 라벨 + 활성 표시

**Settings:** `slotIndex` (0–4 for A-E, or "cycle"). Set via Property Inspector. / PI에서 슬롯 선택.

---

## File Structure / 파일 구조

```
streamdeck-plugin/
  manifest.json               SDK v2 plugin manifest / 플러그인 매니페스트
  package.json                Node.js package (ws v8.16.0)
  src/
    plugin.js                 Main entry, routes SD events to handlers / 메인 진입점
    websocket-client.js       WebSocket client with auto-reconnect / 자동 재연결 WS 클라이언트
    actions/
      bypass-toggle.js        Bypass toggle action / Bypass 토글 액션
      panic-mute.js           Panic mute action / 패닉 뮤트 액션
      volume-control.js       Volume control action / 볼륨 제어 액션
      preset-switch.js        Preset switch action / 프리셋 전환 액션
    inspectors/
      bypass-pi.html          Bypass settings UI / Bypass 설정 UI
      volume-pi.html          Volume settings UI / 볼륨 설정 UI
      preset-pi.html          Preset settings UI / 프리셋 설정 UI
  images/                     Button icons (72x72 PNG) / 버튼 아이콘
```

---

## Troubleshooting / 문제 해결

**All buttons show alert? / 모든 버튼에 경고 표시?**
- DirectPipe is not running or WebSocket connection failed. / DirectPipe 미실행 또는 WebSocket 연결 실패.
- Check WebSocket port (default: 8765). / 포트 확인.

**Plugin keeps restarting? / 플러그인이 계속 재시작?**
- Check logs: `%APPDATA%\Elgato\StreamDeck\logs\` / 로그 확인
- Run `npm install` if dependencies are missing. / 의존성 누락 시 `npm install` 실행.
- Node.js v18+ required. / Node.js v18+ 필요.

---

## API Reference / API 레퍼런스

See [CONTROL_API.md](./CONTROL_API.md) for the complete WebSocket and HTTP API. / 전체 API는 CONTROL_API.md 참조.
