## DirectPipe v4.4.0

One processed microphone stream can now feed up to **eight independent
Receivers**, while keeping the existing **A–E/Auto slots**, plugin identities
and saved OBS settings. One separate legacy connection remains available for
an older Receiver.

처리된 마이크 음성을 **최대 8개의 Receiver가 독립적으로 수신**합니다.
기존 **A–E/Auto 슬롯**, 플러그인 식별자와 OBS 저장 설정은 유지하며,
구형 Receiver 1개를 위한 별도 호환 연결도 제공합니다.

### Upgrading from 4.3.0 / 4.3.0에서 업데이트

**The 4.3.0 updater replaces only DirectPipe itself.** After the update, launch
4.4.0 and open **Settings > Update Receiver...** to update your existing Receiver
installations. Review the paths and versions; use **Choose Folder...** for a
custom plugin folder. If OBS or another app is using a Receiver, close that app
yourself and select **Retry**, or defer with **Later**. Reopen it afterward and
check the loaded Receiver version and audio.

**4.3.0 업데이터는 DirectPipe 본체만 교체합니다.** 갱신 뒤 4.4.0을 실행하고
**Settings > Update Receiver...**에서 기존 Receiver도 갱신하세요. 경로와
버전을 확인하고, 사용자 지정 위치는 **Choose Folder...**로 선택합니다.
OBS 등이 Receiver를 사용 중이면 해당 앱을 직접 종료한 뒤 **Retry**를 누르거나
**Later**로 미룰 수 있습니다. 완료 후 앱을 다시 열어 버전과 소리를 확인하세요.

Both host and Receiver must be 4.4.0 for independent reception. A new Receiver
connected to an older host still uses the old single-reader path. Keep sample
rates matched; this update does not add resampling.

독립 다중 수신에는 **본체와 Receiver 모두 4.4.0**이 필요합니다. 구형 본체에
새 Receiver를 연결하면 기존 단일 수신 경로를 사용합니다. 샘플레이트는 이전과
같이 맞춰야 하며 리샘플링은 추가하지 않습니다.

### Changes / 주요 변경

- **Independent queues:** muting, pausing or reconnecting one Receiver cannot
  consume another's data. A full queue drops only that Receiver's incoming audio;
  recovery discards stale speech. A ninth connection waits for a slot rather than
  silently using the legacy queue. Unload an unused plugin to release its slot;
  closing only its editor window is insufficient.
- **독립 수신 버퍼:** 한 Receiver의 뮤트·정지·재연결이 다른 수신 데이터를
  소비하지 않습니다. 지연된 Receiver만 오래된 데이터를 버리고 복귀합니다.
  9번째 연결은 슬롯을 기다리며, 해제하려면 편집 창만 닫지 말고 플러그인을
  제거하거나 사용 앱을 종료하세요.
- **Audio and preset fixes:** full available callbacks are preserved, large
  callbacks are handled, local mute drains queued speech, and underrun/recovery
  transitions remain bounded. Preset state changes exclude concurrent audio
  mutation; a short silence interval remains possible.
- **오디오·프리셋 수정:** 충분한 데이터의 잘못된 축소, 큰 콜백 잘림, 뮤트 해제
  후 이전 음성 재생을 수정했습니다. 프리셋 상태 변경과 오디오 처리가 겹치지
  않도록 보호하며, 전환 중 짧은 무음 가능성은 남습니다.
- **Windows Receiver upkeep:** previews existing destinations, retains same/newer
  copies, checks files in use, validates package and plugin identities, and keeps
  backups for rollback. Receiver-only updates keep DirectPipe running. Protected
  folders may require Windows administrator approval.
  Short temporary/rotated-backup names avoid unnecessary path expansion, and
  unsupported transaction paths are detected before host shutdown.
- **Windows Receiver 갱신:** 설치 경로·버전을 먼저 표시하고 같은 버전·더 최신
  설치본을 유지합니다. 사용 중인 파일, 패키지와 플러그인 신원을 확인하고
  백업·복구를 지원합니다. Receiver만 갱신하면 본체는 계속 실행되며 보호된
  폴더에는 관리자 승인이 필요할 수 있습니다.
  짧은 임시·이전 백업 이름을 사용하고 지원하지 않는 긴 경로는 본체 종료 전에
  확인합니다.

### Compatibility and verification / 호환성과 검증 범위

Receiver parameter/state formats, buffer choices, Stream Deck actions and the
legacy IPC ABI stay compatible. Every Receiver hears the same processed stream;
the host preset and IPC output switch apply to all of them. Use a Receiver's
local Mute to silence just that destination.

Receiver의 파라미터·저장 형식·버퍼 선택, Stream Deck 액션과 기존 IPC 형식은
유지합니다. 모든 Receiver는 같은 처리 음성을 받으며 본체의 프리셋·VST 출력
스위치는 전체에 적용됩니다. 한 곳만 끄려면 해당 Receiver의 Mute를 사용하세요.

Verification includes automated regressions and isolated actual-DLL, OBS filter
engine, updater-process and POSIX core checks. These do not establish physical
device/listening quality, OBS frontend behavior or interactive UAC acceptance.
See [4.4.0 validation](https://github.com/LiveTrack-X/DirectPipe/blob/v4.4.0/docs/MULTI_RECEIVER_4_4_0.md)
for the evidence boundaries. Windows packages may be unsigned; SHA-256 checks
verify integrity and do not replace code signing.

### Downloads / 다운로드

- `DirectPipe-v4.4.0-Windows.zip`
- `DirectPipe-v4.4.0-macOS.dmg`
- `DirectPipe-v4.4.0-Linux.tar.gz`
- `com.directpipe.directpipe.streamDeckPlugin`
- `checksums.sha256`

[Full changelog / 전체 변경 비교](https://github.com/LiveTrack-X/DirectPipe/compare/v4.3.0...v4.4.0)
