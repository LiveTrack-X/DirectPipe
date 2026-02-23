# DirectPipe Control API Reference / 제어 API 레퍼런스

DirectPipe exposes two network interfaces for external control: a WebSocket server for real-time bidirectional communication and an HTTP REST API for one-shot commands.

DirectPipe는 두 가지 네트워크 인터페이스를 제공한다: 실시간 양방향 통신용 WebSocket 서버와 원샷 커맨드용 HTTP REST API.

## Connection Details / 연결 정보

| Protocol | Default Port | Address |
|----------|-------------|---------|
| WebSocket | 8765 | ws://127.0.0.1 |
| HTTP | 8766 | http://127.0.0.1 |

Both bind to localhost only for security. Ports configurable in Settings > Controls > Server Ports. / 보안을 위해 localhost만 바인딩. 설정에서 포트 변경 가능.

---

## WebSocket API

### Connecting / 연결

```javascript
const ws = new WebSocket("ws://127.0.0.1:8765");
```

On connection, the server sends the current state as a JSON message. / 연결 시 현재 상태를 JSON으로 전송.

### Message Format / 메시지 형식

All messages are JSON with a `type` field. / 모든 메시지는 `type` 필드가 있는 JSON.

**Client → Server (action request):**
```json
{ "type": "action", "action": "<action_name>", "params": { ... } }
```

**Server → Client (state update):**
```json
{ "type": "state", "data": { ... } }
```

State updates are pushed automatically on every state change. / 상태 변경 시 자동 푸시.

---

### Actions / 액션

#### `plugin_bypass` — Toggle Plugin Bypass / 플러그인 Bypass 토글

```json
{ "type": "action", "action": "plugin_bypass", "params": { "index": 0 } }
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `index` | number | No | Plugin chain index (0-based, default: 0) / 체인 인덱스 |

---

#### `master_bypass` — Toggle Master Bypass / 마스터 Bypass 토글

```json
{ "type": "action", "action": "master_bypass", "params": {} }
```

---

#### `set_volume` — Set Volume / 볼륨 설정

```json
{ "type": "action", "action": "set_volume", "params": { "target": "monitor", "value": 0.75 } }
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `target` | string | No | `"input"`, `"virtual_mic"`, or `"monitor"` (default: `"monitor"`) |
| `value` | number | Yes | 0.0 (silent) to 1.0 (full) |

---

#### `toggle_mute` — Toggle Mute / 뮤트 토글

```json
{ "type": "action", "action": "toggle_mute", "params": { "target": "monitor" } }
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `target` | string | No | `"input"`, `"virtual_mic"`, `"monitor"`, or `""` (all) |

---

#### `panic_mute` — Panic Mute / 패닉 뮤트

```json
{ "type": "action", "action": "panic_mute", "params": {} }
```

Immediately mutes all outputs. Send again to unmute. / 전체 뮤트. 재전송 시 해제.

---

#### `input_gain` — Adjust Input Gain / 입력 게인 조절

```json
{ "type": "action", "action": "input_gain", "params": { "delta": 1.0 } }
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `delta` | number | Yes | Gain change in dB (+louder, -quieter) / dB 단위 게인 변화 |

---

#### `input_mute_toggle` — Toggle Input Mute / 입력 뮤트 토글

```json
{ "type": "action", "action": "input_mute_toggle", "params": {} }
```

---

#### `switch_preset_slot` — Switch Preset Slot / 프리셋 슬롯 전환

