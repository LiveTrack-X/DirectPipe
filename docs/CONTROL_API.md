# DirectPipe Control API Reference

DirectPipe exposes two network interfaces for external control: a WebSocket server for real-time bidirectional communication and an HTTP REST API for simple one-shot commands.

## Connection Details

| Protocol  | Default Port | Address         |
|-----------|-------------|-----------------|
| WebSocket | 8765        | ws://127.0.0.1  |
| HTTP      | 8766        | http://127.0.0.1|

Both servers bind to `127.0.0.1` (localhost only) for security. Ports can be changed in DirectPipe settings (Settings > Control > Server Ports).

If the default port is unavailable, the WebSocket server will try ports 8766-8770 as fallbacks. The actual port is reported in the DirectPipe log.

---

## WebSocket API

### Connecting

```javascript
const ws = new WebSocket("ws://127.0.0.1:8765");
```

Upon connection, the server immediately sends the current application state as a JSON message.

### Message Format

All messages are JSON objects with a `type` field.

#### Client to Server: Action Requests

```json
{
  "type": "action",
  "action": "<action_name>",
  "params": { ... }
}
```

#### Server to Client: State Updates

```json
{
  "type": "state",
  "data": { ... }
}
```

State updates are pushed automatically whenever the application state changes (bypass toggled, volume adjusted, preset loaded, etc.).

---

### Action Messages

#### `plugin_bypass` — Toggle Plugin Bypass

Toggle bypass for a specific VST plugin in the chain.

**Request:**
```json
{
  "type": "action",
  "action": "plugin_bypass",
  "params": {
    "index": 0
  }
}
```

| Parameter | Type   | Required | Description                           |
|-----------|--------|----------|---------------------------------------|
| `index`   | number | No       | Plugin chain index (0-based). Default: 0 |

---

#### `master_bypass` — Toggle Master Bypass

Toggle bypass for the entire VST processing chain.

**Request:**
```json
{
  "type": "action",
  "action": "master_bypass",
  "params": {}
}
```

No parameters required.

---

#### `set_volume` — Set Volume

Set the volume level for a specific audio target.

**Request:**
```json
{
  "type": "action",
  "action": "set_volume",
  "params": {
    "target": "monitor",
    "value": 0.75
  }
}
```

| Parameter | Type   | Required | Description                                      |
|-----------|--------|----------|--------------------------------------------------|
| `target`  | string | No       | `"input"`, `"virtual_mic"`, or `"monitor"`. Default: `"monitor"` |
| `value`   | number | Yes      | Volume level from `0.0` (silent) to `1.0` (full) |

---

#### `toggle_mute` — Toggle Mute

Toggle mute for a specific audio target.

**Request:**
```json
{
  "type": "action",
  "action": "toggle_mute",
  "params": {
    "target": "monitor"
  }
}
```

| Parameter | Type   | Required | Description                                          |
|-----------|--------|----------|------------------------------------------------------|
| `target`  | string | No       | `"input"`, `"virtual_mic"`, or `"monitor"`. Default: `""` (all) |

---

#### `load_preset` — Load Preset by Index

Load a saved preset by its index.

**Request:**
```json
{
  "type": "action",
  "action": "load_preset",
  "params": {
    "index": 2
  }
}
```

| Parameter | Type   | Required | Description              |
|-----------|--------|----------|--------------------------|
| `index`   | number | Yes      | Preset index (0-based)   |

---

#### `panic_mute` — Panic Mute

Immediately mute all audio outputs. Sending again toggles the mute off.

**Request:**
```json
{
  "type": "action",
  "action": "panic_mute",
  "params": {}
}
```

No parameters required.

---

#### `input_gain` — Adjust Input Gain

Adjust the microphone input gain by a relative amount in dB.

**Request:**
```json
{
  "type": "action",
  "action": "input_gain",
  "params": {
    "delta": 1.0
  }
}
```

| Parameter | Type   | Required | Description                        |
|-----------|--------|----------|------------------------------------|
| `delta`   | number | Yes      | Gain change in dB (positive = louder, negative = quieter) |

---

### State Object

The state message contains a complete snapshot of the DirectPipe application state.

**Full example:**
```json
{
  "type": "state",
  "data": {
    "plugins": [
      {
        "name": "ReaComp",
        "bypass": false,
        "loaded": true
      },
      {
        "name": "ReaEQ",
        "bypass": true,
        "loaded": true
      }
    ],
    "volumes": {
      "input": 1.0,
      "virtual_mic": 0.8,
      "monitor": 0.6
    },
    "master_bypassed": false,
    "muted": false,
    "input_muted": false,
    "preset": "Streaming Vocal",
    "latency_ms": 5.2,
    "level_db": -18.3,
    "cpu_percent": 3.1,
    "sample_rate": 48000,
    "buffer_size": 128,
    "channel_mode": 1,
    "driver_connected": true
  }
}
```

#### State Fields

