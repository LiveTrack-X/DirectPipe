# Built-in Processors Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Filter/NoiseRemoval/AutoGain 내장 프로세서 3종을 AudioProcessor로 구현하여 VST와 동일하게 체인에 삽입. Auto 특수 프리셋 슬롯 추가.

**Architecture:** 각 내장 프로세서는 JUCE AudioProcessor 서브클래스로 구현. AudioProcessorGraph에 VST와 동일하게 삽입. VSTChain의 PluginSlot에 Type 필드 추가. Auto 슬롯은 6번째 프리셋 슬롯(이름 고정, 순환 제외).

**Tech Stack:** C++17, JUCE 7.0.12, RNNoise (BSD-3, 순수 C), ITU-R BS.1770 K-weighting

**Spec:** `docs/FEATURE_AUTO_MODE.md`
**Version:** v5.0.0 대상 (현재 4.0.0)

---

## CI/빌드 고려사항

**RNNoise = BSD-3 오픈소스 → thirdparty/에 소스 직접 커밋**
- VST2/ASIO SDK와 달리 시크릿 복원 불필요
- `thirdparty/rnnoise/` 디렉토리를 git에 포함
- CI (`build.yml`)에서 별도 단계 없이 cmake가 자동으로 빌드
- 순수 C (libm 의존) → Windows/macOS/Linux 모두 추가 종속성 없음
- `.gitignore`에서 `thirdparty/rnnoise/` 제외 확인 필요

---

## File Structure

### 신규 파일 (16개)

| 파일 | 책임 |
|------|------|
| `thirdparty/rnnoise/` | RNNoise 소스 (xiph/rnnoise에서 복사) |
| `host/Source/Audio/BuiltinFilter.h` | Filter AudioProcessor 선언 |
| `host/Source/Audio/BuiltinFilter.cpp` | HPF/LPF 구현 (IIRFilter) |
| `host/Source/Audio/BuiltinNoiseRemoval.h` | NoiseRemoval AudioProcessor 선언 |
| `host/Source/Audio/BuiltinNoiseRemoval.cpp` | RNNoise 래퍼 + FIFO + VAD 게이팅 |
| `host/Source/Audio/BuiltinAutoGain.h` | AutoGain AudioProcessor 선언 |
| `host/Source/Audio/BuiltinAutoGain.cpp` | LUFS 측정 + 비대칭 보정 AGC |
| `host/Source/UI/FilterEditPanel.h` | Filter 설정 패널 선언 |
| `host/Source/UI/FilterEditPanel.cpp` | HPF/LPF UI (AudioProcessorEditor) |
| `host/Source/UI/NoiseRemovalEditPanel.h` | NoiseRemoval 설정 패널 선언 |
| `host/Source/UI/NoiseRemovalEditPanel.cpp` | 강도 프리셋 + VAD 슬라이더 UI |
| `host/Source/UI/AGCEditPanel.h` | AGC 설정 패널 선언 |
| `host/Source/UI/AGCEditPanel.cpp` | LUFS 타겟 + 고급 설정 UI |
| `tests/test_builtin_processors.cpp` | 유닛 테스트 18개 |
| `THIRD_PARTY.md` | RNNoise BSD-3 라이선스 고지 |

### 수정 파일 (11개)

| 파일 | 변경 |
|------|------|
| `CMakeLists.txt` | rnnoise 정적 라이브러리 + 신규 소스 등록 |
| `host/CMakeLists.txt` | Builtin*.cpp 추가 |
| `tests/CMakeLists.txt` | test_builtin_processors.cpp + Builtin*.cpp |
| `host/Source/Audio/VSTChain.h/cpp` | PluginSlot::Type, addBuiltinProcessor(), addAutoProcessors() |
| `host/Source/UI/PluginChainEditor.cpp` | "Add Plugin" 메뉴에 Built-in 서브메뉴 |
| `host/Source/UI/MainComponent.cpp` | [Auto] 버튼, PresetSlotBar 확장 |
| `host/Source/UI/PresetSlotBar.h/cpp` | Auto 슬롯 (인덱스 5), isInRotation(), resetAutoSlot() |
| `host/Source/UI/PresetManager.cpp` | 내장 프로세서 타입 직렬화/역직렬화 |
| `host/Source/Control/StateBroadcaster.cpp` | plugins[].type 필드 |
| `host/Source/Control/ActionDispatcher.h` | AutoProcessorsAdd 액션 |
| `CLAUDE.md` | Built-in Processors 문서 |

