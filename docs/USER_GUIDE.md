# DirectPipe User Guide / 사용자 가이드

## What is DirectPipe? / DirectPipe란?

DirectPipe is a real-time VST2/VST3 host for Windows. It processes your microphone input through a chain of VST plugins and lets you monitor the result through headphones. You can control it remotely via hotkeys, MIDI, Stream Deck, or HTTP API while the app runs in the system tray.

DirectPipe는 Windows용 실시간 VST2/VST3 호스트다. 마이크 입력을 VST 플러그인 체인으로 처리하고 헤드폰으로 모니터링할 수 있다. 시스템 트레이에서 실행하면서 단축키, MIDI, Stream Deck, HTTP API로 원격 제어 가능.

## Quick Start / 빠른 시작

1. **Launch DirectPipe** / DirectPipe 실행
2. **Select driver type** — ASIO or Windows Audio (WASAPI) in Audio tab / Audio 탭에서 드라이버 선택
3. **Select your microphone** from the device dropdown / 마이크 선택
4. **Scan for plugins** — click "Scan..." to discover installed VST plugins / "Scan..." 클릭으로 VST 스캔
5. **Add plugins** to the chain — click "+ Add Plugin" / "+ Add Plugin"으로 플러그인 추가
6. **Configure monitor output** in Output tab to hear yourself / Output 탭에서 모니터 출력 설정

## Audio Settings (Audio Tab) / 오디오 설정

### Driver Type / 드라이버 타입

- **Windows Audio (WASAPI)** — Default. Non-exclusive access — other apps can use your mic simultaneously. Separate input/output device selection. / 기본값. 비독점 접근. 입출력 장치 개별 선택.
- **ASIO** — Lower latency. Single device selection. Dynamic sample rate and buffer size from the device. "ASIO Control Panel" button for native driver settings. / 저지연. 단일 장치. ASIO 컨트롤 패널 버튼 제공.

### Sample Rate & Buffer Size / 샘플레이트 & 버퍼 크기

**WASAPI** — Fixed list of common values (44100, 48000 Hz; 64–2048 samples). / 고정 목록.

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

## Output Settings (Output Tab) / 출력 설정

### Monitor Output / 모니터 출력

- **Device** — Select output device for monitoring / 모니터링용 출력 장치 선택
- **Volume** — Adjust monitor volume / 모니터 볼륨 조절
- **Enable** — Toggle monitor on/off (default: off) / 모니터 켜기/끄기

Lets you hear your processed audio through headphones. / 헤드폰으로 처리된 오디오를 들을 수 있다.

## VST Plugin Scanner / VST 스캐너

Out-of-process scanner that safely discovers all installed plugins. / 별도 프로세스에서 안전하게 플러그인 탐색.

1. Click **"Scan..."** / "Scan..." 클릭
2. Default directories are pre-configured / 기본 경로 자동 설정
3. Click **"Scan for Plugins"** — runs in a separate process / 별도 프로세스에서 스캔
4. Bad plugin crashes → auto-retry (up to 5 times), skips problematic plugin / 불량 플러그인 크래시 시 자동 재시도, 건너뜀

## System Tray / 시스템 트레이

- **Close button** — Minimizes to tray (app keeps running) / X 버튼 → 트레이 최소화
- **Double-click tray icon** — Shows the main window / 더블클릭 → 창 복원
- **Right-click tray icon** — Menu: "Show Window" / "Quit DirectPipe" / 우클릭 → 메뉴

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

### Panic Mute / 패닉 뮤트

Immediately silences all outputs. When unmuted, previous monitor enable state is restored. Virtual Cable output is always kept ON. / 전체 출력 즉시 뮤트. 해제 시 모니터 상태 복원. Virtual Cable은 항상 ON 유지.

### MIDI Control / MIDI 제어

1. Open Controls tab > MIDI section / Controls 탭 > MIDI 섹션
2. Select your MIDI device / MIDI 장치 선택
3. Click [Learn] next to an action / [Learn] 클릭
4. Move a knob or press a button on your controller / 컨트롤러 조작
5. Mapping saved automatically / 자동 저장

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
