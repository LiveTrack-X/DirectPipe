# Platform Guide / 플랫폼별 가이드

DirectPipe는 Windows, macOS, Linux를 지원합니다. 플랫폼에 따라 기능과 설정이 다릅니다.

DirectPipe supports Windows, macOS, and Linux. Features and setup vary by platform.

---

## 플랫폼별 기능 비교 / Feature Comparison

| 기능 / Feature | Windows | macOS | Linux |
|---|---|---|---|
| **상태 / Status** | 안정 (Stable) | 베타 (Beta) | 실험적 (Experimental) |
| **오디오 드라이버** | WASAPI Shared/Exclusive, ASIO | CoreAudio | ALSA, JACK |
| **VST2 호스팅** | O | O | O |
| **VST3 호스팅** | O | O | O |
| **AU 호스팅** | - | O | - |
| **글로벌 핫키** | RegisterHotKey (Win32) | CGEventTap (접근성 권한 필요) | Stub (미지원) |
| **모니터 출력** | WASAPI (별도 디바이스) | CoreAudio (별도 디바이스) | ALSA/JACK |
| **자동 시작** | Registry (HKCU\Run) | LaunchAgent (plist) | XDG autostart (.desktop) |
| **다중 인스턴스** | Named Mutex | InterProcessLock | InterProcessLock |
| **시스템 트레이** | O | O (macOS 메뉴바) | O (DE 지원 시) |
| **자동 업데이트** | O (인앱 다운로드+재시작) | 알림만 (GitHub 링크) | 알림만 (GitHub 링크) |
| **Receiver VST2** | .dll | .vst (bundle) | .so |
| **Receiver VST3** | .vst3 | .vst3 (bundle) | .vst3 |
| **Receiver AU** | - | .component | - |

---

## Windows

### 오디오 드라이버 / Audio Drivers

5가지 드라이버 지원:
- **Windows Audio** (WASAPI Shared) — 추천. 비독점, 다른 앱과 동시 사용 가능
- **Windows Audio (Low Latency)** — IAudioClient3, 하드웨어 의존
- **Windows Audio (Exclusive Mode)** — 독점 모드, 다른 앱 사운드 차단
- **DirectSound** — 레거시, 사용 비추천
- **ASIO** — 전문 오디오 인터페이스용, 최저 지연

### 핫키 / Hotkeys

`RegisterHotKey` API로 시스템 전역 단축키 등록. 최소화/트레이 상태에서도 동작.
별도 권한 필요 없음.

### 자동 시작 / Auto Start

레지스트리 `HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run`에 등록.
설정 탭 또는 트레이 메뉴에서 토글.

### 자동 업데이트 / Auto Update

GitHub Releases에서 자동 다운로드 → exe 교체 → 재시작. "Update Now" 버튼.

### Receiver 설치 / Receiver Installation

