## DirectPipe v4.0.6

v4.0.5 이후, Stream Deck 응답성, Panic Mute 상태 정합성, 트레이 표시, IPC/shared-memory 안전성을 보강한 v4.0.6 릴리즈입니다.
This v4.0.6 release improves Stream Deck responsiveness, Panic Mute state consistency, tray visibility, and IPC/shared-memory safety after v4.0.5.

---

### Highlights / 주요 변경

#### 1) Stream Deck update load reduction / Stream Deck 업데이트 부하 감소
- DirectPipe 상태 브로드캐스트를 즉시 반영이 필요한 control state와 고빈도 telemetry로 분리하고, telemetry 전송을 제한해 Stream Deck 전체 UI 렉 가능성을 줄였습니다.
- Split DirectPipe state broadcasts into immediate control state and high-frequency telemetry, then throttled telemetry updates to reduce Stream Deck UI load.

#### 2) Stream Deck render caching / Stream Deck 렌더 캐싱
- Stream Deck 액션들이 같은 이미지/타이틀/상태를 반복 전송하지 않도록 공통 render cache를 추가했습니다.
- Added a shared render cache so Stream Deck actions do not repeatedly send identical image/title/state updates.

#### 3) Panic Mute set-mode path / Panic Mute 명시적 set-mode 경로
- Panic Mute 액션을 명시적 `muted=true/false` set-mode 중심으로 정리해, 상태 반영 지연 중 legacy toggle이 섞여 이중 토글처럼 보이는 경로를 줄였습니다.
- Reworked Panic Mute around explicit `muted=true/false` set-mode commands, reducing paths where delayed state feedback could look like a double toggle.

#### 4) Panic Mute restore behavior / Panic Mute 복원 동작
- host 쪽 Panic Mute 복원 대기 상태를 engine mute flag와 분리해, Stream Deck의 명시적 unmute 명령이 이전 output/monitor/IPC 상태를 첫 명령에서 복원할 수 있게 보강했습니다.
- Separated host-side pending Panic Mute restore state from the engine mute flag so explicit Stream Deck unmute can restore prior output/monitor/IPC state on the first command.

#### 5) Panic Mute visual state / Panic Mute 표시 상태
- Stream Deck 프로필 캐시 갱신 후에도 muted/unmuted 이미지와 타이틀이 뒤집혀 보이지 않도록 Panic Mute key 이미지를 강제로 동기화합니다.
- Panic Mute now forces the matching Stream Deck key image/title so muted and unmuted visuals do not appear inverted after profile cache refreshes.

#### 6) Tray/menu bar Panic Mute indicator / 트레이·메뉴 바 Panic Mute 표시
- Panic Mute가 활성화되면 트레이/메뉴 바 아이콘에 빨간 사선 overlay를 표시해, 메인 창이 숨겨진 상태에서도 긴급 mute 상태를 바로 확인할 수 있습니다.
- The tray/menu bar icon now shows a red slash overlay while Panic Mute is active, making the emergency muted state visible even when the main window is hidden.

#### 7) IPC/shared-memory hardening / IPC·공유 메모리 보강
- shared-memory open 경로와 ring buffer detach 경로를 보강해, 크기를 명시하지 않는 consumer attach와 detach 이후 read/write 경로를 더 안전하게 처리합니다.
- Hardened shared-memory open and ring-buffer detach paths for safer consumer attach without explicit size and safer read/write behavior after detach.

#### 8) Docs & release notes sync / 문서·릴리스 노트 정합성
- README 최신 버전 링크를 v4.0.6으로 갱신하고, DeepWiki badge와 Windows 다운로드 파일 차단 해제 안내를 추가했습니다.
- Updated README latest links to v4.0.6, added the DeepWiki badge, and documented the Windows downloaded-file unblock step.

---

### Upgrade Notes / 업그레이드 안내
- **No API/state model break / API·상태 모델 비호환 없음**: This release does not introduce breaking control API or state schema changes.
- **Stream Deck 적용 / Stream Deck apply timing**: Stream Deck plugin 업데이트 후 Stream Deck 앱/플러그인을 재시작하면 새 Panic Mute 표시 및 응답성 개선이 적용됩니다.
- **Windows 차단 해제 / Windows unblock**: 다운로드한 실행 파일이 차단되면 `DirectPipe.exe` 우클릭 -> **속성** -> **일반** -> **보안** -> **차단 해제** -> **적용/확인**을 누르세요.

---

### Downloads / 다운로드
- `DirectPipe-v4.0.6-Windows.zip`
- `DirectPipe-v4.0.6-macOS.dmg`
- `DirectPipe-v4.0.6-Linux.tar.gz`
- `com.directpipe.directpipe.streamDeckPlugin`
- `checksums.sha256`
