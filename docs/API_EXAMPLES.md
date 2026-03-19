# DirectPipe API Automation Examples / API 자동화 예제

DirectPipe의 HTTP REST API(포트 8766)와 WebSocket(포트 8765)을 활용한 자동화 예제 모음.
API 레퍼런스 전체 문서는 [CONTROL_API.md](CONTROL_API.md) 참조.

A collection of automation examples using DirectPipe's HTTP REST API (port 8766) and WebSocket (port 8765).
See [CONTROL_API.md](CONTROL_API.md) for the full API reference.

> **기본 포트**: HTTP `8766`, WebSocket `8765` (Controls > Stream Deck 탭에서 변경 가능)
> **접속 주소**: localhost만 바인딩 (`127.0.0.1`). 외부 접근 불가.
> **인증**: 없음. 로컬 전용이므로 별도 인증 없이 사용.

---

## 목차 / Table of Contents

1. [curl — 터미널에서 빠르게 제어](#1-curl--터미널에서-빠르게-제어)
2. [Python — HTTP API 기본](#2-python--http-api-기본)
3. [Python — WebSocket 실시간 모니터링](#3-python--websocket-실시간-모니터링)
4. [Python — 시간 기반 자동 프리셋 전환](#4-python--시간-기반-자동-프리셋-전환)
5. [Python — OBS 장면 연동 자동 전환](#5-python--obs-장면-연동-자동-전환)
6. [Python — Discord 봇 연동](#6-python--discord-봇-연동)
7. [AutoHotkey v2 — 커스텀 글로벌 핫키](#7-autohotkey-v2--커스텀-글로벌-핫키)
8. [PowerShell — 스크립트 자동화](#8-powershell--스크립트-자동화)
9. [JavaScript / Node.js](#9-javascript--nodejs)
10. [JavaScript — 브라우저 대시보드](#10-javascript--브라우저-대시보드)
11. [Batch / cmd — 간단한 원클릭 스크립트](#11-batch--cmd--간단한-원클릭-스크립트)
12. [활용 시나리오](#12-활용-시나리오--use-case-scenarios)
13. [HTTP API 엔드포인트 빠른 참조](#13-http-api-엔드포인트-빠른-참조--quick-reference)
14. [State JSON 필드 참조](#14-state-json-필드-참조--state-json-fields)
15. [트러블슈팅](#15-트러블슈팅--troubleshooting)

---

## 1. curl — 터미널에서 빠르게 제어

모든 HTTP API는 **GET** 요청. curl, 브라우저, 또는 HTTP 클라이언트로 바로 호출 가능.

```bash
# ─── 상태 확인 ───
# 전체 상태 JSON (플러그인, 볼륨, 슬롯, 뮤트 등)
curl http://127.0.0.1:8766/api/status

# jq로 특정 필드만 추출
curl -s http://127.0.0.1:8766/api/status | jq '.data.active_slot'
curl -s http://127.0.0.1:8766/api/status | jq '.data.plugins[] | .name'
curl -s http://127.0.0.1:8766/api/status | jq '{slot: .data.active_slot, muted: .data.muted, cpu: .data.cpu_percent}'

# ─── 프리셋 슬롯 ───
# 슬롯 전환 (0=A, 1=B, 2=C, 3=D, 4=E)
curl http://127.0.0.1:8766/api/slot/0    # 슬롯 A
curl http://127.0.0.1:8766/api/slot/4    # 슬롯 E

# ─── 플러그인 바이패스 ───
# 개별 플러그인 바이패스 토글 (0-based 인덱스)
curl http://127.0.0.1:8766/api/bypass/0/toggle   # 1번째 플러그인
curl http://127.0.0.1:8766/api/bypass/2/toggle   # 3번째 플러그인

# 마스터 바이패스 토글 (전체 체인)
curl http://127.0.0.1:8766/api/bypass/master

# ─── 뮤트 ───
# 패닉 뮤트 (전체 출력 즉시 뮤트/언뮤트, 해제 시 이전 ON/OFF 상태 자동 복원)
curl http://127.0.0.1:8766/api/mute/panic

# 메인 출력 뮤트 토글 (Discord/VB-Cable or other virtual audio device)
curl http://127.0.0.1:8766/api/mute/toggle

# 입력 뮤트 토글
curl http://127.0.0.1:8766/api/input-mute/toggle

# ─── 볼륨 ───
# 모니터 볼륨 설정 (0.0 ~ 1.0)
curl http://127.0.0.1:8766/api/volume/monitor/0.7
curl http://127.0.0.1:8766/api/volume/monitor/0.0   # 음소거
curl http://127.0.0.1:8766/api/volume/monitor/1.0   # 최대

# 입력 게인 설정 (0.0 ~ 2.0 배수)
curl http://127.0.0.1:8766/api/volume/input/1.5

# 입력 게인 조정 (선형, 예: 0.1 = +0.1 게인)
curl http://127.0.0.1:8766/api/gain/0.3     # +0.3 linear gain
curl http://127.0.0.1:8766/api/gain/-0.5    # -0.5 linear gain

# ─── 모니터 / IPC ───
# 모니터 출력 (헤드폰) 토글
curl http://127.0.0.1:8766/api/monitor/toggle

# IPC 출력 (DirectPipe Receiver) 토글
curl http://127.0.0.1:8766/api/ipc/toggle

# ─── 녹음 ───
# 녹음 시작/정지 토글
curl http://127.0.0.1:8766/api/recording/toggle

# ─── 플러그인 파라미터 ───
# 플러그인 0번의 파라미터 2번을 0.5로 설정
curl http://127.0.0.1:8766/api/plugin/0/param/2/0.5

# ─── MIDI 테스트 (하드웨어 없이 MIDI 바인딩 테스트) ───
# CC 메시지 주입 (채널 1-16, CC번호 0-127, 값 0-127)
curl http://127.0.0.1:8766/api/midi/cc/1/7/127

# Note 메시지 주입 (채널 1-16, 노트 0-127, 벨로시티 0-127)
curl http://127.0.0.1:8766/api/midi/note/1/60/100
```

---

## 2. Python — HTTP API 기본

> 필요: `pip install requests`

### 상태 확인 및 조회

```python
import requests

BASE = "http://127.0.0.1:8766/api"

# 전체 상태 가져오기
response = requests.get(f"{BASE}/status")
state = response.json()
data = state["data"]

# 기본 정보 출력
slot_index = data.get("active_slot", -1)
auto = data.get("auto_slot_active", False)
slot_names = data.get("slot_names", ["", "", "", "", ""])
if auto:
    display = "[Auto]"
elif 0 <= slot_index <= 4:
    slot_label = chr(65 + slot_index)  # 0→A, 1→B, ...
    slot_name = slot_names[slot_index]
    display = f"{slot_label}|{slot_name}" if slot_name else slot_label
else:
    display = "\u2014"

print(f"Active Slot: {display}")
print(f"Muted: {data['muted']}")
print(f"CPU: {data['cpu_percent']:.1f}%")
print(f"Sample Rate: {data['sample_rate']} Hz")
print(f"Buffer Size: {data['buffer_size']} samples")
print(f"Latency: {data['latency_ms']:.1f} ms")
print(f"Input Level: {data['level_db']:.1f} dBFS")
print(f"Monitor: {'ON' if data['monitor_enabled'] else 'OFF'}")
print(f"IPC: {'ON' if data['ipc_enabled'] else 'OFF'}")
print(f"Recording: {'REC' if data['recording'] else 'OFF'}")

# 플러그인 체인 출력
print(f"\nPlugins ({len(data['plugins'])}):")
for i, p in enumerate(data["plugins"]):
    bypass = " [BYPASS]" if p["bypass"] else ""
    loaded = "" if p["loaded"] else " (not loaded)"
    print(f"  [{i}] {p['name']}{bypass}{loaded}")
```

### 슬롯 전환 + 결과 확인

```python
import requests
import time

BASE = "http://127.0.0.1:8766/api"

def switch_slot(index):
    """슬롯 전환 후 상태 확인"""
    label = chr(65 + index)
    print(f"Switching to slot {label}...")

    result = requests.get(f"{BASE}/slot/{index}").json()
    print(f"  Result: {result}")

    # 비동기 로딩 대기 (큰 체인은 최대 500ms 소요)
    time.sleep(0.5)

    # 전환 결과 확인
    status = requests.get(f"{BASE}/status").json()["data"]
    actual = status.get("active_slot", -1)
    auto = status.get("auto_slot_active", False)
    plugins = len(status["plugins"])
    if auto:
        display = "[Auto]"
    elif 0 <= actual <= 4:
        display = chr(65 + actual)
    else:
        display = "\u2014"
    print(f"  Active: {display}, Plugins: {plugins}")
    return actual == index

# 사용 예
switch_slot(0)  # A
switch_slot(1)  # B
```

### 모든 플러그인 바이패스 일괄 해제

```python
import requests

BASE = "http://127.0.0.1:8766/api"

status = requests.get(f"{BASE}/status").json()["data"]
for i, plugin in enumerate(status["plugins"]):
    if plugin["bypass"]:
        requests.get(f"{BASE}/bypass/{i}/toggle")
        print(f"  Unbypass: [{i}] {plugin['name']}")

print("All plugins active.")
```

---

## 3. Python — WebSocket 실시간 모니터링

> 필요: `pip install websockets`

### 기본 모니터링 (상태 변경 감지)

```python
import asyncio
import websockets
import json

async def monitor():
    """DirectPipe 상태를 실시간으로 모니터링"""
    uri = "ws://127.0.0.1:8765"

    async with websockets.connect(uri) as ws:
        print("Connected to DirectPipe WebSocket")
        print("Waiting for state changes... (Ctrl+C to stop)\n")

        while True:
            raw = await ws.recv()
            msg = json.loads(raw)

            if msg["type"] != "state":
                continue

            data = msg["data"]
            slot = data.get("active_slot", -1)
            auto = data.get("auto_slot_active", False)
            slot_names = data.get("slot_names", [""] * 5)
            if auto:
                display = "[Auto]"
            elif 0 <= slot <= 4:
                label = chr(65 + slot)
                name = slot_names[slot]
                display = f"{label}|{name}" if name else label
            else:
                display = "\u2014"

            plugins = data["plugins"]
            active_plugins = [p["name"] for p in plugins if not p["bypass"]]

            print(f"Slot: {display} | "
                  f"Muted: {data['muted']} | "
                  f"Monitor: {data['monitor_enabled']} | "
                  f"IPC: {data['ipc_enabled']} | "
                  f"CPU: {data['cpu_percent']:.1f}% | "
                  f"Level: {data['level_db']:.0f}dB | "
                  f"Plugins: {', '.join(active_plugins) or '(none)'}")

asyncio.run(monitor())
```

### WebSocket으로 액션 전송

```python
import asyncio
import websockets
import json

async def send_action(action, params=None):
    """WebSocket으로 액션 전송 후 상태 응답 수신"""
    uri = "ws://127.0.0.1:8765"

    async with websockets.connect(uri) as ws:
        # 초기 상태 수신 (연결 직후 서버가 전송)
        initial = json.loads(await ws.recv())
        print(f"Initial slot: {initial['data']['active_slot']}")

        # 액션 전송
        msg = {"type": "action", "action": action}
        if params:
            msg["params"] = params
        await ws.send(json.dumps(msg))
        print(f"Sent: {action} {params or ''}")

        # 상태 변경 응답 대기
        response = json.loads(await ws.recv())
        print(f"New slot: {response['data']['active_slot']}")

# 사용 예
asyncio.run(send_action("switch_preset_slot", {"slot": 2}))     # 슬롯 C
asyncio.run(send_action("panic_mute"))                           # 패닉 뮤트
asyncio.run(send_action("set_volume", {"target": "monitor", "value": 0.8}))
asyncio.run(send_action("plugin_bypass", {"index": 0}))
asyncio.run(send_action("recording_toggle"))
```

### 장치 분실 감지 + 알림

```python
import asyncio
import websockets
import json

async def device_watchdog():
    """오디오 장치 분실 시 경고"""
    uri = "ws://127.0.0.1:8765"
    prev_device_lost = False
    prev_monitor_lost = False

    async with websockets.connect(uri) as ws:
        print("Device watchdog started")

        while True:
            msg = json.loads(await ws.recv())
            if msg["type"] != "state":
                continue

            data = msg["data"]

            # 메인 장치 분실 감지
            if data["device_lost"] and not prev_device_lost:
                print("⚠️  AUDIO DEVICE LOST! Check USB connection.")
                # 여기에 알림 로직 추가 (이메일, Slack, 데스크톱 알림 등)
            elif not data["device_lost"] and prev_device_lost:
                print("✅ Audio device reconnected.")

            # 모니터 장치 분실 감지
            if data["monitor_lost"] and not prev_monitor_lost:
                print("⚠️  MONITOR DEVICE LOST!")
            elif not data["monitor_lost"] and prev_monitor_lost:
                print("✅ Monitor device reconnected.")

            prev_device_lost = data["device_lost"]
            prev_monitor_lost = data["monitor_lost"]

asyncio.run(device_watchdog())
```

---

## 4. Python — 시간 기반 자동 프리셋 전환

> 필요: `pip install requests schedule`

```python
"""
시간대별 자동 프리셋 전환 스크립트.
방송 스케줄에 맞춰 자동으로 마이크 세팅을 변경.
"""
import requests
import schedule
import time

BASE = "http://127.0.0.1:8766/api"

# 슬롯 매핑 (DirectPipe에서 슬롯 이름을 미리 설정)
SLOTS = {
    "gaming":    0,  # A|게임 — 노이즈 게이트 강하게
    "talk":      1,  # B|토크 — EQ + 컴프레서
    "singing":   2,  # C|노래 — 리버브 추가
    "recording": 3,  # D|녹음 — 고품질 체인
    "quiet":     4,  # E|조용 — 최소 처리
}

def switch(preset_name):
    slot = SLOTS[preset_name]
    result = requests.get(f"{BASE}/slot/{slot}")
    print(f"[{time.strftime('%H:%M')}] Switched to {preset_name} (slot {chr(65 + slot)})")

def start_recording():
    status = requests.get(f"{BASE}/status").json()["data"]
    if not status["recording"]:
        requests.get(f"{BASE}/recording/toggle")
        print(f"[{time.strftime('%H:%M')}] Recording started")

def stop_recording():
    status = requests.get(f"{BASE}/status").json()["data"]
    if status["recording"]:
        requests.get(f"{BASE}/recording/toggle")
        print(f"[{time.strftime('%H:%M')}] Recording stopped")

# ─── 스케줄 설정 ───
# 월~금: 오후 2시에 토크 세팅, 저녁 8시에 게임 세팅
schedule.every().monday.at("14:00").do(switch, "talk")
schedule.every().monday.at("20:00").do(switch, "gaming")
schedule.every().tuesday.at("14:00").do(switch, "talk")
schedule.every().tuesday.at("20:00").do(switch, "gaming")
# ... (다른 요일도 동일)

# 토요일: 노래방 모드
schedule.every().saturday.at("21:00").do(switch, "singing")

# 매일 자정: 조용 모드 + 녹음 정지
schedule.every().day.at("00:00").do(switch, "quiet")
schedule.every().day.at("00:00").do(stop_recording)

print("DirectPipe scheduler running. Press Ctrl+C to stop.")
print(f"Pending jobs: {len(schedule.get_jobs())}")

while True:
    schedule.run_pending()
    time.sleep(1)
```

---

## 5. Python — OBS 장면 연동 자동 전환

> 필요: `pip install obsws-python requests`
>
> OBS에서 **도구 > obs-websocket 설정**에서 WebSocket 서버 활성화 필요.

```python
"""
OBS 장면(Scene) 전환 시 DirectPipe 프리셋을 자동으로 변경.
예: "Gaming" 장면 → 슬롯 A, "Just Chat" 장면 → 슬롯 B
"""
import obsws_python as obs
import requests
import time
import sys

DP_BASE = "http://127.0.0.1:8766/api"

# ─── 매핑 설정 ───
# OBS 장면 이름 → DirectPipe 슬롯 인덱스
# (DirectPipe에서 각 슬롯에 적절한 VST 체인을 미리 설정)
SCENE_TO_SLOT = {
    "Gaming":       0,  # A — 노이즈 게이트 + 컴프레서
    "Just Chatting": 1,  # B — EQ + 디에서 + 컴프레서
    "BRB":          None,  # 전환 안 함 (현재 유지)
    "Starting":     None,
    "Ending":       4,  # E — 페이드아웃용 조용한 세팅
}

# OBS 장면 → 추가 동작
SCENE_ACTIONS = {
    "Gaming":       {"monitor": True,  "ipc": True},
    "Just Chatting": {"monitor": True,  "ipc": True},
    "BRB":          {"monitor": False, "ipc": True},  # 모니터 끄고 IPC만 유지
    "Ending":       {"monitor": False, "ipc": False},
}

def apply_slot(scene_name):
    """장면에 매핑된 슬롯으로 전환"""
    slot = SCENE_TO_SLOT.get(scene_name)
    if slot is not None:
        requests.get(f"{DP_BASE}/slot/{slot}")
        print(f"  → Slot {chr(65 + slot)}")

def apply_actions(scene_name):
    """장면에 매핑된 추가 동작 실행"""
    actions = SCENE_ACTIONS.get(scene_name, {})

    if "monitor" in actions:
        status = requests.get(f"{DP_BASE}/status").json()["data"]
        current = status["monitor_enabled"]
        if current != actions["monitor"]:
            requests.get(f"{DP_BASE}/monitor/toggle")
            print(f"  → Monitor: {'ON' if actions['monitor'] else 'OFF'}")

    if "ipc" in actions:
        status = requests.get(f"{DP_BASE}/status").json()["data"]
        current = status["ipc_enabled"]
        if current != actions["ipc"]:
            requests.get(f"{DP_BASE}/ipc/toggle")
            print(f"  → IPC: {'ON' if actions['ipc'] else 'OFF'}")

def on_current_program_scene_changed(data):
    """OBS 장면 전환 이벤트 핸들러"""
    scene = data.scene_name
    print(f"\n[OBS] Scene changed: {scene}")
    apply_slot(scene)
    apply_actions(scene)

# ─── 메인 ───
print("Connecting to OBS WebSocket...")
try:
    cl = obs.EventClient()
    cl.callback.register(on_current_program_scene_changed)
    print("Connected! Listening for scene changes...")
    print(f"Mapped scenes: {list(SCENE_TO_SLOT.keys())}")
    print("Press Ctrl+C to stop.\n")

    while True:
        time.sleep(1)

except ConnectionRefusedError:
    print("ERROR: Cannot connect to OBS WebSocket.")
    print("  1. OBS를 실행하세요")
    print("  2. 도구 > obs-websocket 설정 > WebSocket 서버 활성화")
    sys.exit(1)
except KeyboardInterrupt:
    print("\nStopped.")
```

---

## 6. Python — Discord 봇 연동

> 필요: `pip install discord.py requests`
>
> Discord Developer Portal에서 봇 토큰 필요.

```python
"""
Discord 봇으로 DirectPipe를 원격 제어.
채팅 명령어로 프리셋 전환, 뮤트, 상태 확인 가능.
"""
import discord
import requests

DP_BASE = "http://127.0.0.1:8766/api"
BOT_TOKEN = "YOUR_BOT_TOKEN_HERE"  # Discord Developer Portal에서 발급

intents = discord.Intents.default()
intents.message_content = True
client = discord.Client(intents=intents)

@client.event
async def on_ready():
    print(f"Bot ready: {client.user}")

@client.event
async def on_message(message):
    if message.author.bot:
        return

    content = message.content.lower()

    # !dp status — 현재 상태
    if content == "!dp status":
        try:
            data = requests.get(f"{DP_BASE}/status", timeout=2).json()["data"]
            slot = data.get("active_slot", -1)
            auto = data.get("auto_slot_active", False)
            slot_names = data.get("slot_names", [""] * 5)
            if auto:
                display = "[Auto]"
            elif 0 <= slot <= 4:
                label = chr(65 + slot)
                name = slot_names[slot]
                display = f"{label}|{name}" if name else label
            else:
                display = "\u2014"

            plugins = [f"{'~~' if p['bypass'] else ''}{p['name']}{'~~' if p['bypass'] else ''}"
                      for p in data["plugins"]]

            embed = discord.Embed(title="DirectPipe Status", color=0x7B6FFF)
            embed.add_field(name="Slot", value=display, inline=True)
            embed.add_field(name="Muted", value="🔇 Yes" if data["muted"] else "🔊 No", inline=True)
            embed.add_field(name="CPU", value=f"{data['cpu_percent']:.1f}%", inline=True)
            embed.add_field(name="Plugins", value="\n".join(plugins) or "(empty)", inline=False)
            await message.channel.send(embed=embed)
        except requests.ConnectionError:
            await message.channel.send("❌ DirectPipe is not running.")

    # !dp slot A/B/C/D/E — 슬롯 전환
    elif content.startswith("!dp slot "):
        slot_char = content.split()[-1].upper()
        if slot_char in "ABCDE":
            slot_index = ord(slot_char) - 65
            requests.get(f"{DP_BASE}/slot/{slot_index}")
            await message.channel.send(f"✅ Switched to slot {slot_char}")
        else:
            await message.channel.send("❌ Valid slots: A, B, C, D, E")

    # !dp mute — 패닉 뮤트
    elif content == "!dp mute":
        requests.get(f"{DP_BASE}/mute/panic")
        await message.channel.send("🔇 Panic mute toggled")

    # !dp rec — 녹음 토글
    elif content == "!dp rec":
        requests.get(f"{DP_BASE}/recording/toggle")
        data = requests.get(f"{DP_BASE}/status", timeout=2).json()["data"]
        status = "🔴 Recording" if data["recording"] else "⏹ Stopped"
        await message.channel.send(status)

    # !dp help — 도움말
    elif content == "!dp help":
        help_text = (
            "**DirectPipe Bot Commands**\n"
            "`!dp status` — Current state\n"
            "`!dp slot A/B/C/D/E` — Switch preset\n"
            "`!dp mute` — Panic mute toggle\n"
            "`!dp rec` — Recording toggle\n"
        )
        await message.channel.send(help_text)

client.run(BOT_TOKEN)
```

---

## 7. AutoHotkey v2 — 커스텀 글로벌 핫키

DirectPipe에 내장된 핫키(Ctrl+Shift+1~9, F1~F5 등)로 부족할 때,
AutoHotkey로 더 복잡한 조합이나 매크로를 만들 수 있다.

> [AutoHotkey v2 다운로드](https://www.autohotkey.com/)

### 기본: HTTP API 호출

```ahk
; DirectPipe-Control.ahk (AutoHotkey v2)
; 저장 후 더블클릭으로 실행. 시스템 트레이에 상주.

#Requires AutoHotkey v2.0

SendDP(endpoint) {
    try {
        whr := ComObject("WinHttp.WinHttpRequest.5.1")
        whr.Open("GET", "http://127.0.0.1:8766/api/" endpoint, true)
        whr.Send()
        whr.WaitForResponse(2000)  ; 2초 타임아웃
    }
}

; ─── 프리셋 슬롯 ───
; Win+F1~F5 = 슬롯 A~E
#F1::SendDP("slot/0")
#F2::SendDP("slot/1")
#F3::SendDP("slot/2")
#F4::SendDP("slot/3")
#F5::SendDP("slot/4")

; ─── 뮤트 ───
; Win+M = 패닉 뮤트
#m::SendDP("mute/panic")

; Win+N = 입력 뮤트
#n::SendDP("input-mute/toggle")

; ─── 볼륨 ───
; Win+PageUp/PageDown = 모니터 볼륨 증감
#PgUp::SendDP("volume/monitor/1.0")
#PgDn::SendDP("volume/monitor/0.5")

; ─── 녹음 ───
; Win+R = 녹음 토글
#r::SendDP("recording/toggle")

; ─── IPC ───
; Win+I = IPC 토글
#i::SendDP("ipc/toggle")
```

### 고급: 상태 조회 후 조건부 동작

```ahk
; 현재 상태를 확인하고 조건부로 동작
#Requires AutoHotkey v2.0

GetDPStatus() {
    try {
        whr := ComObject("WinHttp.WinHttpRequest.5.1")
        whr.Open("GET", "http://127.0.0.1:8766/api/status", true)
        whr.Send()
        whr.WaitForResponse(2000)
        return whr.ResponseText
    }
    return ""
}

SendDP(endpoint) {
    try {
        whr := ComObject("WinHttp.WinHttpRequest.5.1")
        whr.Open("GET", "http://127.0.0.1:8766/api/" endpoint, true)
        whr.Send()
        whr.WaitForResponse(2000)
    }
}

; Win+G = "게임 모드" 매크로
; 슬롯 A + 모니터 ON + IPC ON
#g::{
    SendDP("slot/0")
    Sleep(300)  ; 슬롯 전환 대기

    ; 상태 확인 후 필요한 것만 토글
    status := GetDPStatus()
    if InStr(status, '"monitor_enabled":false')
        SendDP("monitor/toggle")
    if InStr(status, '"ipc_enabled":false')
        SendDP("ipc/toggle")

    ToolTip("DirectPipe: Game Mode")
    SetTimer(() => ToolTip(), -2000)  ; 2초 후 툴팁 제거
}

; Win+T = "토크 모드" 매크로
#t::{
    SendDP("slot/1")
    Sleep(300)

    status := GetDPStatus()
    if InStr(status, '"monitor_enabled":false')
        SendDP("monitor/toggle")

    ToolTip("DirectPipe: Talk Mode")
    SetTimer(() => ToolTip(), -2000)
}
```

---

## 8. PowerShell — 스크립트 자동화

### 기본 명령

```powershell
# 상태 확인
$status = Invoke-RestMethod http://127.0.0.1:8766/api/status
$d = $status.data

Write-Host "Slot: $([char](65 + $d.active_slot))"
Write-Host "Muted: $($d.muted)"
Write-Host "CPU: $($d.cpu_percent)%"
Write-Host "Plugins: $($d.plugins.Count)"

# 플러그인 목록
$d.plugins | ForEach-Object {
    $bypass = if ($_.bypass) { "[BYPASS]" } else { "" }
    Write-Host "  $($_.name) $bypass"
}

# 슬롯 전환
Invoke-RestMethod http://127.0.0.1:8766/api/slot/0

# 패닉 뮤트
Invoke-RestMethod http://127.0.0.1:8766/api/mute/panic

# 모니터 볼륨 설정
Invoke-RestMethod http://127.0.0.1:8766/api/volume/monitor/0.7
```

### 모든 플러그인 바이패스 일괄 조작

```powershell
# 모든 플러그인 바이패스 → 전부 해제
$status = Invoke-RestMethod http://127.0.0.1:8766/api/status
for ($i = 0; $i -lt $status.data.plugins.Count; $i++) {
    if ($status.data.plugins[$i].bypass) {
        Invoke-RestMethod "http://127.0.0.1:8766/api/bypass/$i/toggle"
        Write-Host "Unbypass: [$i] $($status.data.plugins[$i].name)"
    }
}
```

### DirectPipe 상태 모니터링 (폴링)

```powershell
# 5초마다 상태를 체크하여 콘솔에 표시
while ($true) {
    try {
        $d = (Invoke-RestMethod http://127.0.0.1:8766/api/status -TimeoutSec 2).data
        $slot = [char](65 + $d.active_slot)
        $mute = if ($d.muted) { "MUTED" } else { "OK" }
        $rec = if ($d.recording) { "REC $([math]::Round($d.recording_seconds))s" } else { "" }
        Write-Host "$(Get-Date -f 'HH:mm:ss') | Slot:$slot | $mute | CPU:$([math]::Round($d.cpu_percent,1))% | Level:$([math]::Round($d.level_db))dB $rec"
    }
    catch {
        Write-Host "$(Get-Date -f 'HH:mm:ss') | DirectPipe not responding"
    }
    Start-Sleep -Seconds 5
}
```

---

## 9. JavaScript / Node.js

### HTTP (Node.js 18+ built-in fetch)

```javascript
const BASE = "http://127.0.0.1:8766/api";

// 상태 확인
const res = await fetch(`${BASE}/status`);
const { data } = await res.json();
const slot = data.active_slot;
const slotNames = data.slot_names || [];
const label = String.fromCharCode(65 + slot);
const name = slotNames[slot];
console.log(`Slot: ${name ? `${label}|${name}` : label}`);
console.log(`Muted: ${data.muted}`);
console.log(`Plugins: ${data.plugins.map(p => p.name).join(", ")}`);

// 슬롯 전환
await fetch(`${BASE}/slot/0`);

// 패닉 뮤트
await fetch(`${BASE}/mute/panic`);

// 볼륨 설정
await fetch(`${BASE}/volume/monitor/0.8`);
```

### WebSocket (실시간 모니터링)

```javascript
// Node.js: npm install ws
// 브라우저: WebSocket은 네이티브 지원
const WebSocket = require("ws");

const ws = new WebSocket("ws://127.0.0.1:8765");

ws.on("open", () => {
    console.log("Connected to DirectPipe");
});

ws.on("message", (raw) => {
    const msg = JSON.parse(raw);
    if (msg.type !== "state") return;

    const d = msg.data;
    const slot = d.active_slot;
    const names = d.slot_names || [];
    const label = String.fromCharCode(65 + slot);
    const display = names[slot] ? `${label}|${names[slot]}` : label;

    const activePlugins = d.plugins
        .filter(p => !p.bypass && p.loaded)
        .map(p => p.name);

    console.log(
        `Slot: ${display} | ` +
        `Muted: ${d.muted} | ` +
        `CPU: ${d.cpu_percent.toFixed(1)}% | ` +
        `Plugins: ${activePlugins.join(", ") || "(none)"}`
    );
});

// 액션 전송
ws.on("open", () => {
    // 슬롯 B로 전환
    ws.send(JSON.stringify({
        type: "action",
        action: "switch_preset_slot",
        params: { slot: 1 }
    }));

    // 모니터 볼륨 80%
    ws.send(JSON.stringify({
        type: "action",
        action: "set_volume",
        params: { target: "monitor", value: 0.8 }
    }));
});

ws.on("close", () => console.log("Disconnected"));
ws.on("error", (err) => console.error("Error:", err.message));
```

---

## 10. JavaScript — 브라우저 대시보드

브라우저에서 열어 DirectPipe 상태를 실시간으로 모니터링하는 간단한 대시보드.
아래 HTML을 파일로 저장하고 브라우저에서 열면 동작한다.

```html
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>DirectPipe Dashboard</title>
    <style>
        body { font-family: 'Segoe UI', sans-serif; background: #1a1a2e; color: #e0e0e0; padding: 20px; }
        h1 { color: #7B6FFF; }
        .card { background: #2a2a40; border-radius: 8px; padding: 16px; margin: 8px 0; }
        .slot { font-size: 24px; font-weight: bold; color: #7B6FFF; }
        .muted { color: #E05050; }
        .ok { color: #4CAF50; }
        .plugin { padding: 4px 8px; margin: 2px 0; background: #3a3a50; border-radius: 4px; display: inline-block; }
        .bypass { opacity: 0.5; text-decoration: line-through; }
        .meter { height: 8px; background: #3a3a50; border-radius: 4px; overflow: hidden; }
        .meter-fill { height: 100%; background: #4CAF50; transition: width 0.3s; }
        button { background: #7B6FFF; color: white; border: none; padding: 8px 16px; border-radius: 4px; cursor: pointer; margin: 4px; }
        button:hover { background: #6B5FEF; }
        .slot-btn { width: 50px; }
        .slot-btn.active { background: #4CAF50; }
        #status { font-size: 12px; color: #888; }
    </style>
</head>
<body>
    <h1>DirectPipe Dashboard</h1>
    <div id="status">Connecting...</div>

    <div class="card">
        <div>Slot: <span id="slot" class="slot">-</span></div>
        <div style="margin-top:8px">
            <button class="slot-btn" onclick="switchSlot(0)">A</button>
            <button class="slot-btn" onclick="switchSlot(1)">B</button>
            <button class="slot-btn" onclick="switchSlot(2)">C</button>
            <button class="slot-btn" onclick="switchSlot(3)">D</button>
            <button class="slot-btn" onclick="switchSlot(4)">E</button>
            <button onclick="sendAction('panic_mute')" style="background:#E05050;margin-left:20px">PANIC</button>
        </div>
    </div>

    <div class="card">
        <div>Muted: <span id="muted">-</span> | Monitor: <span id="monitor">-</span> | IPC: <span id="ipc">-</span></div>
        <div>CPU: <span id="cpu">-</span>% | Latency: <span id="latency">-</span>ms | Level: <span id="level">-</span>dB</div>
        <div>SR: <span id="sr">-</span> Hz | Buffer: <span id="bs">-</span></div>
        <div class="meter" style="margin-top:8px"><div class="meter-fill" id="level-bar" style="width:0%"></div></div>
    </div>

    <div class="card">
        <div>Plugins:</div>
        <div id="plugins"></div>
    </div>

<script>
const API = "http://127.0.0.1:8766/api";
let ws;

function connect() {
    ws = new WebSocket("ws://127.0.0.1:8765");
    ws.onopen = () => { document.getElementById("status").textContent = "Connected"; };
    ws.onclose = () => {
        document.getElementById("status").textContent = "Disconnected — reconnecting...";
        setTimeout(connect, 3000);
    };
    ws.onerror = () => {};
    ws.onmessage = (e) => {
        const msg = JSON.parse(e.data);
        if (msg.type === "state") updateUI(msg.data);
    };
}

function updateUI(d) {
    const slot = d.active_slot;
    const names = d.slot_names || [];
    const label = String.fromCharCode(65 + slot);
    document.getElementById("slot").textContent = names[slot] ? `${label}|${names[slot]}` : label;

    const mutedEl = document.getElementById("muted");
    mutedEl.textContent = d.muted ? "🔇 YES" : "🔊 NO";
    mutedEl.className = d.muted ? "muted" : "ok";

    document.getElementById("monitor").textContent = d.monitor_enabled ? "ON" : "OFF";
    document.getElementById("ipc").textContent = d.ipc_enabled ? "ON" : "OFF";
    document.getElementById("cpu").textContent = d.cpu_percent.toFixed(1);
    document.getElementById("latency").textContent = d.latency_ms.toFixed(1);
    document.getElementById("level").textContent = d.level_db.toFixed(0);
    document.getElementById("sr").textContent = d.sample_rate;
    document.getElementById("bs").textContent = d.buffer_size;

    // Level meter (dBFS -60..0 → 0..100%)
    const pct = Math.max(0, Math.min(100, ((d.level_db + 60) / 60) * 100));
    document.getElementById("level-bar").style.width = pct + "%";

    // Plugins
    const html = d.plugins.map((p, i) =>
        `<span class="plugin ${p.bypass ? 'bypass' : ''}" onclick="toggleBypass(${i})">${p.name}</span>`
    ).join(" ");
    document.getElementById("plugins").innerHTML = html || "(empty chain)";

    // Slot buttons
    document.querySelectorAll(".slot-btn").forEach((btn, i) => {
        btn.className = `slot-btn${i === slot ? ' active' : ''}`;
    });
}

function switchSlot(i) { fetch(`${API}/slot/${i}`); }
function toggleBypass(i) { fetch(`${API}/bypass/${i}/toggle`); }
function sendAction(action) {
    if (ws && ws.readyState === 1) {
        ws.send(JSON.stringify({ type: "action", action, params: {} }));
    }
}

connect();
</script>
</body>
</html>
```

---

## 11. Batch / cmd — 간단한 원클릭 스크립트

`.bat` 파일로 저장 후 더블클릭으로 실행. Windows에 별도 설치 없이 사용 가능.

> **macOS/Linux**: Use `.sh` shell scripts instead of `.bat`. Replace `>nul` with `>/dev/null 2>&1`, `timeout /t N` with `sleep N`, and `@echo off` with `#!/bin/bash`.
>
> **macOS/Linux**: `.bat` 대신 `.sh` 셸 스크립트 사용. `>nul` → `>/dev/null 2>&1`, `timeout /t N` → `sleep N`, `@echo off` → `#!/bin/bash`로 대체.

```batch
@echo off
:: game-mode.bat — 게임 모드로 전환
:: 슬롯 A + IPC ON

curl -s http://127.0.0.1:8766/api/slot/0 >nul
timeout /t 1 /nobreak >nul
curl -s http://127.0.0.1:8766/api/ipc/toggle >nul
echo Game mode activated!
timeout /t 2
```

```batch
@echo off
:: panic.bat — 패닉 뮤트 (데스크톱에 바로가기 만들면 긴급 뮤트 버튼)
curl -s http://127.0.0.1:8766/api/mute/panic >nul
echo MUTED!
```

```batch
@echo off
:: status.bat — 현재 상태 확인
curl -s http://127.0.0.1:8766/api/status | findstr active_slot muted cpu_percent
pause
```

---

## 12. 활용 시나리오 / Use Case Scenarios

### 시나리오 1: 방송 자동화 (OBS + DirectPipe)

```
방송 시작:
  OBS "Starting" 장면 → DirectPipe 슬롯 E (조용한 세팅)
  OBS "Gaming" 장면 → DirectPipe 슬롯 A (게임용 세팅) + IPC ON
  OBS "Just Chat" 장면 → DirectPipe 슬롯 B (토크 세팅)
  OBS "Ending" 장면 → DirectPipe 패닉 뮤트

→ [예제 5: OBS 장면 연동](#5-python--obs-장면-연동-자동-전환) 참조
```

> **Receiver VST 참고**: Receiver VST는 입력 버스가 없는 출력 전용 플러그인으로, OBS 오디오 소스의 마이크 입력은 무시하고 DirectPipe에서 IPC로 전송된 오디오만 출력합니다.
>
> **Receiver VST note**: Receiver VST is an output-only plugin (no input bus) — it ignores OBS source audio, only outputs what DirectPipe sends via IPC.

### 시나리오 1.5: 출력별 개별 뮤트 (Discord + OBS 독립 제어)

VB-Cable(Discord) + Receiver VST(OBS) 동시 사용 시, API로 각 출력을 독립적으로 뮤트/언뮤트할 수 있습니다. (macOS/Linux에서는 VB-Cable 대신 BlackHole, PipeWire 등 해당 플랫폼의 가상 오디오 장치 사용.)

When using VB-Cable (Discord) + Receiver VST (OBS) together, you can independently mute/unmute each output via API. (On macOS/Linux, use platform-equivalent virtual audio devices such as BlackHole or PipeWire instead of VB-Cable.)

```bash
# OBS만 뮤트 (Discord 유지) — IPC 출력만 끔
curl http://127.0.0.1:8766/api/ipc/toggle

# Discord만 뮤트 (OBS 유지) — 메인 출력만 끔
curl http://127.0.0.1:8766/api/mute/toggle

# 모니터(헤드폰)만 끔
curl http://127.0.0.1:8766/api/monitor/toggle

# 전체 긴급 차단 (Panic Mute)
curl http://127.0.0.1:8766/api/mute/panic

# 현재 상태 확인 (어떤 출력이 켜져 있는지)
curl -s http://127.0.0.1:8766/api/status | jq '{output_muted: .data.output_muted, ipc_enabled: .data.ipc_enabled, monitor_enabled: .data.monitor_enabled, panic: .data.muted}'
```

```python
# Python으로 상태 확인 후 조건부 뮤트
import requests

BASE = "http://127.0.0.1:8766/api"
data = requests.get(f"{BASE}/status").json()["data"]

# OBS 마이크가 켜져 있으면 끄기
if data["ipc_enabled"]:
    requests.get(f"{BASE}/ipc/toggle")
    print("OBS mic muted (Discord still active)")

# Discord 마이크가 켜져 있으면 끄기
if not data["output_muted"]:
    requests.get(f"{BASE}/mute/toggle")
    print("Discord mic muted (OBS still active)")
```

### 시나리오 2: 스트림 데크 없이 외부 제어

```
AutoHotkey로 커스텀 핫키:
  Win+F1~F5 → 프리셋 A~E
  Win+M → 패닉 뮤트
  Win+G → "게임 모드" 매크로 (슬롯 A + 모니터 ON + IPC ON)

→ [예제 7: AutoHotkey](#7-autohotkey-v2--커스텀-글로벌-핫키) 참조
```

### 시나리오 3: 세팅 도우미가 원격으로 제어

```
세팅 도우미 PC에서 Python 스크립트:
  1. DirectPipe 상태 확인 (어떤 플러그인이 로드되어 있는지)
  2. 슬롯 전환 명령 (검증된 프리셋으로)
  3. 플러그인 파라미터 미세 조정

→ [예제 2: Python HTTP API](#2-python--http-api-기본) 참조
```

### 시나리오 4: 대시보드로 모니터링

```
방송 중 두 번째 모니터에 브라우저 대시보드:
  실시간 레벨 미터, CPU, 현재 슬롯, 플러그인 상태
  버튼 클릭으로 슬롯 전환, 바이패스 토글

→ [예제 10: 브라우저 대시보드](#10-javascript--브라우저-대시보드) 참조
```

---

## 13. HTTP API 엔드포인트 빠른 참조 / Quick Reference

| Endpoint | Description |
|----------|-------------|
| `GET /api/status` | 전체 상태 JSON / Full state JSON |
| `GET /api/slot/:index` | 슬롯 전환 (0-5, A-E + Auto) / Switch preset slot |
| `GET /api/bypass/:index/toggle` | 플러그인 바이패스 토글 / Toggle plugin bypass |
| `GET /api/bypass/master` | 마스터 바이패스 / Master bypass toggle |
| `GET /api/mute/panic` | 패닉 뮤트 / Panic mute toggle |
| `GET /api/mute/toggle` | 출력 뮤트 토글 / Output mute toggle |
| `GET /api/input-mute/toggle` | 입력 뮤트 토글 / Input mute toggle |
| `GET /api/volume/:target/:value` | 볼륨 설정 (monitor/output 0-1, input 0-2) / Set volume |
| `GET /api/gain/:delta` | 입력 게인 조정 (선형, 예: 0.1 = +0.1) / Adjust input gain (linear) |
| `GET /api/preset/:index` | 프리셋 로드 (0-5, A-E + Auto) / Load preset |
| `GET /api/monitor/toggle` | 모니터 출력 토글 / Monitor toggle |
| `GET /api/ipc/toggle` | IPC 출력 토글 / IPC toggle |
| `GET /api/recording/toggle` | 녹음 토글 / Recording toggle |
| `GET /api/plugin/:p/param/:i/:v` | 플러그인 파라미터 설정 / Set plugin parameter |
| `GET /api/midi/cc/:ch/:num/:val` | MIDI CC 테스트 / Test MIDI CC |
| `GET /api/midi/note/:ch/:num/:vel` | MIDI Note 테스트 / Test MIDI Note |
| `GET /api/plugins` | 로드된 플러그인 목록 / List loaded plugins |
| `GET /api/plugin/:idx/params` | 플러그인 파라미터 목록 / List plugin parameters |
| `GET /api/xrun/reset` | XRun 카운터 리셋 / Reset XRun counter |
| `GET /api/perf` | 성능 통계 / Performance stats |
| `GET /api/limiter/toggle` | Safety Limiter 토글 / Toggle safety limiter |
| `GET /api/limiter/ceiling/:value` | 리미터 실링 설정 (-6.0~0.0) / Set limiter ceiling |
| `GET /api/auto/add` | 내장 프로세서 추가 / Add auto processors |

---

## 14. State JSON 필드 참조 / State JSON Fields

`GET /api/status` 또는 WebSocket state 메시지의 `data` 객체:

| Field | Type | Description |
|-------|------|-------------|
| `plugins` | array | 플러그인 목록 `[{name, bypass, loaded, latency_samples, type}]` |
| `volumes.input` | number | 입력 게인 배수 (0.0-2.0) |
| `volumes.monitor` | number | 모니터 볼륨 (0.0-1.0) |
| `volumes.output` | number | 출력 볼륨 (0.0-1.0) |
| `master_bypassed` | bool | 전체 체인 바이패스 여부 |
| `muted` | bool | 패닉 뮤트 활성 여부 |
| `output_muted` | bool | 메인 출력 뮤트 여부 |
| `input_muted` | bool | 입력 뮤트 여부 (`muted`와 동일 — 독립 입력 뮤트 없음) |
| `active_slot` | number | 활성 프리셋 슬롯 (0-4 = A-E, -1 = 없음, 클램프) |
| `auto_slot_active` | bool | Auto 프리셋 슬롯 선택 여부 |
| `slot_names` | array | 슬롯 이름 배열 (6개: A-E + Auto, 빈 문자열 = 이름 없음) |
| `preset` | string | 현재 프리셋 이름 |
| `latency_ms` | number | 메인 레이턴시 (ms) |
| `monitor_latency_ms` | number | 모니터 레이턴시 (ms) |
| `level_db` | number | 입력 레벨 (dBFS) |
| `cpu_percent` | number | 오디오 CPU 사용률 (%) |
| `sample_rate` | number | 샘플레이트 (Hz) |
| `buffer_size` | number | 버퍼 크기 (samples) |
| `xrun_count` | number | 60초 롤링 윈도우 XRun 카운트 |
| `channel_mode` | number | 1=Mono, 2=Stereo |
| `monitor_enabled` | bool | 모니터 출력 활성 여부 |
| `recording` | bool | 녹음 중 여부 |
| `recording_seconds` | number | 녹음 경과 시간 (초) |
| `ipc_enabled` | bool | IPC (DirectPipe Receiver) 활성 여부 |
| `safety_limiter` | object | Safety Limiter 상태 `{enabled, ceiling_dB, gain_reduction_dB, is_limiting}` |
| `chain_pdc_samples` | number | 플러그인 체인 총 PDC (샘플) |
| `chain_pdc_ms` | number | 플러그인 체인 총 PDC (ms) |
| `device_lost` | bool | 메인 오디오 장치 분실 여부 |
| `monitor_lost` | bool | 모니터 장치 분실 여부 |

---

## 15. 트러블슈팅 / Troubleshooting

### DirectPipe에 연결이 안 돼요

```
1. DirectPipe가 실행 중인지 확인 (시스템 트레이 아이콘)
2. 포트 확인: Controls > Stream Deck 탭에서 WebSocket/HTTP 포트 확인
3. 방화벽: localhost 연결이므로 보통 문제 없음.
   만약 차단된다면 OS 보안 소프트웨어(Windows Defender, macOS Firewall 등)에서 DirectPipe 허용
4. 테스트: curl http://127.0.0.1:8766/api/status
```

### WebSocket이 자꾸 끊겨요

```
- DirectPipe는 3초 이상 응답 없는 WebSocket 클라이언트를 정리함
- 클라이언트에서 정기적으로 메시지를 보내거나 상태 응답을 처리해야 함
- 재연결 로직 구현 권장 (예제 10 브라우저 대시보드 참조)
```

### HTTP 요청이 느려요

```
- HTTP 서버는 3초 읽기 타임아웃. 보통 <10ms 응답
- 여러 명령을 빠르게 보낼 때는 WebSocket 사용 권장
- GET 응답: { "ok": true } 또는 { "error": "..." }
```

### MIDI 테스트 API가 동작 안 해요

```
- MIDI 바인딩이 먼저 설정되어 있어야 함 (Controls > MIDI 탭)
- 테스트 API: curl http://127.0.0.1:8766/api/midi/cc/1/7/127
- 채널 범위: 1-16, CC/Note 범위: 0-127, 값/벨로시티: 0-127
```
