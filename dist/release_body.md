### [최신버전](../releases/latest)이 있습니다. 해당 버전을 받아 주세요.
### A [newer version](../releases/latest) is available. Please download the latest release.

---

## What's New in v3.10.1

### Bugfix: VST Bypass / VST 바이패스 수정
- **Graph-connection bypass** — Bypassed plugins are disconnected from the signal chain (fixes RNNoise/Clear bypass not working)
- 그래프 연결 바이패스 — 바이패스된 플러그인을 신호 체인에서 분리 (RNNoise/Clear 바이패스 미작동 수정)
- **Triple bypass sync** — Graph connections + node bypass flag + bypass parameter all synced together
- 3중 바이패스 동기화 — 그래프 연결 + 노드 바이패스 플래그 + 바이패스 파라미터 모두 동기화
- **No audio stutter on bypass toggle** — `rebuildGraph(suspend=false)` skips `suspendProcessing` for connection-only changes
- 바이패스 토글 시 오디오 끊김 없음 — 연결 변경만 하는 경우 `suspendProcessing` 생략

### Bugfix: Preset Slot Oscillation / 프리셋 슬롯 진동 수정
- **Per-slot version counter** — Prevents stale preload cache from overwriting invalidated entries
- 슬롯별 버전 카운터 — 오래된 프리로드 캐시가 무효화된 항목을 덮어쓰지 못하도록 방지
- **Structure-based cache validation** — `isCachedWithStructure` compares plugin names+paths in order (not just count)
- 구조 기반 캐시 검증 — 플러그인 이름+경로를 순서대로 비교 (단순 개수가 아닌)
- **Iterator invalidation fix** — `rebuildGraph` copies connection array before removal loop
- 이터레이터 무효화 수정 — 연결 배열을 복사 후 제거 루프 실행

> Synced: Stream Deck Plugin v3.10.1.0 / DirectPipe Receiver v3.10.1

### Downloads / 다운로드

| File | Description |
|------|-------------|
| `DirectPipe-v3.10.1-win64.zip` | DirectPipe.exe + DirectPipe Receiver.dll |
| `com.directpipe.directpipe.streamDeckPlugin` | Stream Deck Plugin (double-click to install) |

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v3.10.0...v3.10.1
