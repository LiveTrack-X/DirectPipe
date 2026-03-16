# Feature Spec: Per-Plugin Latency Display (플러그인별 레이턴시 표시)

> **상태: UI에서 제거됨** — UX 피드백에 따라 per-plugin latency display와 chain PDC summary가 UI에서 제거되었습니다.
> 백엔드 데이터(`VSTChain::getPluginLatencies()`, `getTotalChainPDC()`)와 API 브로드캐스트(`plugins[].latency_samples`, `chain_pdc_samples`, `chain_pdc_ms`)는 유지됩니다.
>
> **DirectPipe v4.1.0 구현 명세** — Claude Code 핸드오프용
>
> 이 문서는 DirectPipe에 "Per-Plugin Latency Display" 기능을 추가하기 위한 설계 명세입니다.
> 기존 코드베이스의 아키텍처(`CLAUDE.md` Key Implementations 섹션)와 스레드 안전 규칙을 준수해야 합니다.

---

## 1. 목적 / Purpose

VST 플러그인 체인에서 **각 플러그인이 보고하는 레이턴시(Plugin Delay Compensation, PDC)**를 사용자에게 시각적으로 표시.

### 왜 필요한가

- 일부 VST 플러그인(리니어 페이즈 EQ, look-ahead 컴프레서/리미터 등)은 내부 처리를 위해 **추가 레이턴시를 리포트**함
- JUCE `AudioProcessorGraph`는 이 레이턴시를 자동으로 보상(PDC)하지만, **사용자에게는 보이지 않음**
- 현재 DirectPipe는 상태 바에 **전체 레이턴시(입력 버퍼 + 처리 + 출력 버퍼)**만 표시하고, 어떤 플러그인이 얼마나 기여하는지 알 수 없음
- 방송/실시간 모니터링에서 레이턴시는 핵심 관심사 — "어떤 플러그인을 빼면 지연이 줄어드는지" 즉시 판단할 수 있어야 함

### 핵심 원칙

1. **읽기 전용** — 레이턴시 값은 플러그인이 리포트하는 것을 표시만 함. 사용자가 변경할 수 없음
2. **비침습** — 기본 상태에서는 표시하지 않거나 최소한으로 표시. 관심 있는 사용자만 확인
3. **정확성** — `AudioProcessor::getLatencySamples()` 값을 샘플과 ms 단위로 표시
4. **기존 UI 최소 변경** — PluginChainEditor의 각 행에 작은 라벨 추가 수준

---

## 2. 데이터 소스 / Data Source

### JUCE API

각 VST 플러그인 인스턴스는 `AudioProcessor::getLatencySamples()` 메서드로 자신의 레이턴시를 리포트함.

```cpp
// JUCE AudioProcessor
int getLatencySamples() const;
```

- 반환값: 샘플 수 (0 = 레이턴시 없음, >0 = look-ahead 등으로 인한 추가 지연)
- 대부분의 실시간 플러그인(EQ, 컴프레서, 노이즈 제거 등)은 **0을 리포트**
- 리니어 페이즈 EQ, look-ahead 리미터 등은 **수백~수천 샘플을 리포트**
- 이 값은 플러그인이 `prepareToPlay` 이후 또는 파라미터 변경 시 바뀔 수 있음

### 현재 VSTChain 구조

`VSTChain`의 `PluginSlot` 구조에 각 플러그인의 `AudioPluginInstance` 포인터가 있음:

```cpp
struct PluginSlot {
    juce::String name;
    juce::String path;
    juce::PluginDescription description;
    bool bypassed = false;
    juce::AudioProcessorGraph::NodeID nodeId;
    juce::AudioPluginInstance* instance = nullptr;  // ← 여기서 getLatencySamples() 호출
};
```

### 총 체인 레이턴시

`AudioProcessorGraph`의 `getLatencySamples()`는 그래프 내 모든 플러그인의 PDC를 고려한 **총 보상 레이턴시**를 반환함. 이 값은 이미 `LatencyMonitor`에서 사용 중일 수 있음.

---

## 3. 표시 방식 설계 / Display Design

### 3.1 PluginChainEditor 행별 레이턴시 라벨

