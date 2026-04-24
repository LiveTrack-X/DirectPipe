# DirectPipe Control API Reference / 제어 API 레퍼런스

DirectPipe exposes two network interfaces for external control: a WebSocket server for real-time bidirectional communication and an HTTP REST API for one-shot commands.

DirectPipe는 두 가지 네트워크 인터페이스를 제공한다: 실시간 양방향 통신용 WebSocket 서버와 원샷 커맨드용 HTTP REST API.

## Connection Details / 연결 정보

| Protocol | Default Port | Address |
|----------|-------------|---------|
| WebSocket | 8765 | ws://127.0.0.1 |
| HTTP | 8766 | http://127.0.0.1 |
| UDP Discovery | 8767 | 127.0.0.1 (broadcast) |

Both WebSocket and HTTP bind to localhost only for security. Ports configurable in Controls > Stream Deck tab (accessible even without Stream Deck installed). DirectPipe sends a `DIRECTPIPE_READY:<port>` UDP packet to port 8767 when the WebSocket server starts, enabling instant client connection without polling.

WebSocket과 HTTP는 보안을 위해 localhost만 바인딩. 포트는 Controls > Stream Deck 탭에서 변경 가능 (Stream Deck 미설치 상태에서도 접근 가능). DirectPipe는 WebSocket 서버 시작 시 UDP 8767로 `DIRECTPIPE_READY:<port>` 패킷을 전송하여 클라이언트가 폴링 없이 즉시 연결 가능.

---

## WebSocket API

### Connecting / 연결

```javascript
const ws = new WebSocket("ws://127.0.0.1:8765");
```

On connection, the server sends the current state as a JSON message. / 연결 시 현재 상태를 JSON으로 전송.

### Message Format / 메시지 형식

All messages are JSON with a `type` field. / 모든 메시지는 `type` 필드가 있는 JSON.

**Client -> Server (action request):**
```json
{ "type": "action", "action": "<action_name>", "params": { ... } }
```

**Server -> Client (state update):**
```json
{ "type": "state", "data": { ... } }
```

State updates are pushed automatically on every state change. / 상태 변경 시 자동 푸시.

> **State Broadcast:** The `active_slot` field is 0-5 (A-E + Auto) or -1 (none). The `auto_slot_active` boolean field is deprecated — it is auto-derived from `active_slot == 5` and kept for backward compatibility.
>
> **상태 브로드캐스트:** `active_slot` 필드는 0-5 (A-E + Auto) 또는 -1 (없음)입니다. `auto_slot_active` boolean 필드는 deprecated — `active_slot == 5`에서 자동 파생되며 하위 호환성을 위해 유지됩니다.

### Connection Notes / 연결 참고

- Server implements RFC 6455 with custom SHA-1 handshake / RFC 6455 구현 (커스텀 SHA-1)
- Dead clients are automatically cleaned up during broadcast / 브로드캐스트 시 죽은 클라이언트 자동 정리
- Multiple clients can connect simultaneously / 다중 클라이언트 동시 연결 가능

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
| `target` | string | No | `"monitor"`, `"output"`, or `"input"` (default: `"monitor"`) |
| `value` | number | Yes | 0.0-1.0 for monitor/output, 0.0-2.0 for input gain multiplier |

---

#### `toggle_mute` — Toggle Mute / 뮤트 토글

```json
{ "type": "action", "action": "toggle_mute", "params": { "target": "monitor" } }
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `target` | string | No | `"input"`, `"output"`, `"monitor"`, or `""` (all) |

---

#### `panic_mute` — Panic Mute / 패닉 뮤트

```json
{ "type": "action", "action": "panic_mute", "params": {} }
```

Optional explicit-set mode:

```json
{ "type": "action", "action": "panic_mute", "params": { "muted": true } }
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `muted` | boolean | No | Explicit panic state set (`true`=engage, `false`=release). If omitted, legacy toggle behavior is used. / 패닉 상태를 명시적으로 설정 (`true`=켜기, `false`=해제). 생략 시 레거시 토글 동작 사용 |

