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

### 예상 작업량 / Estimated Work

- BuiltinNoiseRemoval.cpp 수정: 3-4시간
- 테스트 추가: 1시간
- 합계: 4-5시간
