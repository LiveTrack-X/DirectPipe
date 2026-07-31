## DirectPipe v4.2.8

v4.2.8 is a focused Windows device-restoration reliability update. It keeps
the last saved driver, input, and output across reboot/startup and silent
endpoint fallback instead of adopting the first enumerated or system-default
device.

v4.2.8은 Windows 장치 복원 신뢰성에 집중한 업데이트입니다. 재부팅·시작
및 오류 문자열 없이 발생하는 endpoint fallback 뒤에도 목록 첫 장치나
시스템 기본 장치를 새 설정으로 수용하지 않고 마지막으로 저장한 드라이버,
입력, 출력을 복원합니다.

### Highlights / 주요 변경

- **Restore the actual last selection**: Driver, input, and output identities
  are restored independently; no device name such as VB-Cable is hard-coded.
- **실제 마지막 선택 복원**: 드라이버·입력·출력 신원을 각각 복원하며
  VB-Cable을 포함한 특정 장치명을 고정하지 않습니다.
- **Reject silent fallback**: A callback that starts on a different endpoint
  or driver is treated as recovery state even when JUCE reports no error.
- **무음 fallback 거부**: JUCE가 오류를 반환하지 않더라도 저장값과 다른
  endpoint나 드라이버가 시작되면 복구 대상으로 처리합니다.
- **Manual choices win**: Generation-gated deferred work cannot overwrite a
  later manual device selection, `Output: None`, or restored startup settings.
- **최신 사용자 선택 우선**: 세대 번호로 보호된 지연 작업은 이후의 수동
  장치 선택, `Output: None`, 시작 복원 설정을 덮어쓰지 못합니다.
- **Fail closed on unusable starts**: Invalid rate/buffer/device identity keeps
  output muted and recovery pending rather than adopting an unsafe stream.
- **사용 불가 시작은 안전 중단**: rate·buffer·장치 신원이 유효하지 않으면
  잘못 열린 스트림을 수용하지 않고 출력 mute와 복구 대기를 유지합니다.

### Compatibility / 호환성

- IPC protocol v1 and the 192-byte shared-memory header are unchanged.
- IPC protocol v1과 192-byte shared-memory header는 변경되지 않았습니다.
- Receiver, Stream Deck action/state schemas, presets, and settings remain
  compatible. The saved device names already stored by each user remain the
  source of truth.
- Receiver, Stream Deck action/state schema, preset, settings는 호환되며 각
  사용자가 저장한 장치 이름이 복원 기준입니다.

### Validation / 검증

- Local Windows Release validation completed all 582 CTest registrations:
  580 passed, 2 environment-dependent tests skipped, and 0 failed
  (59 core + 521 host + 2 focused endpoint).
- 로컬 Windows Release 검증은 CTest 582개 중 580개 통과, 환경 의존 2개
  건너뜀, 실패 0개로 완료했습니다(59 core + 521 host + 2 focused
  endpoint).
- Publication requires the exact `v4.2.8` tag to pass the Windows, macOS,
  Linux, Stream Deck, executable-identity, unsigned-state, and checksum gates.
- 정확한 `v4.2.8` 태그가 Windows, macOS, Linux, Stream Deck, 실행 파일
  신원, unsigned 상태, checksum 게이트를 통과한 경우에만 게시합니다.
- No trusted Windows code-signing certificate is configured. The Windows
  package is unsigned and may trigger browser/SmartScreen reputation warnings;
  verify the official ZIP with `checksums.sha256`.
- 신뢰된 Windows 코드 서명 인증서가 없어 Windows 패키지는 unsigned이며
  브라우저/SmartScreen 평판 경고가 나타날 수 있습니다. 공식 ZIP을
  `checksums.sha256`으로 검증하세요.
- Real-device audio, third-party VST crash containment, and macOS/Linux hardware
  behavior were not verified for this release.
- 실기기 오디오, 제3자 VST crash containment, macOS/Linux 하드웨어 동작은
  이번 릴리즈에서 검증하지 않았습니다.

### Downloads / 다운로드

- `DirectPipe-v4.2.8-Windows.zip`
- `DirectPipe-v4.2.8-macOS.dmg`
- `DirectPipe-v4.2.8-Linux.tar.gz`
- `com.directpipe.directpipe.streamDeckPlugin`
- `checksums.sha256`

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.7...v4.2.8

**전체 변경 비교**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.2.7...v4.2.8
