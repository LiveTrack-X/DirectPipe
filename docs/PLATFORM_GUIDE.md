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

### 엔타이틀먼트 / Entitlements

DirectPipe는 Hardened Runtime 공증(notarization)을 위해 아래 엔타이틀먼트를 포함합니다 (`host/Resources/Entitlements.plist`):

DirectPipe includes the following entitlements for Hardened Runtime notarization (`host/Resources/Entitlements.plist`):

- `com.apple.security.device.audio-input` — 마이크 접근 / Microphone access
- `com.apple.security.cs.allow-unsigned-executable-memory` — VST 플러그인 로딩 (JIT/dynamic code) / VST plugin loading
- `com.apple.security.cs.disable-library-validation` — 서드파티 VST/AU dylib 로딩 / Third-party plugin loading

`Info.plist`에도 아래 권한 설명이 포함됩니다 / Info.plist includes these usage descriptions:
- `NSMicrophoneUsageDescription` — 마이크 권한 다이얼로그 / Microphone permission dialog
- `NSAccessibilityUsageDescription` — 접근성 권한 다이얼로그 (핫키용) / Accessibility dialog (for hotkeys)
- `NSLocalNetworkUsageDescription` — 네트워크 권한 (WebSocket/HTTP 서버) / Network (WebSocket/HTTP server)

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
  libxcursor-dev libgl-dev

# Fedora
sudo dnf install alsa-lib-devel jack-audio-connection-kit-devel \
  freetype-devel libX11-devel libXrandr-devel libXinerama-devel \
  libXcursor-devel mesa-libGL-devel
```

> `libwebkit2gtk`와 `libcurl`은 필요 없음 (`JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`).
> `libwebkit2gtk` and `libcurl` are NOT required (`JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`).

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

## 크로스 플랫폼 데이터 호환성 / Cross-Platform Data Compatibility

### 백업/복원 / Backup & Restore

백업 파일(`.dpbackup`, `.dpfullbackup`)에는 `platform` 필드가 포함됩니다. **같은 OS끼리만 복원 가능**합니다. 다른 OS에서 만든 백업을 복원하려 하면 경고 다이얼로그가 표시되고 차단됩니다.

Backup files (`.dpbackup`, `.dpfullbackup`) include a `platform` field. **Restore is only supported between the same OS.** Attempting to restore a backup from a different OS shows a warning dialog and is blocked.

이유 / Reason:
- 오디오 드라이버 타입이 OS별로 다름 (WASAPI vs CoreAudio vs ALSA)
- VST 플러그인 경로가 OS별로 다름 (`C:\Program Files\...` vs `/Library/Audio/...`)
- 핫키 코드가 OS별로 다름 (Windows VK codes vs macOS key codes)
- 오디오 장치 이름이 OS별로 다름

> `platform` 필드가 없는 레거시 백업(v3.10.0 이전)은 호환성을 위해 허용됩니다.
> Legacy backups without `platform` field (pre-v3.10.0) are accepted for compatibility.

### 프리셋 / Presets

`.dppreset` 파일은 VST 체인만 포함하므로 이론적으로 크로스 플랫폼 호환이 가능하나, 플러그인 경로와 포맷이 OS별로 다르므로 실제로는 같은 OS에서 사용하는 것을 권장합니다.

`.dppreset` files contain only the VST chain, so they're theoretically cross-platform compatible. However, plugin paths and formats differ per OS, so using presets on the same OS is recommended in practice.

### CJK 폰트 / CJK Font Support

플랫폼별로 다른 CJK 폰트를 자동 선택합니다:
- **Windows**: Malgun Gothic, Microsoft YaHei, Yu Gothic
- **macOS**: Apple SD Gothic Neo, PingFang SC, Hiragino Sans
- **Linux**: Noto Sans CJK KR, Noto Sans CJK SC, DejaVu Sans

Platform-specific CJK fonts are auto-selected for correct rendering of Korean, Chinese, and Japanese characters.

---

## 빌드 / Build

각 플랫폼별 빌드 방법은 [Build Guide](BUILDING.md)를 참조하세요.

See [Build Guide](BUILDING.md) for platform-specific build instructions.
