# DirectPipe — Claude Code 프로젝트 가이드

## 프로젝트 설명
USB 마이크용 VST 호스트 + 초저지연 루프백 시스템.
하드웨어 오디오 인터페이스 없이 VST 이펙트를 적용하고
OBS/Discord에 초저지연(3~12ms)으로 전달하는 올인원 앱.

## 기술 스택
- C++17, JUCE 7+, CMake 3.22+
- Windows WASAPI Exclusive Mode
- OBS Plugin SDK (C)
- SPSC Lock-free Ring Buffer + Windows Shared Memory

## 빌드 방법
```bash
# 전체 빌드
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# 테스트
cd build && ctest --config Release
```

## 아키텍처 핵심
1. **오디오 스레드는 실시간** — malloc, lock, I/O 절대 금지
2. **IPC는 lock-free** — std::atomic + memory_order만 사용
3. **출력 3개 동시** — 공유메모리(OBS), WASAPI(가상마이크), WASAPI(모니터)

## 코딩 규칙
- 오디오 콜백 내: 힙 할당 금지, 뮤텍스 금지, 시스템 콜 최소화
- 모든 public API에 Doxygen 주석
- 에러 시 예외 대신 반환값/optional 사용
- VST 관련: juce::AudioProcessorGraph 기반
- IPC 관련: Windows API (CreateFileMapping, MapViewOfFile, CreateEvent)

## 주요 파일 역할
- `core/` — 공유 라이브러리 (ring buffer, shared memory)
- `host/` — JUCE 앱 (VST 호스트, 오디오 엔진, GUI)
- `obs-plugin/` — OBS 소스 플러그인 (DLL)

## 개발 순서
Phase 0 → 1 → 2 → 3 → 4 → 5 → 6
각 Phase는 이전 Phase에 의존하므로 순서대로 개발.
