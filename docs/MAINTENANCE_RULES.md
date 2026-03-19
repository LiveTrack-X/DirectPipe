# DirectPipe Maintenance Rules / 유지보수 규칙

This document is the maintainer-facing rulebook for release operations, doc sync, and repository hygiene.
이 문서는 릴리즈 운영, 문서 동기화, 저장소 위생 관리를 위한 유지보수 규칙서입니다.

## 1) Scope / 범위

- v3 is legacy. Active maintenance scope is **v4 only**.
- v3는 레거시이며, 현재 유지보수 범위는 **v4만** 포함합니다.
- Source of truth repository for v4 release artifacts is:
  - `LiveTrack-X/DirectPipe` (release + binaries)
  - `LiveTrack-X/DirectLoopMic` (main mirror)

## 2) Local-Only Files / 로컬 전용 파일

Never publish the following to GitHub:
아래 항목은 GitHub에 업로드하지 않습니다.

- `CLAUDE.md`
- `.claude/`
- `docs/superpowers/`
- local test helpers intended for personal workflow only

`.gitignore` already contains these patterns. Verify before commit.
`.gitignore`에 패턴이 포함되어 있어야 하며, 커밋 전에 반드시 확인합니다.

## 3) Behavior Sync Checklist / 동작-문서 동기화 체크리스트

When behavior changes, update public docs in the same PR/commit set:
동작이 바뀌면 공개 문서를 같은 변경 묶음으로 함께 갱신합니다.

- `README.md`
- `docs/USER_GUIDE.md`
- `docs/PRODUCT_SPEC.md`
- `docs/CONTROL_API.md` (if API/state model changed)
- `docs/ReleaseNote.md`

### Mute semantics (must stay consistent)

- `input_muted`: input-only silence, chain/output keep running.
- `muted` (panic): blocks OUT/MON/VST paths, restores pre-panic state on unmute.
- During panic, OUT/MON/VST controls are lock-guarded by action logic.

### Auto slot semantics (must stay consistent)

- Auto is a **stateful special preset slot** (`index 5`), not a stateless one-shot button.
- First click may create defaults and save to slot 5.
- Subsequent clicks restore saved Auto state.
- Right-click reset restores default Auto chain.

### RNNoise limitation wording (must stay conservative)

- Current fact: NR is active at 48kHz and passthrough at non-48kHz.
- Roadmap phrasing must be soft: use **"under evaluation"** wording, not hard promises.
- Recommended phrase:
  - `Internal resampling (e.g., juce::LagrangeInterpolator) is under evaluation as a future improvement.`

## 4) Release Notes Style / 릴리즈 노트 스타일

- Use bilingual style (Korean + English) consistently per section.
- Keep existing section order unless there is a strong reason to change.
- For hotfix append updates, extend existing v4.0.x body rather than replacing with a short standalone note.
- Avoid labels like "Re-release" unless explicitly requested.

## 5) Release Execution Flow / 릴리즈 실행 흐름

1. Verify local state and tests:
   - `bash tools/pre-release-test.sh`
2. Commit + push `main` to both remotes:
   - `origin` (`DirectLoopMic`)
   - `directpipe` (`DirectPipe`)
3. Tag/re-tag `v4.0.x` as needed on both remotes.
4. Create/edit GitHub release on `LiveTrack-X/DirectPipe`.
5. Confirm workflow `Build & Release` success and uploaded assets:
   - Windows zip, macOS dmg, Linux tar.gz, Stream Deck plugin, checksums.

## 6) Final Sanity Checks / 최종 점검

- `git status` clean
- public docs and code semantics match
- release notes readable and encoding-safe (UTF-8)
- no local-only/internal files included