---

## Chunk 1: RNNoise 빌드 통합 + BuiltinFilter

### Task 1: RNNoise 소스 통합 + CMake 설정

**Files:**
- Create: `thirdparty/rnnoise/` (xiph/rnnoise 소스)
- Create: `THIRD_PARTY.md`
- Modify: `CMakeLists.txt`
- Modify: `.gitignore`

- [ ] **Step 1: RNNoise 소스 다운로드**

```bash
cd DirectPipe-v4
git clone https://github.com/xiph/rnnoise.git /tmp/rnnoise-src
mkdir -p thirdparty/rnnoise/src thirdparty/rnnoise/include
cp /tmp/rnnoise-src/src/{denoise.c,rnn.c,rnn_data.c,rnn_reader.c,pitch.c,celt_lpc.c,kiss_fft.c} thirdparty/rnnoise/src/
cp /tmp/rnnoise-src/src/*.h thirdparty/rnnoise/src/
cp /tmp/rnnoise-src/include/rnnoise.h thirdparty/rnnoise/include/
cp /tmp/rnnoise-src/COPYING thirdparty/rnnoise/
```

⚠️ 필요한 소스 파일만 복사 (전체 repo가 아님). configure/autotools 불필요.
⚠️ `rnn_data.c`에 훈련된 모델 가중치가 포함 — 반드시 복사.

- [ ] **Step 2: CMakeLists.txt에 rnnoise 라이브러리 추가**

최상위 CMakeLists.txt에서, JUCE FetchContent 이후, host/receiver 섹션 전에:

```cmake
# ─── RNNoise (noise suppression, BSD-3) ───
add_library(rnnoise STATIC
    thirdparty/rnnoise/src/denoise.c
    thirdparty/rnnoise/src/rnn.c
    thirdparty/rnnoise/src/rnn_data.c
    thirdparty/rnnoise/src/pitch.c
    thirdparty/rnnoise/src/celt_lpc.c
    thirdparty/rnnoise/src/kiss_fft.c
)
target_include_directories(rnnoise PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rnnoise/include
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rnnoise/src
)
# Suppress warnings in third-party C code
if(MSVC)
    target_compile_options(rnnoise PRIVATE /W0)
else()
    target_compile_options(rnnoise PRIVATE -w)
endif()
```

host 타겟에 링크:
```cmake
target_link_libraries(DirectPipe PRIVATE rnnoise)
```

tests 타겟에도 링크:
```cmake
target_link_libraries(directpipe-host-tests PRIVATE rnnoise)
```

- [ ] **Step 3: .gitignore 확인**

`thirdparty/rnnoise/`가 gitignore에 의해 제외되지 않는지 확인. 필요 시 예외 추가:
```
!thirdparty/rnnoise/
```

- [ ] **Step 4: THIRD_PARTY.md 생성**

```markdown
# Third-Party Licenses

## RNNoise
- **License**: BSD-3-Clause
- **Source**: https://github.com/xiph/rnnoise
- **Usage**: Built-in noise removal processor
- **Files**: thirdparty/rnnoise/

See thirdparty/rnnoise/COPYING for full license text.
```

