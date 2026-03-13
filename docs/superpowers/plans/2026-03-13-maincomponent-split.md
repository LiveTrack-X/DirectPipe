# MainComponent Split Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** MainComponent.cpp (1835 lines)를 5개 이하의 집중된 클래스로 분할하여 600줄 이하로 축소한다.

**Architecture:** 가장 독립적인 코드부터 순서대로 추출. 각 추출 후 빌드+테스트로 회귀 없음 확인. 추출 클래스는 MainComponent를 참조하지 않고, MainComponent가 추출 클래스를 소유+위임하는 방향.

**Tech Stack:** C++17, JUCE 7, CMake

**Note:** TrayManager는 이미 `Main.cpp`에 `DirectPipeTrayIcon`으로 분리되어 있어 MainComponent 추출 대상에서 제외.

---

## File Structure

| Action | File | Responsibility |
|--------|------|----------------|
| Create | `host/Source/UI/UpdateChecker.h` | GitHub release check, update dialog, auto-update |
| Create | `host/Source/UI/UpdateChecker.cpp` | |
| Create | `host/Source/UI/PresetSlotBar.h` | A-E slot buttons, right-click menu, slot switching |
| Create | `host/Source/UI/PresetSlotBar.cpp` | |
| Modify | `host/Source/MainComponent.h` | Remove extracted members, add new class pointers |
| Modify | `host/Source/MainComponent.cpp` | Remove extracted methods, delegate to new classes |
| Modify | `host/CMakeLists.txt` | Add new source files |

---

## Chunk 1: UpdateChecker 추출

MainComponent에서 가장 독립적인 코드. GitHub API 폴링, 버전 비교, 다운로드+교체 — MainComponent의 다른 기능과 의존 없음.

**추출 대상** (MainComponent.cpp에서 제거):
- `checkForUpdate()` (1530-1608, 78줄)
- `showUpdateDialog()` (1611-1639, 28줄)
- `performUpdate()` (1643-1833, 190줄, Windows only)
- Windows update constants (29-37)
- Member vars: `updateCheckThread_`, `latestVersion_`, `latestDownloadUrl_`, `updateAvailable_`, `downloadProgress_`, `downloadThread_`

**인터페이스 설계**:
```cpp
// UpdateChecker는 creditLink_를 직접 조작하지 않음.
// 대신 콜백으로 MainComponent에 알림.
class UpdateChecker {
public:
    UpdateChecker();
    ~UpdateChecker();
    void checkForUpdate();
    void showUpdateDialog();

    // Callback: 업데이트 발견 시 호출 (message thread)
    std::function<void(const juce::String& version,
                       const juce::String& downloadUrl)> onUpdateAvailable;
};
```

### Task 1: UpdateChecker.h/cpp 생성

**Files:**
- Create: `host/Source/UI/UpdateChecker.h`
- Create: `host/Source/UI/UpdateChecker.cpp`

- [ ] **Step 1: UpdateChecker.h 작성**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once
#include <JuceHeader.h>
#include <thread>
#include <functional>

namespace directpipe {

class UpdateChecker {
public:
    UpdateChecker();
    ~UpdateChecker();

    void checkForUpdate();
    void showUpdateDialog();

    bool isUpdateAvailable() const { return updateAvailable_; }
    juce::String getLatestVersion() const { return latestVersion_; }

    // Called on message thread when a newer release is found
    std::function<void(const juce::String& version,
                       const juce::String& downloadUrl)> onUpdateAvailable;

private:
#if JUCE_WINDOWS
    void performUpdate();
    double downloadProgress_ = -1.0;
    std::thread downloadThread_;
#endif

    std::thread updateCheckThread_;
    juce::String latestVersion_;
    juce::String latestDownloadUrl_;
    bool updateAvailable_ = false;
};

} // namespace directpipe
```

- [ ] **Step 2: UpdateChecker.cpp 작성**

`MainComponent.cpp`의 lines 29-37 (Windows constants), 1530-1833 (3 methods)를 이동.
핵심 변경: `SafePointer<MainComponent>` → `shared_ptr<atomic<bool>> alive_` 패턴.
`creditLink_` 직접 조작 → `onUpdateAvailable` 콜백 호출.

- [ ] **Step 3: CMakeLists.txt에 추가**

`host/CMakeLists.txt`의 `target_sources`에 추가:
```
    Source/UI/UpdateChecker.h
    Source/UI/UpdateChecker.cpp
```
위치: `Source/UI/NotificationBar.cpp` 다음.

- [ ] **Step 4: 빌드 확인 (새 파일만, MainComponent 미수정)**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" build/DirectPipe.sln -p:Configuration=Release -m -v:minimal -t:DirectPipe
```

