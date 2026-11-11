## DirectPipe v4.5.0

Choose **Analog 1, Analog 2 or another single hardware input** while keeping
existing input pairs and presets as the default. This release also adds an
optional Windows mic-level hold at 100, repairs mono VST routing, improves backup
error messages, and confirms Windows startup before removing verified previous
host executables.

**Analog 1·Analog 2 등 장치의 개별 입력 하나**를 선택할 수 있습니다.
기존 입력 쌍과 프리셋은 기본 동작으로 유지하며, Windows 마이크 레벨 100 유지
옵션을 추가했습니다. 모노 VST 라우팅·백업 오류 안내·Windows 업데이트 후
시작 확인과 이전 본체 EXE 정리도 개선했습니다.

### Upgrading from 4.4.x or earlier / 이전 버전에서 업데이트

**The first upgrade to 4.5.0 still uses the updater in your installed version.**
The new startup confirmation and recovery protections take effect once 4.5.0
is installed. If an older updater stops, keep its recovery files and use the
manual download below. Close DirectPipe yourself before replacing its files;
preserve your existing configuration and any date-suffixed manual backups.

**4.5.0으로 처음 갱신할 때는 현재 설치된 버전의 업데이터가 실행됩니다.**
새로운 시작 확인·복구 기능은 4.5.0 설치 후부터 적용됩니다. 구형 업데이터가
멈추면 복구 파일을 보존하고 아래 패키지로 수동 업데이트하세요. 파일 교체 전
DirectPipe를 직접 종료하고 기존 설정과 날짜를 붙인 수동 백업을 유지하세요.

After a manual update, or an older host-only update, use
**Settings > Update Receiver...** to review existing Receiver installations.
Select **Choose Folder...** for a custom location. If OBS or another host is
using a Receiver, close that application yourself, then choose **Retry**;
**Later** defers the operation. Reopen the host and check the loaded Receiver
version and audio. Receiver-only maintenance keeps DirectPipe running.

수동 업데이트나 구형 본체 전용 업데이트 뒤에는
**Settings > Update Receiver...**에서 기존 Receiver의 경로·버전을 확인하세요.
사용자 지정 위치는 **Choose Folder...**로 선택합니다. OBS 등에서 Receiver를
사용 중이면 해당 앱을 직접 종료한 뒤 **Retry**를 누르거나 **Later**로
미룰 수 있습니다. 완료 후 앱을 다시 열어 Receiver 버전과 소리를 확인하세요.
Receiver만 갱신하는 동안 DirectPipe 본체는 계속 실행됩니다.

### Changes / 주요 변경

- **Single-channel input capture:** **Input capture > Mono (single channel)**
  lists the driver's individual input names. The selected channel feeds both
  internal L/R channels at its original level before effects. On multi-input devices, returning to pair
  capture selects a complete pair and reports an unpaired final channel. Native
  single-input devices keep their existing one-channel behavior.
- **개별 모노 입력:** **Input capture > Mono (single channel)**에서 실제 장치
  채널 하나를 선택합니다. 이펙트 전단의 내부 L/R에 같은 신호를 감쇠 없이
  전달합니다. 다중 입력 장치에서 입력 쌍으로 돌아갈 때는 완전한 쌍을 사용하고,
  짝이 없는 마지막 채널은 오류로 안내합니다. 원래 입력이 하나인 장치의 기존
  동작은 유지합니다.
- **Optional Windows mic-level hold:** **Keep Windows mic level at 100**
  maintains the selected Windows Audio shared/Low Latency Mode input's Windows
  level for all apps.
  It defaults off and follows the exact opened endpoint, with no device-name
  guessing. Manual Windows slider changes are corrected while enabled; turning
  it off stops corrections without restoring an old value. ASIO, exclusive mode,
  DirectSound and inputs without an exact supported mapping are disabled with
  a reason. Mute, boost
  and DirectPipe gain remain unchanged; 100 is the driver's maximum, not
  guaranteed 0 dB.
- **선택형 Windows 마이크 레벨 유지:** **Keep Windows mic level at 100**은
  Windows Audio 공유·Low Latency 모드에서 선택한 입력의 Windows 레벨을
  모든 앱에서 100으로 유지합니다.
  기본은 꺼짐이며, 이름으로 추측하지 않고 실제 열린 장치에만 적용합니다.
  켜져 있으면 수동 슬라이더 변경도 보정하고, 끄면 과거 값 복원 없이 보정을
  중지합니다. ASIO·독점 모드·DirectSound·정확히 식별할 수 없는 입력은
  이유와 함께 비활성화합니다.
  음소거·부스트·DirectPipe 게인은 바꾸지 않으며, 100은 드라이버의 최대값으로
  반드시 0 dB를 뜻하지는 않습니다.
- **Mono VST compatibility:** mono effect outputs now reach both channels of
  the next stereo stage and final output. This fixes the left-only output
  reported with GVST GForm VST2. Effects later in the chain can still generate
  stereo; bypass restores the existing route.
