## DirectPipe v4.0.2 - Input/Panic 동작 정합화 + SR 일관성 / Input/Panic Semantics + SR Consistency

> v4.0.1 안정화 패치 이후 후속 릴리즈입니다.
> Follow-up release after the v4.0.1 stability patch.
>
> 핵심: 뮤트 의미 명확화, API/상태 모델 일치, SR 변화 시 NR 타이밍 일관성.
> Focus: clearer mute semantics, consistent API/state model, and SR-safe NR timing.

---

### Highlights / 주요 변경

#### 1) Independent Input Mute (UI + API + State) / 독립 입력 뮤트
- 입력 전용 뮤트 동작을 완성했습니다: **마이크 입력만 무음**, 체인/출력 경로는 유지.
- Completed independent **Input Mute** behavior: silences mic input only while chain/output paths keep running.
- `input_muted`는 이제 panic `muted`와 명시적으로 독립입니다.
- `input_muted` is now explicitly independent from panic `muted`.
- HTTP/WS 상태 브로드캐스트에서 input mute를 그대로 제공합니다.
- Input mute state is now consistently exposed over HTTP/WS state broadcasts.

#### 2) Panic Mute Clarification / 패닉 뮤트 정의 명확화
- Panic은 비상 출력 차단으로 유지됩니다: OUT/MON/VST 차단 + 녹음 자동 중지.
- Panic remains an emergency output-path block: OUT/MON/VST blocked + active recording stops.
- 해제 시 사용자 설정(기존 뮤트 상태)을 복원합니다.
- On unmute, pre-panic user mute states are restored.
- 패닉 중 예외 허용 액션: `InputMuteToggle`, `XRunReset`, `SafetyLimiterToggle`, `SetSafetyLimiterCeiling`, `AutoProcessorsAdd`
- Allowed panic-time exceptions: `InputMuteToggle`, `XRunReset`, `SafetyLimiterToggle`, `SetSafetyLimiterCeiling`, `AutoProcessorsAdd`

#### 3) UI Semantics / UI 상태 표현 개선
- INPUT: 녹색(활성) / 빨강(입력 뮤트)
- INPUT: green(active) / red(muted)
- OUT/MON/VST: 녹색(정상) / 주황(사용자 뮤트) / 빨강(패닉 잠금)
- OUT/MON/VST: green(normal) / orange(user mute) / red(panic lock)
- PANIC 활성 라벨은 `UNMUTE`, AUTO 활성은 고대비 노랑 테마로 통일.
- PANIC active label shows `UNMUTE`; AUTO active uses high-contrast yellow styling.

#### 4) State Model/API / 상태 모델 및 API 정합성
- `active_slot`을 **0-5**로 통일했습니다 (`5=Auto`, `-1`=no active slot).
- Unified `active_slot` to **0-5** (`5=Auto`, `-1` when no active slot).
- `auto_slot_active`는 하위 호환용으로 유지하되 deprecated 처리했습니다.
- `auto_slot_active` remains for backward compatibility but is deprecated.

#### 5) Stability & DSP Timing / 안정성 및 DSP 타이밍
- XRun 60초 윈도우 드리프트를 실시간 경과 기준으로 수정.
- Fixed XRun 60-second window drift using real elapsed time.
- CPU/XRun 라벨 클릭 리셋 기능 추가.
- Added click-to-reset on CPU/XRun label.
- `VSTChain.isStable()` 기반 자동 저장 안전망 추가.
- Added `VSTChain.isStable()` as an additional auto-save safety guard.
- NR hold/smoothing 상수를 런타임 SR 기준으로 재계산해 시간 동작을 고정.
- Recomputed NR hold/smoothing from runtime SR to keep timing behavior consistent across sample rates.

#### 6) Documentation + Comment Sync / 문서 + 주석 동기화
- README와 `docs/`의 Input/Panic 설명을 실제 코드 동작에 맞게 정렬.
- Synced README and `docs/` Input/Panic descriptions with actual code behavior.
- 유지보수자가 추론하기 쉽도록 Control 계층 주석을 보강.
- Strengthened control-layer comments for clearer maintenance ownership and intent.

---

### Upgrade Notes / 업그레이드 안내

- **Controller/API 연동자 / integrators**
  - `input_muted`와 `muted`를 별도 상태로 처리하세요.
  - Treat `input_muted` and `muted` as separate state fields.
  - 슬롯 상태는 `active_slot` (`0-5` / `-1`)을 우선 사용하세요.
  - Prefer `active_slot` (`0-5` / `-1`) as the source of truth.
  - `auto_slot_active`는 레거시 호환용으로만 유지하세요.
  - Keep `auto_slot_active` only as a legacy compatibility bridge.

- **운영 관점 / operations**
  - Panic 해제 시 사용자 뮤트 상태가 복원됩니다.
  - Panic unmute restores pre-panic user mute states.
  - Input Mute는 panic 전/중/후에도 독립적으로 동작합니다.
  - Input Mute remains independent before/during/after panic.

---

### Downloads / 다운로드

| File | Description |
|------|-------------|
| `DirectPipe-v4.0.2-Windows.zip` | DirectPipe.exe + Receiver VST2 (`.dll`) + VST3 (`.vst3`) |
| `DirectPipe-v4.0.2-macOS.dmg` | DirectPipe.app + Receiver VST2 (`.vst`) + VST3 (`.vst3`) + AU (`.component`) |
| `DirectPipe-v4.0.2-Linux.tar.gz` | DirectPipe + Receiver VST2 (`.so`) + VST3 (`.vst3`) |
| `com.directpipe.directpipe.streamDeckPlugin` | Stream Deck plugin package / 스트림덱 플러그인 패키지 |
| `checksums.sha256` | SHA-256 checksums / 무결성 검증 체크섬 |

> Synced versions / 동기화 버전: Stream Deck plugin 4.0.2.0 / DirectPipe host 4.0.2

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.0.1...v4.0.2
