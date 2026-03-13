# v4 Phase 0 + v3.10.3 Cherry-Pick Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** v3.10.3 안정성 수정 3건을 v4에 적용하고, Phase 0 기반 정비(CLAUDE.md 동기화, MEMORY.md 정리)를 완료한다.

**Architecture:** v3.10.3 freeze report의 코드 수정(SafePointer, AlertWindow, pre-release-test.sh)을 v4 코드베이스에 수동 적용. IMPROVEMENT_PLAN Phase 0의 문서 정비를 병행.

**Tech Stack:** C++17, JUCE 7, Bash, Markdown

**Source Documents:**
- `C:\Users\livet\Desktop\v3.10.3-stable-freeze-report.md`
- `C:\Users\livet\Desktop\IMPROVEMENT_PLAN.md`

---

## File Structure

| Action | File | Responsibility |
|--------|------|----------------|
| Modify | `host/Source/MainComponent.h:97` | SafePointer 교체 |
| Modify | `host/Source/MainComponent.cpp:891` | AlertWindow 이중 해제 수정 |
| Modify | `tools/pre-release-test.sh:34,196,212,232` | 빌드 방식 + 테스트 경로 + summary 버그 |
| Modify | `CLAUDE.md:18` | project version 3.10.0 → 4.0.0 |
| Modify | `C:\Users\livet\.claude\projects\...\memory\MEMORY.md` | 정리 및 현행화 |

---

## Chunk 1: v3.10.3 Cherry-Pick (코드 수정 3건)

### Task 1: outputPanelPtr_ SafePointer 교체

`rightTabs_`가 소유하는 OutputPanel이 탭 전환/소멸 시 댕글링될 수 있음. SafePointer는 Component 소멸 시 자동 null.

**Files:**
- Modify: `host/Source/MainComponent.h:97`

- [ ] **Step 1: 수정 적용**

Line 97 변경:
```diff
- OutputPanel* outputPanelPtr_ = nullptr;  // raw ptr (rightTabs_ owns the component)
+ juce::Component::SafePointer<OutputPanel> outputPanelPtr_;  // safe ref (rightTabs_ owns the component)
```

SafePointer가 `operator bool()`, `operator->()` 지원하므로 `.cpp` 사용 코드 변경 불필요.

- [ ] **Step 2: 빌드 확인**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" build/DirectPipe.sln -p:Configuration=Release -m -v:minimal -t:DirectPipe
```
Expected: 빌드 성공, 경고 없음

- [ ] **Step 3: 커밋**

```bash
git add host/Source/MainComponent.h
git commit -m "fix: replace raw OutputPanel ptr with SafePointer (v3.10.3 port)"
```

---

### Task 2: AlertWindow 이중 해제 수정

`enterModalState`의 3번째 파라미터 `true` = JUCE가 callback 후 자동 delete. callback 내에서도 `delete aw` → 이중 해제(UB). `false`로 변경하여 callback이 소유권 전담.

**Files:**
- Modify: `host/Source/MainComponent.cpp:891`

- [ ] **Step 1: 수정 적용**

Line 891 변경:
```diff
-                    }), true);
+                    }), false);
```

참고: 이 수정은 슬롯 이름 변경 AlertWindow(~line 877)에만 해당. `showUpdateDialog()`의 AlertWindow(~line 1627)는 callback에서 delete하지 않으므로 `true`가 정확 — 수정 불필요.

- [ ] **Step 2: 빌드 확인**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" build/DirectPipe.sln -p:Configuration=Release -m -v:minimal -t:DirectPipe
```
Expected: 빌드 성공

- [ ] **Step 3: 커밋**

```bash
git add host/Source/MainComponent.cpp
git commit -m "fix: AlertWindow double-delete in slot rename dialog (v3.10.3 port)"
```

---

### Task 3: pre-release-test.sh 수정 (3건)

v3.10.3에서 발견된 3가지 버그 수정: add_result 함수, 빌드 방식, 호스트 테스트 경로.

**Files:**
- Modify: `tools/pre-release-test.sh:34,196,232`

- [ ] **Step 1: add_result 함수 수정 (line 34)**

