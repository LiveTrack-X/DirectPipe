# Noise Removal 내부 리샘플링 설계 / NR Internal Resampling Design

> 상태: 미구현 (TODO) / Status: Not implemented (TODO)
> 우선순위: 중 / Priority: Medium

## 문제 / Problem

RNNoise는 48kHz 전용. 비-48kHz 환경에서 Auto의 핵심 기능(NR)이 비활성화됨.

RNNoise requires exactly 48kHz. At non-48kHz sample rates, Auto's core noise removal is disabled (passthrough).

## 설계 / Design

```
Input (hostSR) → Upsample to 48kHz → FIFO → RNNoise 480-frame → Downsample to hostSR → Output
```

### 구현 방법 / Implementation

1. `juce::LagrangeInterpolator` (JUCE 내장, 4차 보간)
2. 업샘플 FIFO: hostSR → 48kHz 변환 후 480 샘플 프레임 축적
3. 다운샘플 FIFO: 48kHz → hostSR 변환 후 호스트 버퍼 크기에 맞춤

### 고려사항 / Considerations

- 레이턴시 증가: 리샘플링 FIFO + RNNoise 480 samples
- CPU 증가: 2x 리샘플링 연산 (작지만 RT 스레드)
- 대안: libsamplerate (SRC_SINC_FASTEST) — 더 나은 품질, 외부 의존성
- getLatencySamples() 업데이트 필요

### SR-dependent 상수 (완료) / SR-dependent Constants (Done)

`holdSamples_`와 `gateSmooth_`가 `prepareToPlay`에서 SR 기반으로 재계산됨.
이전에는 48kHz 하드코딩이었으나 non-48kHz에서 시간 상수가 잘못 적용되던 문제 수정.

- `holdSamples_ = sampleRate * 0.300` (300ms hold time)
- `gateSmooth_ = exp(-1 / (sampleRate * 0.020))` (20ms smoothing)

이 수정은 리샘플링과 무관하게 적용됨 (리샘플링 구현 후에도 유지).

### 입력 기반 SR 동기화 전략 (미래) / Input-based SR Sync Strategy (Future)

WASAPI 환경에서 입력 디바이스의 고유 SR이 48kHz가 아닌 경우가 있음.
WASAPI Shared 모드에서는 Windows 오디오 엔진이 리샘플링을 수행하지만,
NR은 실제 오디오 데이터의 SR에 의존함.

가능한 접근:
1. 입력 디바이스 고유 SR 감지 → 48kHz 강제 설정 (사용자에게 권장)
2. 내부 리샘플링으로 SR 차이를 투명하게 처리 (위의 리샘플링 설계)
3. AudioSettings UI에서 입력 디바이스 SR이 48kHz가 아닌 경우 경고 표시

접근 3이 가장 낮은 위험도 + 높은 사용자 가치. 접근 2가 궁극적 해결책.

### 예상 작업량 / Estimated Work

- BuiltinNoiseRemoval.cpp 수정: 3-4시간
- 테스트 추가: 1시간
- 합계: 4-5시간