`DirectPipe Receiver.dll`을 VST2 폴더에 복사:
- `C:\Program Files\VSTPlugins\` (추천)
- `C:\Program Files\Common Files\VST2\`

VST3: `DirectPipe Receiver.vst3`를 VST3 폴더에 복사:
- `C:\Program Files\Common Files\VST3\`

---

## macOS (Beta)

### 오디오 드라이버 / Audio Drivers

- **CoreAudio** — macOS 기본 오디오 시스템. 별도 드라이버 설치 불필요.

### 핫키 / Hotkeys (접근성 권한 필요)

`CGEventTap`으로 시스템 전역 키 이벤트 캡처. **접근성 권한**이 필요합니다.

1. DirectPipe 최초 실행 시 접근성 권한 요청 다이얼로그 표시
2. **시스템 설정** → **개인 정보 보호 및 보안** → **접근성** → DirectPipe 허용
3. 권한을 허용하지 않으면 핫키는 작동하지 않음 (MIDI/Stream Deck/HTTP로 대체 가능)

> System Settings → Privacy & Security → Accessibility → Allow DirectPipe.
> Without this permission, hotkeys won't work (use MIDI/Stream Deck/HTTP as alternatives).

### Gatekeeper 보안 경고 / Security Warning

코드 서명이 없으므로 첫 실행 시 Gatekeeper 경고가 나타납니다:

1. Finder에서 DirectPipe.app **우클릭** → **열기** → **열기** 클릭
2. 또는: **시스템 설정** → **개인 정보 보호 및 보안** → 하단의 **"확인 없이 열기"**

> Right-click → Open → Open. Or: System Settings → Privacy & Security → "Open Anyway".

### 자동 시작 / Auto Start

`~/Library/LaunchAgents/com.directpipe.host.plist` 파일 생성/삭제.
설정 탭 또는 트레이 메뉴에서 토글.

### 자동 업데이트 / Auto Update

새 버전 감지 시 "View on GitHub" 버튼으로 릴리즈 페이지 안내. 수동 다운로드 필요.

> macOS does not support in-app auto-update. Click "View on GitHub" to download manually.

### Receiver 설치 / Receiver Installation

**VST2** (OBS용): `DirectPipe Receiver.vst`를 VST 폴더에 복사:
- `~/Library/Audio/Plug-Ins/VST/`
- `/Library/Audio/Plug-Ins/VST/`

**VST3**: `DirectPipe Receiver.vst3`를 VST3 폴더에 복사:
- `~/Library/Audio/Plug-Ins/VST3/`

**AU**: `DirectPipe Receiver.component`를 AU 폴더에 복사:
- `~/Library/Audio/Plug-Ins/Components/`

> OBS on macOS only supports VST2 filters. Use the .vst bundle for OBS.

### macOS 알려진 제한 / Known Limitations

- ASIO 미지원 (macOS에 ASIO 없음, CoreAudio가 네이티브 저지연 지원)
- 인앱 자동 업데이트 미지원 (GitHub 링크로 수동 다운로드)
- WASAPI 관련 기능 비활성 (Exclusive Mode, Low Latency 등)

---

## Linux (Experimental)

### 오디오 드라이버 / Audio Drivers

- **ALSA** — 기본 Linux 오디오
- **JACK** — 전문 오디오 (낮은 지연, 유연한 라우팅)

> PipeWire를 사용하는 경우 JACK 호환 레이어를 통해 동작합니다.
> If using PipeWire, it works through the JACK compatibility layer.

### 시스템 패키지 / System Packages

```bash
# Ubuntu/Debian
sudo apt-get install libasound2-dev libjack-jackd2-dev \
  libfreetype-dev libx11-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libgl-dev libwebkit2gtk-4.1-dev libcurl4-openssl-dev

# Fedora
sudo dnf install alsa-lib-devel jack-audio-connection-kit-devel \
  freetype-devel libX11-devel libXrandr-devel libXinerama-devel \
  libXcursor-devel mesa-libGL-devel webkit2gtk4.1-devel libcurl-devel
```

### 핫키 / Hotkeys

현재 Linux에서는 글로벌 핫키가 지원되지 않습니다 (stub). MIDI, HTTP API, WebSocket으로 제어하세요.

> Global hotkeys are not supported on Linux (stub). Use MIDI, HTTP API, or WebSocket for control.

### 자동 시작 / Auto Start

XDG autostart 표준을 따릅니다. `~/.config/autostart/directpipe.desktop` 파일 생성/삭제.

### Receiver 설치 / Receiver Installation

**VST2**: `DirectPipe Receiver.so`를 VST 폴더에 복사:
- `~/.vst/`
- `/usr/lib/vst/`
- `/usr/local/lib/vst/`

**VST3**: `DirectPipe Receiver.vst3`를 VST3 폴더에 복사:
- `~/.vst3/`
- `/usr/lib/vst3/`
- `/usr/local/lib/vst3/`

### Linux 알려진 제한 / Known Limitations

- 글로벌 핫키 미지원
- 시스템 트레이: 데스크톱 환경에 따라 다름 (GNOME은 확장 필요)
- 인앱 자동 업데이트 미지원
- 테스트가 제한적 (실험적 지원)

---

## 빌드 / Build

각 플랫폼별 빌드 방법은 [Build Guide](BUILDING.md)를 참조하세요.

See [Build Guide](BUILDING.md) for platform-specific build instructions.
