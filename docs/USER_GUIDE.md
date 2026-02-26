# DirectPipe User Guide / 사용자 가이드

## What is DirectPipe? / DirectPipe란?

DirectPipe is a real-time VST2/VST3 host for Windows. It processes your microphone input through a chain of VST plugins. The main output goes to the AudioSettings Output device (e.g., VB-Audio virtual cable for OBS/Discord). An optional separate WASAPI monitor output sends to headphones. You can control it remotely via hotkeys, MIDI, Stream Deck, or HTTP API while the app runs in the system tray.

DirectPipe는 Windows용 실시간 VST2/VST3 호스트다. 마이크 입력을 VST 플러그인 체인으로 처리. 메인 출력은 AudioSettings Output 장치로 직접 전송 (예: VB-Audio 가상 케이블 → OBS, Discord). 별도 WASAPI 모니터 출력(헤드폰) 선택적 사용 가능. 시스템 트레이에서 실행하면서 단축키, MIDI, Stream Deck, HTTP API로 원격 제어 가능.

## Quick Start / 빠른 시작

1. **Launch DirectPipe** / DirectPipe 실행
2. **Select driver type** — ASIO or Windows Audio (WASAPI) in Audio tab / Audio 탭에서 드라이버 선택
3. **Select your microphone** from the device dropdown / 마이크 선택
4. **Scan for plugins** — click "Scan..." to discover installed VST plugins / "Scan..." 클릭으로 VST 스캔
5. **Add plugins** to the chain — click "+ Add Plugin" / "+ Add Plugin"으로 플러그인 추가
6. **Configure monitor output** in Monitor tab to hear yourself / Monitor 탭에서 모니터 출력 설정

## Audio Settings (Audio Tab) / 오디오 설정

### Driver Type / 드라이버 타입

- **Windows Audio (WASAPI)** — Default. Non-exclusive access — other apps can use your mic simultaneously. Separate input/output device selection. / 기본값. 비독점 접근. 입출력 장치 개별 선택.
- **ASIO** — Lower latency. Single device selection. Dynamic sample rate and buffer size from the device. "ASIO Control Panel" button for native driver settings. ASIO channel routing (input/output pair selection). / 저지연. 단일 장치. ASIO 컨트롤 패널 + 채널 라우팅.

### Sample Rate & Buffer Size / 샘플레이트 & 버퍼 크기

**WASAPI** — Fixed list of common values (44100, 48000 Hz; 64-2048 samples). / 고정 목록.

**ASIO** — Lists only what the device supports. Use the ASIO Control Panel for best results. / 장치 지원 값만 표시. ASIO 컨트롤 패널 권장.

### Channel Mode / 채널 모드

- **Stereo** (default) — Channels pass through as-is. / 채널 그대로 통과.
- **Mono** — Both channels mixed to mono. Best for voice. / 모노 믹스. 음성에 적합.

## VST Plugin Chain / VST 플러그인 체인

Supports both **VST2** (.dll) and **VST3** (.vst3) plugins in a serial chain. / VST2와 VST3 모두 지원.

- **Add** — Click "+ Add Plugin" and select from scanned list / 스캔된 목록에서 선택
- **Remove** — Click **X** on a plugin row / X 버튼으로 제거
- **Reorder** — Drag and drop a plugin row / 드래그 앤 드롭으로 순서 변경
- **Bypass** — Click the bypass toggle to skip a plugin / Bypass 토글
- **Edit** — Click **Edit** to open the native plugin GUI / 네이티브 GUI 열기

### Plugin State / 플러그인 상태

Plugin parameters (EQ curves, compressor settings, etc.) are automatically saved and restored: / 플러그인 파라미터 자동 저장/복원:

- When any setting changes (1-second debounce auto-save) / 설정 변경 시 (1초 디바운스 자동 저장)
- When switching preset slots A-E / 프리셋 슬롯 전환 시
- When closing a plugin editor window / 에디터 창 닫을 때
- On application exit / 앱 종료 시