Immediately mutes all output paths and stops active recording. Send again to unmute (previous monitor/output/IPC state is restored; recording does not auto-restart). During panic mute, most actions (bypass, volume, preset, recording, etc.) are blocked; `input_mute_toggle`, `xrun_reset`, limiter controls, and `auto_add` are allowed for maintenance/prep use. / 패닉 뮤트 (출력 경로 전체 차단) + 녹음 자동 중지. 재전송 시 해제 (이전 모니터/출력/IPC 상태 복원; 녹음은 자동 재시작 안 함). 패닉 뮤트 중 대부분의 액션(바이패스, 볼륨, 프리셋, 녹음 등)은 차단되며, 유지보수/준비 용도로 `input_mute_toggle`, `xrun_reset`, 리미터 제어, `auto_add`는 허용됩니다.

---

#### `input_gain` — Adjust Input Gain / 입력 게인 조절

```json
{ "type": "action", "action": "input_gain", "params": { "delta": 1.0 } }
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `delta` | number | Yes | Step count (±1 = ±0.1 linear gain, range 0.0-2.0). Default: 1.0 / 스텝 수 (±1 = ±0.1 선형 게인 변화) |

> **Note**: `delta`는 dB가 아닌 스텝 카운트입니다. 1 스텝 = 선형 게인 0.1 변화. 실제 게인을 직접 설정하려면 `set_volume` 액션에 `target: "input"`, `value: 0.0-2.0`을 사용하세요.
> `delta` is a step count, NOT dB. 1 step = 0.1 linear gain change. To set absolute gain, use `set_volume` with `target: "input"` and `value: 0.0-2.0`.

---

#### `input_mute_toggle` — Toggle Input Mute / 입력 뮤트 토글

```json
{ "type": "action", "action": "input_mute_toggle", "params": {} }
```

Toggles independent input mute. When muted, microphone input is silenced but the VST chain continues processing (reverb tails fade naturally, AGC enters freeze). Different from `panic_mute` which stops all processing.

독립 입력 뮤트를 토글합니다. 뮤트 시 마이크 입력이 무음이 되지만 VST 체인은 계속 처리됩니다 (리버브 테일 자연 감쇠, AGC 프리즈 진입). 전체 처리를 중단하는 `panic_mute`와 다릅니다.

---

#### `monitor_toggle` — Toggle Monitor Output / 모니터 출력 토글

```json
{ "type": "action", "action": "monitor_toggle", "params": {} }
```

Toggles the monitor output (headphones) on/off. / 모니터 출력(헤드폰) 켜기/끄기 토글.

---

#### `switch_preset_slot` — Switch Preset Slot / 프리셋 슬롯 전환

```json
{ "type": "action", "action": "switch_preset_slot", "params": { "slot": 2 } }
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `slot` | number | Yes | 0=A, 1=B, 2=C, 3=D, 4=E |

> **Auto 슬롯**: WebSocket에서 Auto 슬롯(index 5)을 활성화하려면 `auto_processors_add` 액션을 사용하세요. `switch_preset_slot`은 A-E(0-4)만 지원합니다. HTTP에서는 `/api/preset/5`로 Auto 접근 가능.
> **Auto slot**: To activate the Auto slot (index 5) via WebSocket, use the `auto_processors_add` action. `switch_preset_slot` supports A-E (0-4) only. Via HTTP, `/api/preset/5` accesses Auto.

---

#### `load_preset` — Load Preset by Index / 프리셋 로드