**위치**: 플러그인 체인 에디터의 각 플러그인 행에 레이턴시 값 표시.

```
기존:
┌─────────────────────────────────────────┐
│ 1. ReaComp           [Edit][Bp][X]      │
│ 2. FabFilter Pro-Q   [Edit][Bp][X]      │
│ 3. ReaXcomp          [Edit][Bp][X]      │
└─────────────────────────────────────────┘

변경 후:
┌─────────────────────────────────────────────┐
│ 1. ReaComp                    [Edit][Bp][X] │
│ 2. FabFilter Pro-Q   512smp  [Edit][Bp][X] │  ← 리니어 페이즈 모드일 때
│ 3. ReaXcomp                   [Edit][Bp][X] │
└─────────────────────────────────────────────┘
```

**표시 규칙**:
- 레이턴시 0인 플러그인: **표시 안 함** (대부분의 플러그인). 공간 차지 없음
- 레이턴시 > 0인 플러그인: `"{samples}smp"` 형식으로 표시
- 색상: 흐린 텍스트 (`#8888AA`, 기존 테마의 보조 텍스트 색상)
- 폰트: 기존 체인 에디터보다 작은 크기 (10-11pt)
- 바이패스된 플러그인: 레이턴시 표시도 함께 흐리게 (바이패스 상태에서도 PDC가 적용될 수 있으므로 여전히 표시하되, 더 흐리게)

### 3.2 체인 하단 총 PDC 요약

**위치**: 플러그인 체인 에디터 하단, "Add Plugin" 버튼 위.

```
┌─────────────────────────────────────────────┐
│ 1. ReaComp                    [Edit][Bp][X] │
│ 2. FabFilter Pro-Q   512smp  [Edit][Bp][X] │
│ 3. ReaXcomp                   [Edit][Bp][X] │
├─────────────────────────────────────────────┤
│ Chain PDC: 512 samples (10.7ms @ 48kHz)     │  ← 0이 아닐 때만 표시
├─────────────────────────────────────────────┤
│ [+ Add Plugin]  [Scan...]                   │
└─────────────────────────────────────────────┘
```

**표시 규칙**:
- 총 PDC가 0이면: **줄 자체를 숨김** (대부분의 경우)
- 총 PDC > 0이면: `"Chain PDC: {samples} samples ({ms}ms @ {sampleRate}Hz)"` 형식
- ms 계산: `(samples / sampleRate) * 1000.0`, 소수점 1자리
- 색상: 주황색 계열 (경고성 정보) — 레이턴시가 있다는 걸 시각적으로 인지시킴
- 폰트: 10-11pt, 이탤릭 또는 일반

### 3.3 플러그인 에디터(GUI) 열 때 툴팁 (선택)

플러그인 행에 마우스 호버 시 상세 정보:

```
"FabFilter Pro-Q 3
 Latency: 512 samples (10.7ms @ 48kHz)
 Format: VST3"
```

이건 구현 난이도가 낮으므로 시간이 되면 추가.

---

## 4. 데이터 수집 방식 / Data Collection

### 방법: 타이머 폴링 (권장)

VSTChain에서 정기적으로 각 플러그인의 `getLatencySamples()`를 조회하여 캐시.

```cpp
// VSTChain 또는 별도 클래스에서
struct PluginLatencyInfo {
    int latencySamples = 0;
    float latencyMs = 0.0f;
};

std::vector<PluginLatencyInfo> pluginLatencies_;
int totalChainPDC_ = 0;
```

**업데이트 타이밍**:
- 플러그인 추가/제거/교체 시 (`onChainChanged` 콜백)
- 슬롯 전환 완료 시
- 주기적 폴링 (2-5Hz) — 일부 플러그인은 파라미터 변경 시 레이턴시가 바뀔 수 있음 (예: FabFilter Pro-Q의 리니어 페이즈 ↔ 미니멈 페이즈 전환)