- [ ] **Step 5: 빌드 확인 — rnnoise 정적 라이브러리 빌드**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target rnnoise
```

- [ ] **Step 6: Commit**

```
feat(build): integrate RNNoise source (BSD-3) into thirdparty/
```

---

### Task 2: BuiltinFilter (HPF + LPF) AudioProcessor

**Files:**
- Create: `host/Source/Audio/BuiltinFilter.h`
- Create: `host/Source/Audio/BuiltinFilter.cpp`
- Modify: `host/CMakeLists.txt`

- [ ] **Step 1: BuiltinFilter.h 작성**

AudioProcessor 상속. HPF/LPF 파라미터 (atomic). getStateInformation/setStateInformation (JSON).

주요 멤버:
- `std::atomic<bool> hpfEnabled_{true}, lpfEnabled_{false}`
- `std::atomic<float> hpfFreq_{60.0f}, lpfFreq_{16000.0f}`
- `juce::IIRFilter hpfL_, hpfR_, lpfL_, lpfR_`
- `processBlock()` — RT-safe, IIRFilter 적용
- `getLatencySamples()` = 0

- [ ] **Step 2: BuiltinFilter.cpp 구현**

- `prepareToPlay`: 버터워스 계수 계산
- `processBlock`: HPF/LPF 순서대로 적용 (bypassed 체크)
- `getStateInformation`: JSON으로 4개 파라미터 직렬화
- `setStateInformation`: JSON 파싱 후 복원
- `createEditor`: FilterEditPanel 반환 (나중에 구현, 일단 nullptr)

⚠️ IIRFilter 계수 변경 시 `makeHighPass`/`makeLowPass` 재호출 — atomic freq 변경 감지해서 processBlock 안에서 갱신 (또는 prepareToPlay에서만).

- [ ] **Step 3: CMakeLists.txt 등록 + 빌드 확인**

- [ ] **Step 4: Commit**

```
feat(audio): add BuiltinFilter AudioProcessor (HPF + LPF)
```

---

### Task 3: BuiltinFilter 유닛 테스트

**Files:**
- Create: `tests/test_builtin_processors.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: 테스트 파일 생성**

FilterHPF, FilterLPF, FilterBypass, FilterStateRoundtrip 4개 테스트.

- HPF: 30Hz 사인파 입력 → HPF 80Hz → 출력 레벨 감쇠 확인
- LPF: 15kHz 사인파 → LPF 8kHz → 감쇠 확인
- Bypass: 신호 변경 없음
- StateRoundtrip: getStateInformation → 새 인스턴스 setStateInformation → 동일 파라미터

- [ ] **Step 2: CMakeLists.txt 등록 + 빌드 + 테스트**

- [ ] **Step 3: Commit**

```
test(audio): add BuiltinFilter unit tests
```

---

## Chunk 2: BuiltinNoiseRemoval (RNNoise)

### Task 4: BuiltinNoiseRemoval AudioProcessor

**Files:**
- Create: `host/Source/Audio/BuiltinNoiseRemoval.h`
- Create: `host/Source/Audio/BuiltinNoiseRemoval.cpp`
- Modify: `host/CMakeLists.txt`

이 Task가 가장 복잡. 핵심 구현 포인트:

**FIFO 버퍼링:**
```
호스트 버퍼 (가변 크기) → FIFO 누적 → 480 프레임 도달 시 rnnoise_process_frame → 출력 FIFO
```
- 입력 FIFO + 출력 FIFO (링버퍼, prepareToPlay에서 사전 할당)
- processBlock: 입력 FIFO에 push → 480 이상이면 RNNoise 처리 → 출력 FIFO에서 pop
- getLatencySamples(): FIFO 지연 리포트 (480 samples)

**리샘플링 (SR ≠ 48kHz):**
- `prepareToPlay`에서 hostSR 확인 → 48kHz 아니면 리샘플러 활성화
- `LagrangeInterpolator`로 hostSR → 48kHz → RNNoise → 48kHz → hostSR
- 리샘플링 버퍼도 사전 할당

**VAD 게이팅:**
- `rnnoise_process_frame()` 반환값 = VAD 확률
- VAD < threshold → fade out (~5ms)
- 히스테리시스: 열림/닫힘 임계값 분리

**Dual-mono:**
- `prepareToPlay`에서 RNNoise 인스턴스 2개 생성 (L/R)
- `releaseResources`에서 destroy

⚠️ `rnnoise_create()`는 내부적으로 malloc — prepareToPlay에서만 호출
⚠️ FIFO 버퍼 resize도 prepareToPlay에서만 — processBlock에서 절대 힙 할당 금지

- [ ] **Step 1: BuiltinNoiseRemoval.h 작성**
- [ ] **Step 2: BuiltinNoiseRemoval.cpp — FIFO + RNNoise 처리 루프**
- [ ] **Step 3: BuiltinNoiseRemoval.cpp — 리샘플링 경로**
- [ ] **Step 4: BuiltinNoiseRemoval.cpp — VAD 게이팅**
- [ ] **Step 5: BuiltinNoiseRemoval.cpp — getStateInformation/setStateInformation**
- [ ] **Step 6: CMakeLists 등록 + 빌드 확인**
- [ ] **Step 7: Commit**