- **모노 VST 호환성:** GForm 등 모노 이펙트의 출력이 다음 스테레오 단계와
  최종 출력의 양쪽 채널에 전달됩니다. GForm VST2에서 한쪽 출력만 들리던
  문제를 수정하며, 이후 이펙트의 스테레오 출력과 Bypass 경로는 유지합니다.
- **Backup restore feedback:** failed settings/full-backup restores identify
  the failed stage, reason and next check. Incomplete recovery and asynchronous
  plug-in load failures remain visible in the error messages or log.
- **백업 복원 안내:** 설정·전체 백업을 복원하지 못하면 실패 단계·이유·확인할
  항목을 표시합니다. 불완전한 복구와 비동기 플러그인 로드 오류도 안내합니다.
- **Input-gain limits:** relative controls stay within the documented 0.0–2.0
  range. Non-finite values are ignored, retaining the last valid gain.
- **입력 게인 범위:** 증감 제어를 문서의 0.0–2.0 범위로 제한하고 NaN·무한대
  값은 무시해 마지막 정상 게인을 유지합니다.
- **Autosave without audio:** controls/app settings save while no audio device
  is available and the chain is idle/unprepared. Changes merge into the preserved
  complete saved plug-in chain, or use a recovery sidecar without replacing
  chain files. A full save remains pending and resumes after preparation.
- **오디오 없는 상태의 자동 저장:** 장치 없이 체인이 준비되지 않은 유휴
  상태에서도 제어·앱 설정을 저장합니다. 완전한 저장 체인을 보존해 설정만
  병합하거나 체인 파일을 교체하지 않는 복구 파일에 기록합니다. 전체 저장은
  보류 상태로 유지하고 체인 준비 뒤 재개합니다.
- **Explicit audio state:** additive `audio_running` API telemetry lets clients
  check audio state without treating default sample-rate values as proof of a
  running stream.
- **오디오 실행 상태:** API의 `audio_running` 항목으로 기본 샘플레이트 값만
  보고 실행 중이라고 추정하지 않고 오디오 상태를 직접 확인합니다.
- **Windows updater recovery:** waits are bounded to the launching host, with
  stage/error logs and retained recovery files. Startup confirmation checks the
  updated application's identity after initial restore and its saved window/tray
  policy. A failed or unverified start keeps recovery files; an already-started
  app reports cleanup and final-completion problems separately.
- **Windows 업데이트 복구:** 본체 종료 대기에 제한을 두고 실패 단계·이유·
  복구 파일을 기록합니다. 초기 복원과 저장된 창/트레이 시작 정책 적용 뒤
  새 본체의 시작을 확인합니다. 시작 실패·미확인 시 복구 파일을 유지하며,
  시작 후 정리 실패와 최종 완료 기록 오류를 구분합니다.
- **Long Windows startup-confirmation paths:** a short sibling temporary
  filename and atomic commit avoid extra path-length growth on valid paths.
- **긴 Windows 시작 확인 경로:** 짧은 임시 파일명과 원자적 확정을 사용해,
  유효한 경로가 추가 파일명 확장 때문에 저장되지 않는 문제를 수정합니다.
- **Audio cleanup after failed startup:** callbacks and audio resources close
  even when initial audio initialization failed and recovery selected a device
  later.
- **초기 시작 실패 뒤 오디오 정리:** 오디오 초기화가 실패한 뒤 복구 과정에서
  장치를 선택한 경우에도 종료 시 콜백과 오디오 자원을 정리합니다.
- **Verified old-EXE cleanup:** known previous host EXEs are removed after a
  verified successful start. Locks, unknown identities or changed files are
  reported and retained. Date-suffixed manual backups and settings are preserved.
- **이전 EXE 정리:** 정상 시작이 확인된 뒤 검증된 이전 본체 EXE를 정리합니다.
  잠김·신원 불일치·파일 변경 시에는 보존하고 안내하며, 날짜를 붙인 수동 백업과
  설정 파일은 유지합니다.
- **Stream Deck packaging:** updates dependencies and build tools while
  retaining the Stream Deck 6.9 minimum, Node 20 runtime, existing action
  identities, assignments and button artwork.
- **Stream Deck 패키징:** 의존성과 빌드 도구를 갱신하며 Stream Deck 6.9 이상·
  Node 20 실행 환경, 기존 액션 식별자·버튼 할당·이미지를 유지합니다.

### Compatibility and verification / 호환성과 검증 범위

**Stereo pair (existing) remains the default.** Existing settings do not opt
into individual capture automatically. **Channel Mode > Mono/Stereo** keeps
its existing pre-effects meaning; mono capture does not overwrite that saved
choice. A–E/Auto slots, supported preset/backup formats, device preferences,
hotkey/MIDI mappings, Receiver/IPC formats and Stream Deck action identities
remain compatible. Device availability and fallback warnings still apply.