```json
{ "type": "action", "action": "switch_preset_slot", "params": { "slot": 2 } }
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `slot` | number | Yes | 0=A, 1=B, 2=C, 3=D, 4=E |

---

#### `load_preset` — Load Preset by Index / 프리셋 로드

```json
{ "type": "action", "action": "load_preset", "params": { "index": 2 } }
```

---

#### `next_preset` / `previous_preset` — Cycle Presets / 프리셋 순환

```json
{ "type": "action", "action": "next_preset", "params": {} }
```

---

### State Object / 상태 객체

```json
{
  "type": "state",
  "data": {
    "plugins": [
      { "name": "ReaComp", "bypass": false, "loaded": true },
      { "name": "ReaEQ", "bypass": true, "loaded": true }
    ],
    "volumes": { "input": 1.0, "virtual_mic": 0.8, "monitor": 0.6 },
    "master_bypassed": false,
    "muted": false,
    "input_muted": false,
    "active_slot": 0,
    "preset": "Streaming Vocal",
    "latency_ms": 5.2,
    "level_db": -18.3,
    "cpu_percent": 3.1,
    "sample_rate": 48000,
    "buffer_size": 128,
    "channel_mode": 1
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `plugins` | array | Plugin objects in chain / 체인 내 플러그인 |
| `plugins[].name` | string | Plugin name / 플러그인 이름 |
| `plugins[].bypass` | boolean | Bypassed / Bypass 여부 |
| `plugins[].loaded` | boolean | Loaded (slot not empty) / 로드 여부 |
| `volumes.input` | number | Input gain (0.0–1.0) / 입력 게인 |
| `volumes.virtual_mic` | number | Virtual mic volume (0.0–1.0) / 가상 마이크 볼륨 |
| `volumes.monitor` | number | Monitor volume (0.0–1.0) / 모니터 볼륨 |
| `master_bypassed` | boolean | Entire chain bypassed / 전체 체인 Bypass |
| `muted` | boolean | Panic mute active / 패닉 뮤트 상태 |
| `input_muted` | boolean | Input muted / 입력 뮤트 |
| `active_slot` | number | Active preset slot (0–4) / 활성 슬롯 |
| `preset` | string | Current preset name / 현재 프리셋 이름 |
| `latency_ms` | number | Latency in ms / 레이턴시 (ms) |
| `level_db` | number | Input level in dBFS / 입력 레벨 (dBFS) |
| `cpu_percent` | number | Audio CPU usage % / 오디오 CPU 사용률 |
| `sample_rate` | number | Sample rate (Hz) / 샘플레이트 |
| `buffer_size` | number | Buffer size (samples) / 버퍼 크기 |
| `channel_mode` | number | 1=Mono, 2=Stereo |

---

## HTTP REST API

Simple GET endpoints for one-shot commands. / 원샷 커맨드용 GET 엔드포인트.

Base URL: `http://127.0.0.1:8766`

| Endpoint | Description |
|----------|-------------|
| `GET /api/status` | Full state (same as WebSocket state) / 전체 상태 |
| `GET /api/bypass/:index/toggle` | Toggle plugin bypass / 플러그인 Bypass 토글 |
| `GET /api/bypass/master/toggle` | Toggle master bypass / 마스터 Bypass 토글 |
| `GET /api/mute/toggle` | Toggle mute / 뮤트 토글 |
| `GET /api/mute/panic` | Panic mute / 패닉 뮤트 |
| `GET /api/volume/:target/:value` | Set volume (target: `input`, `virtual_mic`, `monitor`; value: 0.0–1.0) / 볼륨 설정 |
| `GET /api/preset/:index` | Load preset / 프리셋 로드 |
| `GET /api/slot/:index` | Switch preset slot (0–4) / 슬롯 전환 |
| `GET /api/input-mute/toggle` | Toggle input mute / 입력 뮤트 토글 |
| `GET /api/gain/:delta` | Adjust input gain (dB) / 입력 게인 조절 |

All responses return JSON. Success: `{ "ok": true, "action": "..." }`. Error: `{ "error": "..." }`.

모든 응답은 JSON. 성공: `{ "ok": true }`, 에러: `{ "error": "..." }`.

---

## Examples / 예제

### curl

```bash
# Get state / 상태 조회
curl http://127.0.0.1:8766/api/status

# Toggle first plugin bypass / 첫 번째 플러그인 Bypass
curl http://127.0.0.1:8766/api/bypass/0/toggle

# Panic mute / 패닉 뮤트
curl http://127.0.0.1:8766/api/mute/panic

# Set monitor volume to 50% / 모니터 볼륨 50%
curl http://127.0.0.1:8766/api/volume/monitor/0.5

# Switch to slot C / 슬롯 C로 전환
curl http://127.0.0.1:8766/api/slot/2
```

### Python (WebSocket)

```python
import asyncio, websockets, json

async def main():
    async with websockets.connect("ws://127.0.0.1:8765") as ws:
        state = json.loads(await ws.recv())
        print("State:", state)

        await ws.send(json.dumps({
            "type": "action",
            "action": "master_bypass",
            "params": {}
        }))

        async for msg in ws:
            print("Update:", json.loads(msg))

asyncio.run(main())
```

### Node.js (WebSocket)

```javascript
const WebSocket = require("ws");
const ws = new WebSocket("ws://127.0.0.1:8765");

ws.on("open", () => {
    ws.send(JSON.stringify({
        type: "action",
        action: "set_volume",
        params: { target: "monitor", value: 0.8 }
    }));
});

ws.on("message", (data) => {
    console.log("State:", JSON.parse(data));
});
```