```json
{ "type": "action", "action": "load_preset", "params": { "index": 2 } }
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `index` | number | Yes | 0=A, 1=B, 2=C, 3=D, 4=E, 5=Auto. HTTP equivalent: `/api/preset/:index` / 슬롯 인덱스. HTTP: `/api/preset/:index` |

> `load_preset`과 `switch_preset_slot`의 차이: `load_preset`은 슬롯 0-5(Auto 포함)를 로드, `switch_preset_slot`은 A-E(0-4)만 전환. Auto 슬롯은 `load_preset(index=5)` 또는 `auto_processors_add`로 접근.
> Difference from `switch_preset_slot`: `load_preset` loads slots 0-5 (including Auto), `switch_preset_slot` switches A-E (0-4) only. Auto slot is accessed via `load_preset(index=5)` or `auto_processors_add`.

---

#### `next_preset` — Next Preset / 다음 프리셋

```json
{ "type": "action", "action": "next_preset", "params": {} }
```

Cycles forward to the next occupied preset slot. / 다음 사용 중인 프리셋 슬롯으로 이동.

---

#### `previous_preset` — Previous Preset / 이전 프리셋

```json
{ "type": "action", "action": "previous_preset", "params": {} }
```

Cycles backward to the previous occupied preset slot. / 이전 사용 중인 프리셋 슬롯으로 이동.

> **참고**: `next_preset`과 `previous_preset`은 WebSocket 전용입니다. HTTP API에서는 `/api/preset/:index`로 특정 슬롯을 직접 지정하세요.
> **Note**: `next_preset` and `previous_preset` are WebSocket-only. For HTTP, use `/api/preset/:index` to specify the target slot directly.

---

#### `recording_toggle` — Toggle Recording / 녹음 토글

```json
{ "type": "action", "action": "recording_toggle", "params": {} }
```

Start or stop recording processed audio to a WAV file. Recording files are saved to the user's Documents folder. Blocked during panic mute. Recording is also automatically stopped when panic mute engages. / 처리된 오디오의 WAV 녹음 시작/중지. 녹음 파일은 사용자 문서 폴더에 저장. 패닉 뮤트 중 차단됨. 패닉 뮤트 활성화 시 녹음 자동 중지.

---

#### `ipc_toggle` — Toggle IPC Output / IPC 출력 토글

```json
{ "type": "action", "action": "ipc_toggle", "params": {} }
```

Toggles the IPC output (DirectPipe Receiver) on/off. When enabled, processed audio is written to shared memory for the DirectPipe Receiver plugin (e.g., in OBS). / IPC 출력(DirectPipe Receiver) 켜기/끄기 토글. 활성화 시 처리된 오디오가 공유 메모리로 기록되어 DirectPipe Receiver 플러그인(예: OBS)에서 사용 가능.

> **Note**: Receiver VST는 입력 버스가 없는 출력 전용 플러그인으로, OBS 오디오 소스의 마이크 입력은 무시하고 DirectPipe에서 IPC로 전송된 오디오만 출력합니다. `ipc_toggle`은 사실상 **OBS 방송 마이크의 독립 뮤트 스위치** 역할을 합니다 — `toggle_mute`(메인 출력/Discord)와 독립적으로 동작합니다.
>
> **Note**: Receiver VST is an output-only plugin (no input bus) — it ignores OBS source audio and only outputs what DirectPipe sends via IPC. `ipc_toggle` effectively acts as an **independent mute switch for the OBS stream mic** — it works independently from `toggle_mute` (main output/Discord).

---

#### `set_plugin_parameter` — Set Plugin Parameter / 플러그인 파라미터 설정

```json
{ "type": "action", "action": "set_plugin_parameter", "params": { "pluginIndex": 0, "paramIndex": 3, "value": 0.75 } }
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `pluginIndex` | number | Yes | Plugin chain index (0-based) / 체인 인덱스 |
| `paramIndex` | number | Yes | Parameter index (0-based) / 파라미터 인덱스 |
| `value` | number | Yes | Parameter value (0.0-1.0) / 파라미터 값 |

---

#### `auto_processors_add` — Add Auto Processors / 자동 프로세서 추가

```json
{ "type": "action", "action": "auto_processors_add", "params": {} }
```

Adds built-in Filter + NoiseRemoval + AutoGain processors to the chain. Already-present types are skipped. Not blocked by panic mute. / 내장 Filter + NoiseRemoval + AutoGain 프로세서를 체인에 추가. 이미 있는 타입은 스킵. 패닉 뮤트에 의해 차단되지 않음.

---

#### `xrun_reset` — Reset XRun Counter / XRun 카운터 리셋

```json
{ "type": "action", "action": "xrun_reset", "params": {} }
```