## Quick Preset Slots (A-E) / 퀵 프리셋 슬롯

Five quick-access slots for different VST chain configurations. / 5개 체인 구성 퀵 슬롯.

- **Click a slot** to switch (current slot saved first) / 클릭으로 전환 (현재 슬롯 자동 저장)
- **Same plugins** — Instant switch (only bypass and parameters change) / 같은 플러그인이면 즉시 전환
- **Different plugins** — Async background loading, UI stays responsive / 다른 플러그인이면 비동기 로딩
- **Active slot** highlighted in purple / 활성 슬롯 보라색 표시
- **Occupied slots** shown lighter, **empty slots** dimmed / 사용 중인 슬롯은 밝게, 빈 슬롯은 어둡게

Slots save chain-only data (plugins, order, bypass, parameters). Audio and output settings are NOT affected. / 슬롯은 체인 데이터만 저장. 오디오/출력 설정은 영향 없음.

## Monitor Settings (Monitor Tab) / 모니터 설정

### Monitor Output / 모니터 출력

- **Device** — Select output device for monitoring (headphones) / 모니터링용 출력 장치 선택 (헤드폰)
- **Volume** — Adjust monitor volume / 모니터 볼륨 조절
- **Enable** — Toggle monitor on/off / 모니터 켜기/끄기
- **Status** — Shows device state: Active (green), Error (red), or "No device selected" / 디바이스 상태 표시: Active(녹색), Error(빨강), 미선택 안내

### Main Output / 메인 출력

The main processed audio goes directly to the AudioSettings Output device. To send audio to OBS, Discord, etc., select a virtual audio cable (e.g., VB-Audio Hi-Fi Cable) as the Output device in the Audio tab.

처리된 오디오는 AudioSettings Output 장치로 직접 전송된다. OBS, Discord 등에 보내려면 Audio 탭에서 가상 오디오 케이블(예: VB-Audio Hi-Fi Cable)을 Output 장치로 선택.

The monitor output (headphones) uses a separate WASAPI device configured in the Monitor tab, so you can listen while routing to a different device. / 모니터 출력(헤드폰)은 Monitor 탭에서 설정하는 별도 WASAPI 장치를 사용하므로, 다른 장치로 라우팅하면서 동시에 청취 가능.

## VST Plugin Scanner / VST 스캐너

Out-of-process scanner that safely discovers all installed plugins. / 별도 프로세스에서 안전하게 플러그인 탐색.

1. Click **"Scan..."** / "Scan..." 클릭
2. Default directories are pre-configured / 기본 경로 자동 설정
3. Click **"Scan for Plugins"** — runs in a separate process / 별도 프로세스에서 스캔
4. Bad plugin crashes -> auto-retry (up to 5 times), skips problematic plugin / 불량 플러그인 크래시 시 자동 재시도, 건너뜀
5. Scanner logs: `%AppData%/DirectPipe/scanner-log.txt` / 스캐너 로그 경로

## System Tray / 시스템 트레이

- **Close button** — Minimizes to tray (app keeps running) / X 버튼 -> 트레이 최소화
- **Double-click tray icon** — Shows the main window / 더블클릭 -> 창 복원
- **Right-click tray icon** — Menu: "Show Window" / "Start with Windows" / "Quit DirectPipe" / 우클릭 -> 메뉴

### Start with Windows / 시작 프로그램

Toggle via tray menu or Controls > General tab. Registers DirectPipe in Windows startup (HKCU Run registry). / 트레이 메뉴 또는 Controls > General 탭에서 설정. Windows 시작 시 자동 실행.

### Portable Mode / 포터블 모드

Place a file named `portable.flag` next to `DirectPipe.exe` to store all configuration in `./config/` instead of `%AppData%/DirectPipe/`. / exe 옆에 `portable.flag` 파일을 두면 설정이 `./config/`에 저장된다.

## External Control / 외부 제어

DirectPipe can be controlled while minimized or in the background. / 최소화 상태에서도 제어 가능.

