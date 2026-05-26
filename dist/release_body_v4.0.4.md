## DirectPipe v4.0.4

v4.0.3 이후, startup 출력 안정성과 ASIO 채널 복원 안전성을 보강한 v4.0.4 릴리즈입니다.
This v4.0.4 release improves startup output safety and ASIO channel-restore robustness after v4.0.3.

---

### Highlights / 주요 변경

#### 1) Startup output guard 강화 / hardened startup output guard
- 앱 시작 시 settings restore 완료 전 기본 드라이버 경로로 출력이 새지 않도록 startup guard를 보강했습니다.
- Strengthened startup guard so audio does not leak to the temporary default-driver path before settings restore completes.

#### 2) Legacy settings 복원 안정화 / legacy settings restore stabilization
- `outputMuted` 필드가 없는 legacy settings 로드 시, startup guard 상태가 불필요하게 남지 않도록 복원 경로를 보강했습니다.
- Improved restore path for legacy settings without `outputMuted` so startup guard state does not remain unintentionally.

#### 3) ASIO channel mask fallback 보강 / strengthened ASIO channel-mask fallback
- 드라이버가 채널 정보를 불완전하게 보고하는 경우, 채널 마스크 적용 실패 시 최소 explicit mask 재시도 후 driver default로 폴백하도록 보강했습니다.
- When channel metadata is incomplete, mask restore now retries with a minimal explicit mask and then falls back to driver defaults.

#### 4) ASIO restore mono-regression 방지 / mono-regression prevention during ASIO restore
- 채널 수 fallback 기준을 보완해, 일시적 채널명 0 보고 상황에서 모노로 축소되는 회귀 가능성을 줄였습니다.
- Improved channel-count fallback to reduce mono-collapse regression risk when drivers transiently report zero channel names.

#### 5) Tray ↔ Settings Auto-start 토글 동기화 fix / tray-settings auto-start sync fix
- 트레이 우클릭 메뉴의 `Start with System` 변경이 `Settings > Application` 토글에 즉시 반영되도록 양방향 동기화를 수정했습니다.
- Fixed bidirectional sync so changing `Start with System` in tray context menu is immediately reflected in `Settings > Application`.

#### 6) Tests & docs sync / 테스트·문서 정합성
- `SettingsAutosaver` startup guard 회귀 테스트를 추가했고, 관련 문서를 현재 동작 기준으로 동기화했습니다.
- Added startup-guard regression tests in `SettingsAutosaver` and synchronized related docs with current runtime behavior.

---

### Upgrade Notes / 업그레이드 안내
- **No API/state model break / API·상태 모델 비호환 없음**: This release does not introduce breaking schema changes.
- **적용 시점 / apply timing**: startup guard/ASIO 복원/토글 동기화 보강은 `v4.0.4` 실행 시점부터 적용됩니다.

---

### Downloads / 다운로드
- `DirectPipe-v4.0.4-Windows.zip`
- `DirectPipe-v4.0.4-macOS.dmg`
- `DirectPipe-v4.0.4-Linux.tar.gz`
- `com.directpipe.directpipe.streamDeckPlugin`
- `checksums.sha256`