- [ ] **Step 5: 커밋**

```bash
git add host/Source/UI/UpdateChecker.h host/Source/UI/UpdateChecker.cpp host/CMakeLists.txt
git commit -m "refactor: extract UpdateChecker class from MainComponent"
```

---

### Task 2: MainComponent에서 UpdateChecker 코드 제거 + 위임

**Files:**
- Modify: `host/Source/MainComponent.h`
- Modify: `host/Source/MainComponent.cpp`

- [ ] **Step 1: MainComponent.h 수정**

제거할 멤버:
```cpp
// 제거 (lines 178-188):
void checkForUpdate();
void showUpdateDialog();
std::thread updateCheckThread_;
juce::String latestVersion_;
juce::String latestDownloadUrl_;
bool updateAvailable_ = false;
#if JUCE_WINDOWS
void performUpdate();
double downloadProgress_ = -1.0;
std::thread downloadThread_;
#endif
```

추가:
```cpp
#include "UI/UpdateChecker.h"
// private:
    UpdateChecker updateChecker_;
```

- [ ] **Step 2: MainComponent.cpp 수정**

Constructor에서:
- `checkForUpdate()` 호출을 `updateChecker_.checkForUpdate()`로 교체
- `updateChecker_.onUpdateAvailable` 콜백 설정 (creditLink_ 텍스트/색상/onClick 변경)

Destructor에서:
- `updateCheckThread_.join()` / `downloadThread_.join()` 제거 (UpdateChecker 소멸자가 처리)

제거:
- Lines 29-37 (Windows update constants)
- `checkForUpdate()` 메서드 전체 (1530-1608)
- `showUpdateDialog()` 메서드 전체 (1611-1639)
- `performUpdate()` 메서드 전체 (1643-1833)

- [ ] **Step 3: 빌드 확인**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" build/DirectPipe.sln -p:Configuration=Release -m -v:minimal -t:DirectPipe
```

- [ ] **Step 4: 테스트 실행**

```bash
build/tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe
```

- [ ] **Step 5: 커밋**

```bash
git add host/Source/MainComponent.h host/Source/MainComponent.cpp
git commit -m "refactor: delegate update checking to UpdateChecker class"
```

**예상 절감: ~300줄 → MainComponent ~1535줄**

---

## Chunk 2: PresetSlotBar 추출

A-E 슬롯 버튼 UI, 우클릭 메뉴, 슬롯 전환 로직을 독립 Component로 추출.

**추출 대상**:
- Constructor slot button setup (385-403, ~18줄)
- Save/Load preset buttons setup (341-383, ~42줄)
- `mouseDown()` — right-click context menu 전체 (814-965, ~152줄)
- `onSlotClicked()` (968-1034, ~66줄)
- `updateSlotButtonStates()` (1037-1062, ~25줄)
- `setSlotButtonsEnabled()` (1064-1074, ~10줄)
- `resized()` 내 slot button layout (1117-1141, ~24줄)
- handleAction의 preset 관련 cases (780-805, ~25줄)
- Member vars: `slotButtons_`, `savePresetBtn_`, `loadPresetBtn_`, `pendingSlot_`

**인터페이스 설계**:
```cpp
class PresetSlotBar : public juce::Component {
public:
    PresetSlotBar(PresetManager& pm, AudioEngine& engine,
                  PluginChainEditor& chainEditor);

    void updateSlotButtonStates();
    void setSlotButtonsEnabled(bool enabled);
    void onSlotClicked(int slotIndex);
    void handlePresetAction(const ActionEvent& event);

    // Callbacks to MainComponent
    std::function<void()> onSettingsDirty;        // markSettingsDirty()
    std::function<void()> onRefreshUI;            // refreshUI()
    std::function<void(const juce::String&, NotificationLevel)> onNotification;

    // State access (from MainComponent atomics)
    std::atomic<bool>& loadingSlot;
    std::atomic<bool>& partialLoad;

    void resized() override;
};
```

### Task 3: PresetSlotBar.h/cpp 생성

**Files:**
- Create: `host/Source/UI/PresetSlotBar.h`
- Create: `host/Source/UI/PresetSlotBar.cpp`

- [ ] **Step 1: PresetSlotBar.h 작성**

위 인터페이스 설계 기반. `juce::Component` 상속. `mouseDown` override로 right-click menu 처리.

- [ ] **Step 2: PresetSlotBar.cpp 작성**

MainComponent.cpp에서 slot 관련 코드 이동:
- Constructor setup → PresetSlotBar constructor
- `mouseDown` (814-965) → `PresetSlotBar::mouseDown`
- `onSlotClicked` (968-1034) → `PresetSlotBar::onSlotClicked`
- `updateSlotButtonStates` (1037-1062) → `PresetSlotBar::updateSlotButtonStates`
- `setSlotButtonsEnabled` (1064-1074) → `PresetSlotBar::setSlotButtonsEnabled`

MainComponent 의존성을 콜백으로 교체:
- `markSettingsDirty()` → `onSettingsDirty()`
- `refreshUI()` → `onRefreshUI()`
- `showNotification()` → `onNotification()`

- [ ] **Step 3: CMakeLists.txt에 추가**

```
    Source/UI/PresetSlotBar.h
    Source/UI/PresetSlotBar.cpp