```
feat(audio): add BuiltinNoiseRemoval AudioProcessor (RNNoise + FIFO + VAD)
```

---

### Task 5: BuiltinNoiseRemoval 유닛 테스트

**Tests:**
- NoiseRemovalVADGatingLight — VAD 0.35
- NoiseRemovalVADGatingAggressive — VAD 0.85
- NoiseRemovalBypass — 신호 변경 없음
- NoiseRemovalStateRoundtrip — 직렬화 왕복
- ResamplerActivation — 44.1kHz에서 리샘플러 활성화

⚠️ RNNoise 자체 품질 테스트는 어려움 — FIFO 동작, 바이패스, 직렬화에 집중

- [ ] **Step 1: 테스트 작성 + 등록**
- [ ] **Step 2: 빌드 + 테스트**
- [ ] **Step 3: Commit**

```
test(audio): add BuiltinNoiseRemoval unit tests
```

---

## Chunk 3: BuiltinAutoGain (LUFS AGC)

### Task 6: BuiltinAutoGain AudioProcessor

**Files:**
- Create: `host/Source/Audio/BuiltinAutoGain.h`
- Create: `host/Source/Audio/BuiltinAutoGain.cpp`
- Modify: `host/CMakeLists.txt`

핵심 구현:

**K-weighting (ITU-R BS.1770):**
- Stage 1: High shelf (+4dB @ 1681Hz) — biquad 계수
- Stage 2: HPF (38Hz, 2nd order) — biquad 계수
- 계수는 prepareToPlay에서 SR 기반 계산 (bilinear transform)
- sidechain 방식: K-weighted 신호로 측정만, 원본에 게인 적용

**Short-term LUFS (3초 윈도우):**
- 링버퍼 (3초 × SR 크기, 사전 할당)
- 매 샘플: K-weighted 제곱값을 링버퍼에 저장
- LUFS = -0.691 + 10 × log10(mean_square)

**비대칭 보정 (Luveler Mode 2):**
```
correction = target - measured
gain = correction × (correction > 0 ? lowCorrect : highCorrect)
gain = clamp(gain, -maxGain, +maxGain)
smoothedGain = envelope(gain)  // 2s attack, 3s release
```

- [ ] **Step 1: BuiltinAutoGain.h 작성**
- [ ] **Step 2: BuiltinAutoGain.cpp — K-weighting 필터**
- [ ] **Step 3: BuiltinAutoGain.cpp — LUFS 측정**
- [ ] **Step 4: BuiltinAutoGain.cpp — 비대칭 보정 + 엔벨로프**
- [ ] **Step 5: BuiltinAutoGain.cpp — getStateInformation/setStateInformation**
- [ ] **Step 6: CMakeLists 등록 + 빌드 확인**
- [ ] **Step 7: Commit**

```
feat(audio): add BuiltinAutoGain AudioProcessor (LUFS + Luveler Mode 2)
```

---

### Task 7: BuiltinAutoGain 유닛 테스트

**Tests:**
- AGCBelowTargetLUFS — Low Correct 비율 게인 증가
- AGCAboveTargetLUFS — High Correct 비율 감쇠
- AGCMaxGainClamp — 상한선 동작
- AGCBypass — 신호 변경 없음
- LUFSMeasurement — K-weighting + 3초 윈도우 정확도
- AsymmetricCorrection — Low/High 비대칭 확인

- [ ] **Step 1-3: 테스트 작성 + 등록 + 실행 + Commit**

```
test(audio): add BuiltinAutoGain unit tests (LUFS + asymmetric correction)
```

---

## Chunk 4: VSTChain 통합 + 프리셋

### Task 8: VSTChain PluginSlot 확장

**Files:**
- Modify: `host/Source/Audio/VSTChain.h`
- Modify: `host/Source/Audio/VSTChain.cpp`

- [ ] **Step 1: PluginSlot에 Type + builtinProcessor 추가**

```cpp
enum class Type { VST, BuiltinFilter, BuiltinNoiseRemoval, BuiltinAutoGain };
Type type = Type::VST;
std::unique_ptr<juce::AudioProcessor> builtinProcessor;
juce::AudioProcessor* getProcessor() const;
```