Resets the rolling 60-second XRun counter. Not blocked by panic mute. / 60초 롤링 XRun 카운터 리셋. 패닉 뮤트에 의해 차단되지 않음.

---

#### `safety_limiter_toggle` — Toggle Safety Guard (legacy name) / Safety Guard 토글 (레거시 이름)

```json
{ "type": "action", "action": "safety_limiter_toggle", "params": {} }
```

Toggles the global Safety Guard on/off (legacy action name kept for compatibility). Not blocked by panic mute. / 전역 Safety Guard 켜기/끄기 토글 (호환성을 위해 레거시 액션 이름 유지). 패닉 뮤트에 의해 차단되지 않음.

---

#### `set_safety_limiter_ceiling` — Set Safety Guard Ceiling (legacy name) / Safety Guard 실링 설정 (레거시 이름)

```json
{ "type": "action", "action": "set_safety_limiter_ceiling", "params": { "value": -0.5 } }
```

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `value` | number | Yes | Ceiling in dBFS (-6.0 to 0.0) / 실링 값 (dBFS) |

Not blocked by panic mute. / 패닉 뮤트에 의해 차단되지 않음.

---

### State Object / 상태 객체

```json
{
  "type": "state",
  "data": {
    "plugins": [
      { "name": "ReaComp", "bypass": false, "loaded": true, "latency_samples": 0, "type": "vst" },
      { "name": "ReaEQ", "bypass": true, "loaded": true, "latency_samples": 0, "type": "vst" }
    ],
    "volumes": { "input": 1.0, "monitor": 0.6, "output": 1.0 },
    "master_bypassed": false,
    "muted": false,
    "output_muted": false,
    "input_muted": false,
    "preset": "Streaming Vocal",
    "latency_ms": 5.2,
    "monitor_latency_ms": 3.8,
    "level_db": -18.3,
    "cpu_percent": 3.1,
    "sample_rate": 48000,
    "buffer_size": 128,
    "channel_mode": 2,
    "monitor_enabled": true,
    "active_slot": 0,
    "auto_slot_active": false,
    "recording": false,
    "recording_seconds": 0.0,
    "ipc_enabled": false,
    "device_lost": false,
    "monitor_lost": false,
    "xrun_count": 0,
    "slot_names": ["게임", "토크", "", "", "", "Auto"],
    "safety_limiter": {
      "enabled": true,
      "ceiling_dB": -0.3,
      "headroom_enabled": true,
      "headroom_dB": -0.3,
      "gain_reduction_dB": 0.0,
      "is_limiting": false
    },
    "chain_pdc_samples": 0,
    "chain_pdc_ms": 0.0
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `plugins` | array | Plugin objects in chain / 체인 내 플러그인 |
| `plugins[].name` | string | Plugin name / 플러그인 이름 |
| `plugins[].bypass` | boolean | Bypassed / Bypass 여부 |
| `plugins[].loaded` | boolean | Loaded (slot not empty) / 로드 여부 |
| `plugins[].latency_samples` | number | Plugin-reported latency in samples / 플러그인 보고 레이턴시 (샘플) |
| `plugins[].type` | string | Plugin type: `"vst"`, `"builtin_filter"`, `"builtin_noise_removal"`, `"builtin_auto_gain"` / 플러그인 타입 |
| `volumes.input` | number | Input gain multiplier (0.0-2.0) / 입력 게인 배수 |
| `volumes.monitor` | number | Monitor volume (0.0-1.0) / 모니터 볼륨 |
| `volumes.output` | number | Output volume (0.0-1.0) / 출력 볼륨 |
| `master_bypassed` | boolean | Entire chain bypassed / 전체 체인 Bypass |
| `muted` | boolean | Panic mute active / 패닉 뮤트 상태 |
| `output_muted` | boolean | Main output muted / 메인 출력 뮤트 |
| `input_muted` | boolean | 독립 입력 뮤트 상태 (`muted`와 독립) / Independent input mute state (independent from `muted`) |
| `active_slot` | number | Active preset slot (0-5 = A-E + Auto) or -1 (none) / 활성 슬롯 (0-5, -1은 없음) |
| `auto_slot_active` | boolean | **Deprecated** — auto-derived from `active_slot == 5`. Kept for backward compat. / **Deprecated** — `active_slot == 5`에서 자동 파생. 하위 호환용. |
| `slot_names` | array | Slot names (6 strings (A-E + Auto), empty = unnamed) / 슬롯 이름 (6개 (A-E + Auto), 빈 문자열 = 이름 없음) |
| `preset` | string | Current preset name / 현재 프리셋 이름 |
| `latency_ms` | number | Latency in ms / 레이턴시 (ms) |
| `monitor_latency_ms` | number | Monitor output latency in ms / 모니터 출력 레이턴시 (ms) |
| `level_db` | number | Input level in dBFS / 입력 레벨 (dBFS) |
| `cpu_percent` | number | Audio CPU usage % / 오디오 CPU 사용률 |
| `sample_rate` | number | Sample rate (Hz) / 샘플레이트 |
| `buffer_size` | number | Buffer size (samples) / 버퍼 크기 |
| `xrun_count` | number | XRun count in rolling 60s window / 60초 롤링 윈도우 XRun 카운트 |
| `channel_mode` | number | 1=Mono, 2=Stereo (default: 2) |
| `monitor_enabled` | boolean | Monitor output enabled / 모니터 출력 활성화 |
| `recording` | boolean | Audio recording active / 오디오 녹음 중 |
| `recording_seconds` | number | Recording elapsed time in seconds / 녹음 경과 시간 (초) |
| `ipc_enabled` | boolean | IPC output (DirectPipe Receiver) enabled / IPC 출력 (DirectPipe Receiver) 활성화 |
| `safety_limiter` | object | Safety Guard state (legacy field name) / Safety Guard 상태 (레거시 필드 이름) |
| `safety_limiter.enabled` | boolean | Limiter enabled / 리미터 활성화 |
| `safety_limiter.ceiling_dB` | number | Ceiling in dBFS (-6.0 to 0.0) / 실링 (dBFS) |
| `safety_limiter.headroom_enabled` | boolean | Safety Volume final trim enabled / Safety Volume 최종 trim 활성화 |
| `safety_limiter.headroom_dB` | number | Safety Volume final trim in dB (-6.0 to 0.0, default -0.3) / Safety Volume trim (dB) |
| `safety_limiter.gain_reduction_dB` | number | Current gain reduction in dB / 현재 게인 리덕션 (dB) |
| `safety_limiter.is_limiting` | boolean | Currently limiting / 현재 리미팅 중 |
| `chain_pdc_samples` | number | Total plugin chain PDC in samples / 플러그인 체인 총 PDC (샘플) |
| `chain_pdc_ms` | number | Total plugin chain PDC in ms / 플러그인 체인 총 PDC (ms) |
| `device_lost` | boolean | Audio device disconnected / 오디오 장치 연결 끊김 |
| `monitor_lost` | boolean | Monitor device disconnected / 모니터 장치 연결 끊김 |

---

## HTTP REST API

Simple GET endpoints for one-shot commands. All responses return JSON. / 원샷 커맨드용 GET 엔드포인트. 모든 응답은 JSON.

Base URL: `http://127.0.0.1:8766`