호출부가 이미 `"StepName|STATUS"` 형태로 1인자를 넘기는데, 함수가 `$1|$2`로 결합 → 결과에 빈 `|` 잔존.

```diff
- add_result() { RESULTS+=("$1|$2"); }  # "StepName|PASS" or "StepName|FAIL:detail"
+ add_result() { RESULTS+=("$1"); }
```

- [ ] **Step 2: 빌드 방식 — 전체 빌드 → 개별 타겟 (line 196)**

전체 빌드는 불필요한 타겟(ALL_BUILD)까지 빌드. 개별 타겟 빌드로 변경.

```diff
-   if "$CMAKE" --build "$BUILD_DIR" --config Release 2>&1 | tail -5; then
+   if "$CMAKE" --build "$BUILD_DIR" --config Release --target DirectPipe DirectPipeReceiver_VST directpipe-tests directpipe-host-tests 2>&1 | tail -5; then
```

- [ ] **Step 3: 호스트 테스트 경로 수정 (line 232)**

JUCE `juce_add_console_app`는 `_artefacts/Release/`에 출력. 현재 경로는 regular CMake 레이아웃.

```diff
- HOST_TEST_EXE="$BUILD_DIR/tests/Release/directpipe-host-tests.exe"
+ HOST_TEST_EXE="$BUILD_DIR/tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe"
```

참고: Core test (line 212)의 `$BUILD_DIR/tests/Release/directpipe-tests.exe`는 `add_executable` 사용이므로 경로가 정확함 — 수정 불필요.

- [ ] **Step 4: 스크립트 실행 테스트**

```bash
bash tools/pre-release-test.sh --version-only
```
Expected: summary에 `|` 잔존 없이 깔끔한 출력

- [ ] **Step 5: 커밋**

```bash
git add tools/pre-release-test.sh
git commit -m "fix: pre-release-test.sh build targets, host test path, summary bug (v3.10.3 port)"
```

---

## Chunk 2: Phase 0 — 기반 정비

### Task 4: CLAUDE.md project version 동기화 (N-0.1)

CLAUDE.md의 `project version`이 3.10.0으로 outdated. 실제 CMakeLists.txt는 4.0.0.

**Files:**
- Modify: `CLAUDE.md:18`

- [ ] **Step 1: 버전 수정**

Line 18 변경:
```diff
- - C++17, JUCE 7.0.12, CMake 3.22+, project version 3.10.0
+ - C++17, JUCE 7.0.12, CMake 3.22+, project version 4.0.0
```

- [ ] **Step 2: 커밋**

```bash
git add CLAUDE.md
git commit -m "docs: sync CLAUDE.md project version to 4.0.0"
```

---

### Task 5: MEMORY.md 정리 (N-0.2)

현재 MEMORY.md에 v3 세부사항이 남아있고, currentDate가 과거. v4 현재 상태 반영.

**Files:**
- Modify: `C:\Users\livet\.claude\projects\C--Users-livet-Desktop-DirectPipe-v4\memory\MEMORY.md`

- [ ] **Step 1: MEMORY.md 업데이트**

업데이트 내용:
- "Recent Changes (2026-03-12)" 섹션을 현행화 (v3.10.3 cherry-pick 완료 반영)
- currentDate 제거 (자동 관리됨)
- v4.0.0 현재 상태 명시

---

### Task 6: 최종 확인 및 푸시

- [ ] **Step 1: 전체 빌드 확인**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" build/DirectPipe.sln -p:Configuration=Release -m -v:minimal
```

- [ ] **Step 2: 테스트 실행**

```bash
build/tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe
```
Expected: All tests passed

- [ ] **Step 3: origin v4에 푸시**

```bash
git push origin v4
```

---

## Phase 1+ 참고사항

Phase 1 (MainComponent 분할)은 별도 plan으로 작성 필요:
- 5단계 추출 (TrayManager → UpdateChecker → SettingsAutosaver → PresetSlotBar → AppController)
- 각 단계 독립 커밋 + 빌드/테스트 확인
- 현재 MainComponent: 1,835줄 → 목표 < 600줄

Phase 2-7은 Phase 1 완료 후 순차 진행. `IMPROVEMENT_PLAN.md` 참조.