- [ ] **Step 2: addBuiltinProcessor() 구현**

내장 프로세서 인스턴스 생성 → AudioProcessorGraph에 노드 추가 → 체인에 삽입.
기존 addPlugin()과 유사하되, PluginDescription 대신 Type으로 생성.

- [ ] **Step 3: addAutoProcessors() 구현**

Filter + NoiseRemoval + AutoGain 3개를 체인 앞에 순서대로 추가.
이미 있는 타입은 스킵 (중복 방지).

- [ ] **Step 4: 기존 메서드 호환성 확인**

removePlugin, movePlugin, processBlock, rebuildGraph 등이 내장 프로세서에서도 동작하는지 확인.
`slot.instance`가 nullptr인 경우를 처리 (내장은 builtinProcessor 사용).

- [ ] **Step 5: Commit**

```
feat(chain): extend VSTChain PluginSlot for built-in processors
```

---

### Task 9: PresetManager 내장 프로세서 직렬화

**Files:**
- Modify: `host/Source/UI/PresetManager.cpp`

- [ ] **Step 1: saveSlot()에서 내장 프로세서 type 저장**

기존 VST 프리셋 JSON에 `"type"` 필드 추가:
```json
{"type": "builtin_filter", "name": "Filter", "bypassed": false, "state": "..."}
```

- [ ] **Step 2: loadSlot()에서 type에 따라 분기**

- `"vst3"` / `"vst2"` → 기존 로직 (PluginDescription으로 로드)
- `"builtin_filter"` / `"builtin_noise_removal"` / `"builtin_auto_gain"` → addBuiltinProcessor() + setStateInformation()

⚠️ 하위 호환: type 필드 없는 기존 프리셋은 VST로 취급.

- [ ] **Step 3: Commit**

```
feat(preset): serialize/deserialize built-in processor type in presets
```

---

### Task 10: PresetSlotBar — Auto 슬롯 추가

**Files:**
- Modify: `host/Source/UI/PresetSlotBar.h`
- Modify: `host/Source/UI/PresetSlotBar.cpp`
- Modify: `host/Source/UI/MainComponent.cpp`

- [ ] **Step 1: kNumPresetSlots 5 → 6**

```cpp
static constexpr int kAutoSlotIndex = 5;
```

Auto 슬롯 버튼 추가 (이름 "Auto", 다른 색상).

- [ ] **Step 2: isInRotation() / isRenameable() 가드**

```cpp
bool isInRotation(int idx) { return idx < kAutoSlotIndex; }
bool isRenameable(int idx) { return idx < kAutoSlotIndex; }
```

Next/Previous Preset 로직에서 Auto 제외.
컨텍스트 메뉴에서 Rename 비활성화.

- [ ] **Step 3: resetAutoSlot() 구현**

Auto 슬롯 초기화: 체인 클리어 → addAutoProcessors() → 기본 파라미터.

- [ ] **Step 4: MainComponent에 [Auto] 버튼 배치**

입력 게인 슬라이더 옆에 [Auto] 버튼. 클릭 → Auto 슬롯 활성화.
첫 사용 시 resetAutoSlot() 호출.

- [ ] **Step 5: Commit**

```
feat(ui): add Auto preset slot (6th slot, non-renameable, rotation-excluded)
```

---

## Chunk 5: UI Edit Panels

### Task 11: FilterEditPanel (AudioProcessorEditor)

**Files:**
- Create: `host/Source/UI/FilterEditPanel.h/cpp`

- [ ] **Step 1: AudioProcessorEditor 상속 구현**

HPF on/off + 프리셋 드롭다운 (60/80/120Hz) + 커스텀 슬라이더.
LPF on/off + 프리셋 드롭다운 + 커스텀 슬라이더.

- [ ] **Step 2: BuiltinFilter::createEditor()에서 반환**

- [ ] **Step 3: Commit**

```
feat(ui): add FilterEditPanel for built-in Filter processor
```

---

### Task 12: NoiseRemovalEditPanel

**Files:**
- Create: `host/Source/UI/NoiseRemovalEditPanel.h/cpp`

- [ ] **Step 1: 강도 드롭다운 (약/중/강) + 고급 VAD 슬라이더**
- [ ] **Step 2: Commit**