**스레드 안전**:
- `getLatencySamples()`는 일반적으로 스레드 안전함 (단순 정수 반환)
- 하지만 `chainLock_` 보호 하에 플러그인 인스턴스에 접근해야 함 (기존 규칙 #4 준수)
- 수집된 값은 UI 스레드에서 읽으므로, `chainLock_` 안에서 로컬 벡터에 복사 후 lock 밖에서 UI 갱신
- **`chainLock_` 안에서 로그 쓰기 금지** (기존 규칙 준수)

### 방법 상세

```cpp
// VSTChain에 추가할 메서드
std::vector<PluginLatencyInfo> getPluginLatencies() const
{
    const juce::ScopedLock sl(chainLock_);
    std::vector<PluginLatencyInfo> result;
    result.reserve(chain_.size());

    for (const auto& slot : chain_)
    {
        PluginLatencyInfo info;
        if (slot.instance != nullptr)
        {
            info.latencySamples = slot.instance->getLatencySamples();
            if (currentSampleRate_ > 0)
                info.latencyMs = (float)info.latencySamples / (float)currentSampleRate_ * 1000.0f;
        }
        result.push_back(info);
    }
    return result;
}

int getTotalChainPDC() const
{
    const juce::ScopedLock sl(chainLock_);
    // AudioProcessorGraph가 보고하는 총 PDC
    if (graph_ != nullptr)
        return graph_->getLatencySamples();
    return 0;
}
```

---

## 5. VSTChain 변경 / VSTChain Changes

### VSTChain.h 추가

```cpp
struct PluginLatencyInfo {
    int latencySamples = 0;
    float latencyMs = 0.0f;
};

// 각 플러그인의 레이턴시 조회 (UI 스레드에서 호출)
std::vector<PluginLatencyInfo> getPluginLatencies() const;

// 그래프 전체 PDC (UI 스레드에서 호출)
int getTotalChainPDC() const;
```

### 현재 샘플레이트 추적

ms 변환을 위해 VSTChain이 현재 샘플레이트를 알아야 함.
`prepareToPlay`에서 이미 샘플레이트를 받고 있을 가능성이 높음 — 확인 후 `currentSampleRate_` 멤버가 없으면 추가:

```cpp
double currentSampleRate_ = 48000.0;  // prepareToPlay에서 갱신
```

---

## 6. PluginChainEditor UI 변경 / UI Changes

### 6.1 행별 레이턴시 라벨

PluginChainEditor의 각 플러그인 행(아마 내부 `PluginRow` 또는 유사한 컴포넌트)에 `Label` 추가:

```cpp
// PluginChainEditor 내부 행 컴포넌트
juce::Label latencyLabel_;

void updateLatency(int samples, float ms)
{
    if (samples > 0)
    {
        latencyLabel_.setText(juce::String(samples) + "smp", juce::dontSendNotification);
        latencyLabel_.setVisible(true);
    }
    else
    {
        latencyLabel_.setVisible(false);
    }
}
```

**레이아웃 조정**:
- 기존 행: `[번호. 이름]  --------  [Edit][Bp][X]`
- 변경 후: `[번호. 이름]  ----  [512smp]  [Edit][Bp][X]`
- latencyLabel 폭: 고정 60px (숨겨질 때 0px → 버튼들이 왼쪽으로 당겨짐, 또는 항상 60px 공간 확보)
- **권장**: 항상 공간 확보하되, 0일 때 텍스트만 숨김 (레이아웃 점프 방지)

### 6.2 체인 하단 PDC 요약 라벨

PluginChainEditor 하단에 라벨 추가:

```cpp
juce::Label chainPDCLabel_;

void updateChainPDC(int totalSamples, double sampleRate)
{
    if (totalSamples > 0 && sampleRate > 0)
    {
        float ms = (float)totalSamples / (float)sampleRate * 1000.0f;
        chainPDCLabel_.setText(
            "Chain PDC: " + juce::String(totalSamples) + " samples ("
            + juce::String(ms, 1) + "ms @ " + juce::String((int)sampleRate) + "Hz)",
            juce::dontSendNotification);
        chainPDCLabel_.setVisible(true);
    }
    else
    {
        chainPDCLabel_.setVisible(false);
    }
}
```

### 6.3 갱신 주기

PluginChainEditor가 이미 부모(MainComponent)의 타이머에서 tick을 받고 있다면 거기서 갱신.
아니면 자체 타이머를 추가:

```cpp
// 2-5Hz 폴링 (500ms ~ 200ms 간격)
void timerCallback() override
{
    auto latencies = vstChain_.getPluginLatencies();
    int totalPDC = vstChain_.getTotalChainPDC();
    double sr = audioEngine_.getSampleRate();

    // 각 행 업데이트
    for (int i = 0; i < rows_.size() && i < latencies.size(); ++i)
        rows_[i]->updateLatency(latencies[i].latencySamples, latencies[i].latencyMs);

    // 총 PDC 업데이트
    updateChainPDC(totalPDC, sr);
}
```

**최적화**: 이전 값과 동일하면 `setText` 스킵 (불필요한 repaint 방지).

---

## 7. StateBroadcaster 연동 / State Broadcast

### state JSON에 PDC 정보 추가

```json
{
  "data": {
    "plugins": [
      { "name": "ReaComp", "bypass": false, "loaded": true, "latency_samples": 0 },
      { "name": "FabFilter Pro-Q", "bypass": false, "loaded": true, "latency_samples": 512 }
    ],
    "chain_pdc_samples": 512,
    "chain_pdc_ms": 10.7,
    // ... 기존 필드 ...
  }
}
```

기존 `plugins` 배열의 각 객체에 `latency_samples` 필드 추가.
새 최상위 필드 `chain_pdc_samples`, `chain_pdc_ms` 추가.

**quickStateHash 업데이트**: PDC 값이 변경될 때 해시가 바뀌도록 포함. 단, PDC는 자주 바뀌지 않으므로 해시 비용은 최소.

> **중요**: `quickStateHash()`에 `latency_samples`, `chain_pdc_samples` 필드를 포함해야 브로드캐스트가 트리거됨.
> 현재 AppState에는 `outputVolume`, `xrunCount` 등이 이미 추가되어 있으므로 (v4.0.0 Stream Deck 개선),
> PluginState 구조체에 `int latencySamples = 0` 필드를 추가하고 해시에 포함할 것.

### HTTP API 연동

이미 추가된 `GET /api/plugins` 응답에 `latencySamples` 필드를 포함하면
Stream Deck Property Inspector에서도 레이턴시 정보를 활용할 수 있음.

---

## 8. 로깅 / Logging

```
INF [VST] Plugin latency: "FabFilter Pro-Q" reports 512 samples (10.7ms @ 48kHz)
INF [VST] Chain PDC: 512 samples (10.7ms @ 48kHz)
AUD [VST] Plugin latencies: [0] ReaComp=0smp, [1] FabFilter Pro-Q=512smp, [2] ReaXcomp=0smp
```

- 플러그인 추가/교체 시 레이턴시가 0이 아닌 플러그인만 `INF` 로그
- 전체 PDC 변경 시 `INF` 로그
- Audit 모드에서 전체 플러그인 레이턴시 목록 `AUD` 로그

---

## 9. 테스트 / Tests

### Unit Tests (directpipe-host-tests)

`test_plugin_latency_display.cpp` 또는 기존 `test_websocket_protocol.cpp`에 추가:

| 테스트 | 설명 |
|--------|------|
| `EmptyChainZeroPDC` | 빈 체인 → totalPDC = 0, latencies 빈 벡터 |
| `NoLatencyPluginsZeroPDC` | 레이턴시 0인 플러그인만 → 총 PDC = 0 |
| `LatencyPluginReported` | 레이턴시 > 0 플러그인 → 정확한 값 반환 |
| `MsCalculation` | 512 samples @ 48000Hz = 10.666...ms → 10.7ms 반올림 확인 |
| `BypassedPluginStillReports` | 바이패스 플러그인도 레이턴시 값 반환 (PDC는 여전히 적용될 수 있음) |
| `StateJsonIncludesLatency` | state JSON의 plugins 배열에 latency_samples 포함 확인 |
| `ChainPDCInStateJson` | state JSON에 chain_pdc_samples, chain_pdc_ms 포함 확인 |

> **참고**: 실제 VST 플러그인 인스턴스 없이 테스트하려면 mock/stub이 필요할 수 있음.
> `getLatencySamples()`를 직접 테스트하기 어려우면, ms 변환 로직과 JSON 직렬화만 테스트해도 충분.

### Pre-Release Dashboard

`tools/pre-release-dashboard.html`에 수동 테스트 항목 추가:

- [ ] 레이턴시 0인 플러그인 → 행에 레이턴시 표시 없음
- [ ] 리니어 페이즈 EQ 등 레이턴시 > 0 플러그인 → 행에 "{N}smp" 표시
- [ ] 체인 하단에 총 PDC 표시 (0이면 숨김)
- [ ] 플러그인 추가/제거 시 레이턴시 표시 즉시 갱신
- [ ] 슬롯 전환 시 레이턴시 표시 갱신
- [ ] 바이패스 토글해도 레이턴시 표시 유지 (흐리게)
- [ ] /api/status JSON에 latency_samples 필드 포함 확인

---

## 10. 파일 변경 요약 / File Changes

### 새 파일

| 파일 | 설명 |
|------|------|
| `tests/test_plugin_latency_display.cpp` | 유닛 테스트 (또는 기존 테스트 파일에 추가) |

### 변경 파일

| 파일 | 변경 내용 |
|------|----------|
| `host/Source/Audio/VSTChain.h` | `PluginLatencyInfo` 구조체, `getPluginLatencies()`, `getTotalChainPDC()` 추가 |
| `host/Source/Audio/VSTChain.cpp` | 위 메서드 구현, `currentSampleRate_` 추적 (없으면 추가) |
| `host/Source/UI/PluginChainEditor.h/cpp` | 행별 latencyLabel, 하단 chainPDCLabel, 타이머 폴링 |
| `host/Source/Control/StateBroadcaster.cpp` | state JSON에 `latency_samples`, `chain_pdc_samples`, `chain_pdc_ms` 추가 |
| `CMakeLists.txt` | 새 테스트 파일 등록 (있으면) |
| `CLAUDE.md` | Key Implementations 섹션에 VSTChain PDC 조회 메서드 설명 추가 |
| `docs/STREAMDECK_GUIDE.md` | Stream Deck 연동 시 레이턴시 표시 설명 추가 (해당 시) |

---

## 11. 구현 순서 / Implementation Order

```
Phase 1 (데이터):
  1. VSTChain에 getPluginLatencies(), getTotalChainPDC() 추가
  2. currentSampleRate_ 추적 확인/추가

Phase 2 (UI):
  3. PluginChainEditor 행에 latencyLabel 추가
  4. 체인 하단 chainPDCLabel 추가
  5. 타이머 폴링으로 갱신 연결
  6. 레이아웃 조정 (0일 때 숨김, >0일 때 표시)

Phase 3 (연동):
  7. StateBroadcaster state JSON에 PDC 필드 추가
  8. 테스트 작성
  9. 문서 업데이트
```

---

## 12. 참고 사항 / Notes

- `getLatencySamples()`는 대부분의 플러그인에서 0을 반환함. 실제로 레이턴시가 표시되는 경우는 드물지만, 표시될 때 매우 유용함
- JUCE `AudioProcessorGraph`의 PDC는 자동 — DirectPipe가 직접 보상할 필요 없음. 이 기능은 **표시만** 담당
- 일부 플러그인은 `prepareToPlay` 직후가 아니라 파라미터 변경 후에 레이턴시가 바뀔 수 있으므로 주기적 폴링 필요
- 바이패스 상태에서의 PDC 동작은 JUCE AudioProcessorGraph 구현에 의존. DirectPipe의 `rebuildGraph`에서 바이패스 플러그인을 그래프에서 분리하므로, 바이패스 시 해당 플러그인의 PDC는 총 PDC에서 제외될 수 있음. 이 동작을 확인하고 UI에 반영해야 함
- `chainLock_` 안에서 `getLatencySamples()` 호출 → 값 수집 → lock 밖에서 UI 갱신 (기존 스레드 안전 규칙 준수)
- Safety Limiter (별도 Feature Spec 참조)는 look-ahead 없는 설계이므로 PDC에 기여하지 않음 (0 samples)