| Field              | Type     | Description                                        |
|--------------------|----------|----------------------------------------------------|
| `plugins`          | array    | Array of plugin objects in the processing chain     |
| `plugins[].name`   | string   | Plugin display name                                |
| `plugins[].bypass` | boolean  | Whether the plugin is currently bypassed           |
| `plugins[].loaded` | boolean  | Whether the plugin is loaded (slot is not empty)   |
| `volumes.input`    | number   | Input gain level (0.0-1.0)                         |
| `volumes.virtual_mic` | number | Virtual microphone output volume (0.0-1.0)      |
| `volumes.monitor`  | number   | Monitor output volume (0.0-1.0)                    |
| `master_bypassed`  | boolean  | Whether the entire plugin chain is bypassed        |
| `muted`            | boolean  | Whether all outputs are muted (panic mute state)   |
| `input_muted`      | boolean  | Whether the microphone input is muted              |
| `preset`           | string   | Name of the currently loaded preset                |
| `latency_ms`       | number   | Current end-to-end latency in milliseconds         |
| `level_db`         | number   | Current input level in dBFS                        |
| `cpu_percent`      | number   | Audio processing CPU usage percentage              |
| `sample_rate`      | number   | Audio sample rate in Hz (e.g., 48000)              |
| `buffer_size`      | number   | Audio buffer size in samples                       |
| `channel_mode`     | number   | Channel mode: 1 = Mono, 2 = Stereo                |
| `driver_connected` | boolean  | Whether the virtual audio driver is connected      |

---

## HTTP REST API

The HTTP API provides simple GET endpoints that can be called from any HTTP client, including the Stream Deck's built-in "Website" action.

### Base URL

```
http://127.0.0.1:8766
```

### Endpoints

All endpoints return JSON responses.

#### `GET /api/status`

Returns the complete application state (same format as the WebSocket state message).

**Response:**
```json
{
  "type": "state",
  "data": { ... }
}
```

---

#### `GET /api/bypass/:index/toggle`

Toggle bypass for a specific plugin.

**Example:**
```
GET http://127.0.0.1:8766/api/bypass/0/toggle
```

**Response:**
```json
{ "ok": true, "action": "plugin_bypass", "index": 0 }
```

---

#### `GET /api/bypass/master/toggle`

Toggle master bypass for the entire processing chain.

**Example:**
```
GET http://127.0.0.1:8766/api/bypass/master/toggle
```

**Response:**
```json
{ "ok": true, "action": "master_bypass" }
```

---

#### `GET /api/mute/toggle`

Toggle mute on all outputs.

**Example:**
```
GET http://127.0.0.1:8766/api/mute/toggle
```

**Response:**
```json
{ "ok": true, "action": "toggle_mute" }
```

---

#### `GET /api/mute/panic`

Trigger panic mute (immediately mute all outputs).

**Example:**
```
GET http://127.0.0.1:8766/api/mute/panic
```

**Response:**
```json
{ "ok": true, "action": "panic_mute" }
```

---

#### `GET /api/volume/:target/:value`

Set the volume for a specific target.

**Example:**
```
GET http://127.0.0.1:8766/api/volume/monitor/0.75
```

| Parameter | Description                                   |
|-----------|-----------------------------------------------|
| `:target` | `input`, `virtual_mic`, or `monitor`          |
| `:value`  | Volume level from `0.0` to `1.0`              |

**Response:**
```json
{ "ok": true, "action": "set_volume", "target": "monitor", "value": 0.75 }
```

---

#### `GET /api/preset/:index`

Load a preset by index.

**Example:**
```
GET http://127.0.0.1:8766/api/preset/2
```

**Response:**
```json
{ "ok": true, "action": "load_preset", "index": 2 }
```

---

#### `GET /api/gain/:delta`

Adjust input gain by a relative amount in dB.

**Example:**
```
GET http://127.0.0.1:8766/api/gain/-1
```

**Response:**
```json
{ "ok": true, "action": "input_gain", "delta": -1.0 }
```

---

## Error Responses

### WebSocket

Invalid action messages are silently ignored. The server does not send error responses for unrecognized actions.

### HTTP

| Status Code | Meaning                                  |
|-------------|------------------------------------------|
| 200         | Action executed successfully             |
| 400         | Invalid request (bad parameters)         |
| 404         | Unknown endpoint                         |
| 500         | Internal server error                    |

**Error response format:**
```json
{ "ok": false, "error": "Unknown endpoint" }
```

---

## Usage Examples

### curl (Command Line)

```bash
# Get current state
curl http://127.0.0.1:8766/api/status

# Toggle bypass on first plugin
curl http://127.0.0.1:8766/api/bypass/0/toggle

# Panic mute
curl http://127.0.0.1:8766/api/mute/panic

# Set monitor volume to 50%
curl http://127.0.0.1:8766/api/volume/monitor/0.5
```

### Python (WebSocket)

```python
import asyncio
import websockets
import json

async def control_directpipe():
    async with websockets.connect("ws://127.0.0.1:8765") as ws:
        # Receive initial state
        state = json.loads(await ws.recv())
        print("Current state:", state)

        # Toggle master bypass
        await ws.send(json.dumps({
            "type": "action",
            "action": "master_bypass",
            "params": {}
        }))

        # Listen for state updates
        async for message in ws:
            update = json.loads(message)
            print("State update:", update)

asyncio.run(control_directpipe())
```

### Node.js (WebSocket)

```javascript
const WebSocket = require("ws");

const ws = new WebSocket("ws://127.0.0.1:8765");

ws.on("open", () => {
    // Set monitor volume to 80%
    ws.send(JSON.stringify({
        type: "action",
        action: "set_volume",
        params: { target: "monitor", value: 0.8 }
    }));
});

ws.on("message", (data) => {
    const state = JSON.parse(data);
    console.log("State:", state);
});
```