| Endpoint | Description |
|----------|-------------|
| `GET /api/status` | Full state (same as WebSocket state object) / 전체 상태 |
| `GET /api/bypass/:index/toggle` | Toggle plugin bypass (0-based index) / 플러그인 Bypass 토글 |
| `GET /api/bypass/master` | Toggle master bypass / 마스터 Bypass 토글 |
| `GET /api/mute/toggle` | Toggle mute (all outputs) / 뮤트 토글 (전체) |
| `GET /api/mute/panic` | Panic mute / 패닉 뮤트 |
| `GET /api/volume/:target/:value` | Set volume (target: `input` [0.0-2.0], `monitor` [0.0-1.0], `output` [0.0-1.0]; validated) / 볼륨 설정 (범위 검증) |
| `GET /api/monitor/toggle` | Toggle monitor output on/off / 모니터 출력 토글 |
| `GET /api/preset/:index` | Load preset (0-5: 0-4=A-E, 5=Auto) / 프리셋 로드 (0-5: 0-4=A-E, 5=Auto) |
| `GET /api/slot/:index` | Switch preset slot (0-5, A-E + Auto) / 슬롯 전환 |
| `GET /api/input-mute/toggle` | Toggle input mute / 입력 뮤트 토글 |
| `GET /api/gain/:delta` | Adjust input gain (linear, e.g. 0.1 = +0.1 gain) / 입력 게인 조절 (선형, 예: 0.1 = +0.1 게인) |
| `GET /api/recording/toggle` | Toggle audio recording on/off / 오디오 녹음 토글 |
| `GET /api/ipc/toggle` | Toggle IPC output (DirectPipe Receiver) on/off / IPC 출력 (DirectPipe Receiver) 토글 |
| `GET /api/plugin/:pluginIndex/param/:paramIndex/:value` | Set plugin parameter (0.0-1.0) / 플러그인 파라미터 설정 |
| `GET /api/plugins` | List loaded plugins: `[{index, name, bypassed, loaded, parameterCount}]` / 로드된 플러그인 목록 |
| `GET /api/plugin/:idx/params` | List plugin parameters: `[{index, name, value}]` / 플러그인 파라미터 목록 |
| `GET /api/xrun/reset` | Reset XRun counter (bypasses ActionDispatcher, direct engine call) / XRun 카운터 리셋 (ActionDispatcher 우회, 엔진 직접 호출) |
| `GET /api/perf` | Performance stats: `{latencyMs, cpuPercent, sampleRate, bufferSize, xrunCount}` / 성능 통계 |
| `GET /api/limiter/toggle` | Toggle global Safety Guard on/off (legacy endpoint name) / 전역 Safety Guard 토글 (레거시 엔드포인트 이름) |
| `GET /api/limiter/ceiling/:value` | Set Safety Guard ceiling (-6.0 to 0.0 dBFS, legacy endpoint name) / Safety Guard 실링 설정 (레거시 엔드포인트 이름) |
| `GET /api/auto/add` | Add built-in Filter+NoiseRemoval+AutoGain processors / 내장 프로세서 자동 추가 |
| `GET /api/midi/cc/:channel/:number/:value` | Inject MIDI CC for testing (ch 1-16, cc 0-127, val 0-127) / MIDI CC 테스트 주입 |
| `GET /api/midi/note/:channel/:number/:velocity` | Inject MIDI Note for testing (ch 1-16, note 0-127, vel 0-127) / MIDI Note 테스트 주입 |