```

- [ ] **Step 4: 빌드 확인**

- [ ] **Step 5: 커밋**

```bash
git add host/Source/UI/PresetSlotBar.h host/Source/UI/PresetSlotBar.cpp host/CMakeLists.txt
git commit -m "refactor: extract PresetSlotBar class from MainComponent"
```

---

### Task 4: MainComponent에서 PresetSlotBar 코드 제거 + 위임

**Files:**
- Modify: `host/Source/MainComponent.h`
- Modify: `host/Source/MainComponent.cpp`

- [ ] **Step 1: MainComponent.h 수정**

제거할 멤버:
```cpp
juce::TextButton savePresetBtn_{"Save Preset"};
juce::TextButton loadPresetBtn_{"Load Preset"};
static constexpr int kSlotBtnGap = 4;
static constexpr int kNumPresetSlots = 5;
std::array<std::unique_ptr<juce::TextButton>, 5> slotButtons_;
void onSlotClicked(int slotIndex);
void updateSlotButtonStates();
void setSlotButtonsEnabled(bool enabled);
int pendingSlot_ = -1;
```

추가:
```cpp
#include "UI/PresetSlotBar.h"
    std::unique_ptr<PresetSlotBar> presetSlotBar_;
```

- [ ] **Step 2: MainComponent.cpp 수정**

Constructor:
- Slot button setup (385-403) → `presetSlotBar_` 생성 + `addAndMakeVisible`
- Save/Load button setup (341-383) → PresetSlotBar로 이동
- 콜백 와이어링: `presetSlotBar_->onSettingsDirty`, `onRefreshUI`, `onNotification`

`handleAction`:
- `Action::LoadPreset`, `SwitchPresetSlot`, `NextPreset`, `PreviousPreset` → `presetSlotBar_->handlePresetAction(event)`

`mouseDown`:
- Slot right-click 전체 제거 (PresetSlotBar가 자체 처리)

`resized()`:
- Slot button layout → `presetSlotBar_->setBounds(...)` 한 줄로 교체

제거:
- `onSlotClicked()`, `updateSlotButtonStates()`, `setSlotButtonsEnabled()` 메서드 전체

- [ ] **Step 3: 빌드 확인**

- [ ] **Step 4: 테스트 실행**

- [ ] **Step 5: 커밋**

```bash
git add host/Source/MainComponent.h host/Source/MainComponent.cpp
git commit -m "refactor: delegate preset slot UI to PresetSlotBar"
```

**예상 절감: ~320줄 → MainComponent ~1215줄**

---

## Chunk 3: 최종 확인 + 추가 추출 평가

### Task 5: 빌드 + 테스트 + 줄 수 확인

- [ ] **Step 1: 전체 빌드**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" build/DirectPipe.sln -p:Configuration=Release -m -v:minimal
```

- [ ] **Step 2: 테스트 실행**

```bash
build/tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe
```

- [ ] **Step 3: 줄 수 확인**

```bash
wc -l host/Source/MainComponent.cpp
```

목표: ~1200줄 이하 (UpdateChecker -300 + PresetSlotBar -320)

- [ ] **Step 4: origin v4 푸시**

```bash
git push origin v4
```

---

## 추후 추출 (별도 plan)

UpdateChecker + PresetSlotBar 추출 후 MainComponent가 ~1200줄이면, 600줄 목표까지 추가 추출 필요:

| 추출 대상 | 예상 절감 | 복잡도 | 설명 |
|-----------|----------|--------|------|
| **SettingsAutosaver** | ~80줄 | 낮음 | markSettingsDirty, saveSettings, timerCallback dirty tick |
| **handleAction → ActionHandler** | ~210줄 | 중간 | switch문 전체를 별도 클래스로 |
| **Constructor LogPanel callbacks** | ~150줄 | 낮음 | LogPanel 콜백을 LogPanel 자체로 이동 |
| **StateBroadcaster update** | ~50줄 | 낮음 | timerCallback 내 broadcaster_ 업데이트 |

이들은 Phase 1 완료 후 효과를 평가하고 결정.