### Keyboard Shortcuts / 키보드 단축키

| Shortcut / 단축키 | Action / 동작 |
|-------------------|---------------|
| Ctrl+Shift+1~9 | Toggle Plugin 1-9 Bypass / 플러그인 1-9 Bypass 토글 |
| Ctrl+Shift+0 | Master Bypass / 마스터 Bypass |
| Ctrl+Shift+M | Panic Mute / 패닉 뮤트 |
| Ctrl+Shift+N | Input Mute Toggle / 입력 뮤트 토글 |
| Ctrl+Shift+F1~F5 | Preset Slot A-E / 프리셋 슬롯 A-E |

Shortcuts are customizable in Controls > Hotkey tab. / Controls > Hotkey 탭에서 단축키 변경 가능.

### Panic Mute / 패닉 뮤트

Immediately silences all outputs. When unmuted, previous monitor enable state is restored. / 전체 출력 즉시 뮤트. 해제 시 모니터 상태 복원.

### MIDI Control / MIDI 제어

1. Open Controls tab > MIDI section / Controls 탭 > MIDI 섹션
2. Select your MIDI device / MIDI 장치 선택
3. Click [Learn] next to an action / [Learn] 클릭
4. Move a knob or press a button on your controller / 컨트롤러 조작
5. Mapping saved automatically / 자동 저장

Supports 4 mapping types: Toggle, Momentary, Continuous, NoteOnOff. Hot-plug detection with [Rescan] button. / 4가지 매핑 타입 지원. [Rescan]으로 핫플러그 감지.

### Stream Deck

See [Stream Deck Guide](STREAMDECK_GUIDE.md). / Stream Deck 가이드 참조.

### HTTP API

```bash
curl http://127.0.0.1:8766/api/bypass/0/toggle
curl http://127.0.0.1:8766/api/mute/panic
curl http://127.0.0.1:8766/api/volume/monitor/0.5
```

See [Control API Reference](CONTROL_API.md) for all endpoints. / 전체 엔드포인트는 Control API 참조.

## Troubleshooting / 문제 해결

**No audio input? / 오디오 입력이 없나요?**
- Check the correct microphone is selected in Audio tab / 올바른 마이크가 선택되어 있는지 확인
- Verify the mic works in Windows Sound Settings / Windows 사운드 설정에서 마이크 확인
- Try switching between WASAPI and ASIO / 드라이버 타입 변경 시도

**Glitches or dropouts? / 글리치나 끊김?**
- Increase the buffer size / 버퍼 크기 증가
- For ASIO, use the ASIO Control Panel to adjust / ASIO 컨트롤 패널에서 조정
- Close other audio-intensive applications / 다른 오디오 앱 종료

**Slot switching is slow? / 슬롯 전환이 느린가요?**
- First load of different plugins takes time (plugin initialization) / 처음 다른 플러그인 로드 시 초기화 시간 필요
- Subsequent switches between same plugins are instant / 이후 같은 플러그인 간 전환은 즉시
- UI remains responsive during async loading / 비동기 로딩 중 UI 응답 유지

**No monitor output? / 모니터 출력이 안 되나요?**
- Check the monitor device is selected in Monitor tab / Monitor 탭에서 모니터 장치가 선택되어 있는지 확인
- Ensure "Enable" is toggled on / "Enable"이 켜져 있는지 확인
- The monitor uses a separate WASAPI device — it works even when main driver is ASIO / 모니터는 별도 WASAPI 장치를 사용 — ASIO 모드에서도 동작

**Want to send audio to OBS/Discord? / OBS/Discord로 보내고 싶나요?**
- Install a virtual audio cable driver (e.g., VB-Audio Hi-Fi Cable) / 가상 오디오 케이블 드라이버 설치
- Select it as the Output device in the Audio tab / Audio 탭에서 Output 장치로 선택
- Set your headphones as the monitor device in the Monitor tab / Monitor 탭에서 헤드폰을 모니터 장치로 설정