**Action success response:** `{ "ok": true, "action": "..." }`

**Data query responses:** `/api/status` returns `{ "type": "state", "data": { ... } }`, `/api/perf` returns `{ "latencyMs": ..., ... }`, `/api/plugins` and `/api/plugin/:idx/params` return JSON arrays.

**Error response:** `{ "error": "..." }`

CORS header `Access-Control-Allow-Origin: *` is included in all responses. CORS preflight (`OPTIONS`) requests are supported for browser-based clients. / 모든 응답에 CORS 헤더 포함. 브라우저 클라이언트를 위한 CORS preflight (`OPTIONS`) 요청 지원.

Read timeout: 3 seconds. Only `GET` method is accepted for API calls (other methods return 405). / 읽기 타임아웃: 3초. API 호출은 `GET` 메서드만 허용 (다른 메서드는 405 반환).

> **Input Validation:** All numeric parameters are strictly validated. Strings like `"abc0.5"` that contain non-numeric characters will be rejected with 400 Bad Request. Only pure numeric strings (e.g., `"0.5"`, `"-0.3"`, `"1.0e-3"`) are accepted.
>
> **입력 검증:** 모든 숫자 파라미터는 엄격하게 검증됩니다. `"abc0.5"`처럼 비숫자 문자가 포함된 문자열은 400 Bad Request로 거부됩니다. 순수 숫자 문자열(예: `"0.5"`, `"-0.3"`, `"1.0e-3"`)만 허용됩니다.