```
feat(ui): add NoiseRemovalEditPanel with strength presets
```

---

### Task 13: AGCEditPanel

**Files:**
- Create: `host/Source/UI/AGCEditPanel.h/cpp`

- [ ] **Step 1: LUFS 타겟 슬라이더 + 실시간 측정값 표시 + 고급 설정**
- [ ] **Step 2: Commit**

```
feat(ui): add AGCEditPanel with LUFS target and advanced settings
```

---

### Task 14: PluginChainEditor — Built-in 메뉴

**Files:**
- Modify: `host/Source/UI/PluginChainEditor.cpp`

- [ ] **Step 1: showAddPluginMenu()에 Built-in 서브메뉴 추가**

```cpp
juce::PopupMenu builtinMenu;
builtinMenu.addItem(901, "Filter (HPF + LPF)");
builtinMenu.addItem(902, "Noise Removal (RNNoise)");
builtinMenu.addItem(903, "Auto Gain (LUFS AGC)");
menu.addSubMenu("Built-in", builtinMenu);
```

결과 처리: 901-903 → vstChain_.addBuiltinProcessor(type)

- [ ] **Step 2: 내장 프로세서 행 표시 — 이름에 "(내장)" 접미사**

PluginRowComponent에서 slot.type != VST면 이름에 접미사 추가.

- [ ] **Step 3: Commit**

```
feat(ui): add Built-in submenu to Add Plugin and show (내장) suffix
```

---

## Chunk 6: 외부 제어 + 문서

### Task 15: StateBroadcaster type 필드 + ActionDispatcher

**Files:**
- Modify: `host/Source/Control/StateBroadcaster.cpp`
- Modify: `host/Source/Control/ActionDispatcher.h`
- Modify: `host/Source/Control/ActionHandler.cpp`
- Modify: `host/Source/Control/ControlMapping.cpp`
- Modify: `host/Source/Control/HttpApiServer.cpp`
- Modify: `host/Source/Control/WebSocketServer.cpp`

- [ ] **Step 1: toJSON() plugins[] 배열에 type 필드 추가**
- [ ] **Step 2: AutoProcessorsAdd 액션 추가**
- [ ] **Step 3: HTTP /api/auto/add + WS auto_add 명령**
- [ ] **Step 4: Commit**

```
feat(control): add built-in processor type in state + AutoProcessorsAdd action
```

---

### Task 16: 통합 테스트 + 문서

**Files:**
- Modify: `tests/test_builtin_processors.cpp` (추가 테스트)
- Modify: `CLAUDE.md`
- Modify: `docs/CONTROL_API.md`

- [ ] **Step 1: 통합 테스트 추가**

- ChainWithBuiltinAndVST
- PresetSaveLoadBuiltin
- AutoButtonAddsThree

- [ ] **Step 2: CLAUDE.md + CONTROL_API.md 업데이트**
- [ ] **Step 3: Commit**

```
docs: update CLAUDE.md and CONTROL_API for built-in processors
```

---

## Implementation Order

```
Chunk 1 (Task 1-3):  RNNoise 빌드 + BuiltinFilter + 테스트
Chunk 2 (Task 4-5):  BuiltinNoiseRemoval (가장 복잡) + 테스트
Chunk 3 (Task 6-7):  BuiltinAutoGain (LUFS) + 테스트
Chunk 4 (Task 8-10): VSTChain 통합 + PresetManager + Auto 슬롯
Chunk 5 (Task 11-14): UI Edit Panels + 메뉴
Chunk 6 (Task 15-16): 외부 제어 + 문서
```

Chunk 1-3은 독립적 (각 프로세서가 독립 AudioProcessor).
Chunk 4는 Chunk 1-3 완료 후.
Chunk 5-6은 Chunk 4 완료 후.

**예상 빌드 검증 포인트:**
- Chunk 1 완료 후: rnnoise 빌드 + BuiltinFilter 테스트 통과
- Chunk 3 완료 후: 3개 프로세서 모두 독립 테스트 통과
- Chunk 4 완료 후: 체인에 내장 프로세서 삽입 + 프리셋 저장/복원
- Chunk 6 완료 후: 전체 빌드 + 전체 테스트 통과