**Stereo pair (existing)가 기본값입니다.** 기존 설정을 자동으로 개별 입력으로
분리하지 않습니다. **Channel Mode > Mono/Stereo**의 이펙트 전단 처리 의미와
저장 값, A–E/Auto 슬롯·지원 프리셋/백업 형식·장치 설정·단축키/MIDI 매핑·
Receiver/IPC 형식·Stream Deck 액션 식별자를 유지합니다. 장치가 없거나 요청한
경로를 열지 못하는 경우의 기존 복구·안내 동작도 유지합니다.

Windows mic-level hold is off for old settings that do not contain the option.
Its per-endpoint choices are included in settings and full backups; chain-only
preset slots preserve the existing choices.

새 옵션이 없는 기존 설정에서는 Windows 마이크 레벨 유지가 꺼져 있습니다.
장치별 선택은 설정·전체 백업에 포함하며, 체인 전용 프리셋 슬롯을 불러올 때는
기존 선택을 유지합니다.

This selects one input for one processing chain. Concurrent independent
microphone chains/mixing remain future work. The independent Receiver feature
introduced in 4.4.0 is retained; this release adds no sample-rate conversion.
Keep the host and receiving application at matching sample rates.

입력 하나를 선택해 하나의 처리 체인으로 사용합니다. 여러 마이크의 동시 독립
체인·믹싱은 향후 범위입니다. 4.4.0의 독립 다중 Receiver 기능은 유지하며,
리샘플링은 추가하지 않으므로 본체와 수신 앱의 샘플레이트를 맞춰 주세요.

Verification covers automated compatibility/routing regressions, official
GForm 1.2/3.1 DLL checks and isolated Windows update/startup/rollback fixtures.
Mic-level tests use fake endpoints; local validation does not change real
microphone levels.
These software checks do not establish every device or plug-in setup, physical
audio/listening quality, native file-chooser/OBS frontend behavior or interactive
UAC acceptance. Read the [4.5.0 verification record](https://github.com/LiveTrack-X/DirectPipe/blob/v4.5.0/docs/RELEASE_4_5_0.md)
and [Windows update recovery guide](https://github.com/LiveTrack-X/DirectPipe/blob/v4.5.0/docs/WINDOWS_UPDATE_RECOVERY.md)
for detailed results and recovery instructions.

자동 호환성·라우팅 회귀 검사, 공식 GForm 1.2·3.1 DLL과 격리된 Windows
업데이트·시작·복구 시나리오를 검증합니다. 모든 장치·플러그인 조합, 실기기
오디오·청취 품질, 네이티브 파일 선택창·OBS 화면·대화형 UAC 확인은 별도입니다.
마이크 레벨 테스트는 가짜 장치를 사용하며, 로컬 검증에서 실제 마이크 레벨을
변경하지 않습니다.
상세 결과와 복구 방법은 위 검증 기록 및 업데이트 복구 안내에서 확인하세요.

### Downloads / 다운로드

| Platform / 용도 | Package / 파일 |
| --- | --- |
| Windows x64 | [DirectPipe-v4.5.0-Windows.zip](https://github.com/LiveTrack-X/DirectPipe/releases/download/v4.5.0/DirectPipe-v4.5.0-Windows.zip) |
| macOS Universal (beta) | [DirectPipe-v4.5.0-macOS.dmg](https://github.com/LiveTrack-X/DirectPipe/releases/download/v4.5.0/DirectPipe-v4.5.0-macOS.dmg) |
| Linux x86_64 (experimental) | [DirectPipe-v4.5.0-Linux.tar.gz](https://github.com/LiveTrack-X/DirectPipe/releases/download/v4.5.0/DirectPipe-v4.5.0-Linux.tar.gz) |
| Stream Deck 4.5.0.0 | [com.directpipe.directpipe.streamDeckPlugin](https://github.com/LiveTrack-X/DirectPipe/releases/download/v4.5.0/com.directpipe.directpipe.streamDeckPlugin) |
| SHA-256 | [checksums.sha256](https://github.com/LiveTrack-X/DirectPipe/releases/download/v4.5.0/checksums.sha256) |

Extract/open the package for your platform and install the included host and
Receiver formats in their existing locations. Keep `portable.flag` and the
configuration folder when using portable mode. Double-click the Stream Deck
package to install/update that plugin. Check your downloaded payload against
its entry in `checksums.sha256`; SHA-256 verifies file integrity and does not
replace code signing. Unsigned Windows downloads may trigger reputation warnings.

플랫폼별 패키지를 풀거나 열어 본체와 필요한 Receiver를 기존 위치에 설치하세요.
포터블 모드에서는 `portable.flag`와 설정 폴더를 유지합니다. Stream Deck 파일은
더블클릭해 설치·갱신합니다. 다운로드 파일의 SHA-256을 `checksums.sha256`의
해당 항목과 비교하세요. 체크섬은 무결성 확인이며 코드 서명을 대신하지 않습니다.
서명되지 않은 Windows 파일은 브라우저·SmartScreen 경고가 나타날 수 있습니다.

[Full changelog / 전체 변경 비교](https://github.com/LiveTrack-X/DirectPipe/compare/v4.4.0...v4.5.0)
