## What's New in v3.9.9

### 슬롯 이름 지정 / Slot Naming
- **사용자 정의 이름** — 슬롯 우클릭 → "Rename"으로 A-E 슬롯에 이름 부여 (예: `A|게임`, `B|토크`). 최대 8자, 초과 시 ".." 말줄임 — Right-click slot → "Rename" to give custom names (e.g., `A|Game`, `B|Talk`). Max 8 chars with ".." truncation
- **WebSocket/HTTP 전파** — StateBroadcaster `slot_names` 배열로 외부 컨트롤러에 이름 전달 — Slot names propagated via `slot_names` array in StateBroadcaster (WebSocket + HTTP API)
- **백업/복원 포함** — Full Backup/Restore 시 슬롯 이름도 함께 저장/복원 — Slot names included in Full Backup/Restore

### 슬롯 내보내기/가져오기 / Slot Export/Import
- **개별 슬롯 내보내기** — 우클릭 → "Export" 메뉴로 슬롯을 `.dppreset` 파일로 저장. 활성 슬롯은 현재 라이브 상태 캡처 후 내보내기 — Right-click → "Export" to save individual slot as `.dppreset` file. Active slot captures live state before export
- **슬롯 가져오기** — 우클릭 → "Import" 메뉴로 `.dppreset` 파일을 선택한 슬롯에 로드. 활성 슬롯이면 즉시 플러그인 체인 로드 — Right-click → "Import" to load `.dppreset` file into selected slot. If active, immediately loads the plugin chain
- **파일 검증** — 잘못된 형식이나 플러그인 없는 파일은 오류 알림 표시 — Invalid format or missing plugins show error notification

### 버그 수정 / Bug Fixes
- **`loadSlotAsync` 슬롯 이름 동기화** — 비동기 슬롯 로딩 시 `slotNames_` 업데이트 누락 수정. 동기/비동기 경로 모두 이름 필드를 파싱 — Fixed `loadSlotAsync` not updating `slotNames_` from parsed JSON (sync/async paths now consistent)
- **`loadSlot` 이름 초기화** — JSON에 `name` 필드가 없는 구버전 슬롯 로드 시 이전 이름이 남는 버그 수정 — Fixed old slot name persisting when loading legacy slots without `name` field
- **주석 용어 정리** — MonitorOutput/AudioEngine/AudioRingBuffer의 "virtual cable" 용어를 "monitor (headphone) output"으로 수정. AI 리서치 도구의 아키텍처 오해 원인 제거 — Fixed misleading "virtual cable" terminology in comments to "monitor (headphone) output"

---

> **슬롯 이름 지정 + 개별 슬롯 내보내기/가져오기** 중심 업데이트. 세팅 도우미가 최적화된 프리셋을 `.dppreset` 파일로 전달하고, 받는 사람이 우클릭 → Import로 즉시 적용할 수 있습니다. Receiver VST2와 Stream Deck 플러그인은 버전만 동기화 (3.9.9.0) — 기능 변경 없음, 기존 버전 그대로 사용 가능합니다.
>
> **Slot naming + individual slot export/import** update. A setup helper can share optimized presets as `.dppreset` files, and recipients can right-click → Import to apply instantly. Receiver VST2 and Stream Deck plugin version synced to 3.9.9.0 — no functional changes, existing installations do not need to be updated.

---

## Downloads

| 파일 / File | 설명 / Description |
|---|---|
| `DirectPipe-v3.9.9-win64.zip` | DirectPipe.exe + DirectPipe Receiver.dll |
| `com.directpipe.directpipe.streamDeckPlugin` | Stream Deck 플러그인 (v3.9.9.0) |

## Full Changelog
https://github.com/LiveTrack-X/DirectPipe/compare/v3.9.8...v3.9.9