### HTTP 에러 시나리오 / HTTP Error Scenarios

| 시나리오 / Scenario | HTTP 상태 / Status | 응답 / Response |
|---|---|---|
| 존재하지 않는 엔드포인트 / Unknown endpoint | 404 | `{"error": "Not found"}` or `{"error": "Unknown endpoint"}` |
| GET 외 메서드 (OPTIONS 제외) / Non-GET method (except OPTIONS) | 405 | `{"error": "Method not allowed"}` |
| 잘못된 플러그인 인덱스 / Invalid plugin index | 400 | `{"error": "Invalid index"}` |
| 비숫자 값 입력 / Non-numeric value | 400 | `{"error": "value must be a number"}` |
| 볼륨 범위 밖 (0.0~1.0) / Volume out of range | 400 | `{"error": "value out of range"}` |
| 리미터 ceiling 범위 밖 (-6.0~0.0) / Ceiling out of range | 400 | `{"error": "ceiling must be -6.0 to 0.0 dBFS"}` |
| 존재하지 않는 플러그인 / Plugin not found | 404 | `{"error": "Plugin not found"}` |
| 패닉 뮤트 중 차단된 액션 / Action blocked during panic mute | 200 | `{"ok": true}` (ActionHandler에서 무시됨 / silently ignored by ActionHandler) |
| CORS preflight | 204 | Empty body + CORS headers |

> **Note**: 패닉 뮤트 중 대부분의 액션은 HTTP 200을 반환하지만 ActionHandler 내부에서 `engine_.isMuted()` 체크에 의해 무시됩니다. 차단 여부를 확인하려면 WebSocket 상태 브로드캐스트의 `muted` 필드를 모니터링하세요.
>
> Most actions return HTTP 200 during panic mute but are silently ignored by ActionHandler's `engine_.isMuted()` check. Monitor the `muted` field in WebSocket state broadcasts to detect if actions are being blocked.

### WebSocket 에러 응답 / WebSocket Error Responses

WebSocket 메시지에 대한 별도 에러 응답은 없습니다 — 잘못된 메시지(알 수 없는 명령, 잘못된 형식)는 무시됩니다. 성공한 액션의 결과는 다음 상태 브로드캐스트에 반영됩니다.

WebSocket messages have no dedicated error response — invalid messages (unknown commands, malformed format) are silently ignored. Successful action results are reflected in the next state broadcast.

---

## Examples / 예제

### curl 예제 / curl Examples

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

# Toggle input mute / 입력 뮤트 토글
curl http://127.0.0.1:8766/api/input-mute/toggle

# Toggle recording / 녹음 토글
curl http://127.0.0.1:8766/api/recording/toggle

# Toggle IPC output / IPC 출력 토글
curl http://127.0.0.1:8766/api/ipc/toggle

# Set plugin 0, parameter 3 to 0.75 / 플러그인 0 파라미터 3 설정
curl http://127.0.0.1:8766/api/plugin/0/param/3/0.75

# Set output volume to 80% / 출력 볼륨 80%
curl http://127.0.0.1:8766/api/volume/output/0.8

# List loaded plugins / 로드된 플러그인 목록
curl http://127.0.0.1:8766/api/plugins

# List parameters of plugin 0 / 플러그인 0 파라미터 목록
curl http://127.0.0.1:8766/api/plugin/0/params

# Get performance stats / 성능 통계 조회
curl http://127.0.0.1:8766/api/perf

# Add auto processors (Filter+NoiseRemoval+AutoGain) / 자동 프로세서 추가
curl http://127.0.0.1:8766/api/auto/add

# Inject MIDI CC for testing (Ch1, CC#7, Value=127) / MIDI CC 테스트 주입
curl http://127.0.0.1:8766/api/midi/cc/1/7/127

# Inject MIDI Note for testing (Ch1, Note 60, Vel=127) / MIDI Note 테스트 주입
curl http://127.0.0.1:8766/api/midi/note/1/60/127
```

### Python (WebSocket) 예제 / Python (WebSocket) Example

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

### Node.js (WebSocket) 예제 / Node.js (WebSocket) Example

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
