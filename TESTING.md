# DirectPipe 테스트 가이드

## 릴리스 전 체크리스트 / Pre-Release Checklist

릴리스 전에 아래 3단계를 순서대로 실행합니다.
Run the following 3 steps in order before each release.

### Step 1: 자동 테스트 스크립트 (~3-5분)

```bash
bash tools/pre-release-test.sh
```

수행 항목:
- Version consistency check (7개 파일)
- Release build
- Google Test unit tests (core + host)
- API integration tests (앱 실행 중일 때)
- Git status check

옵션: `--skip-build`, `--skip-api`, `--api-only`, `--version-only`

### Step 2: GUI 대시보드 + AI 코드 리뷰

**GUI 대시보드** — 브라우저에서 `tools/pre-release-dashboard.html` 열기:
- API 자동 테스트와 수동 체크리스트를 실행/기록 (개수는 대시보드 버전 및 테스트 스크립트 업데이트에 따라 달라질 수 있음) / Run and track API auto tests + manual checklist (counts may change with dashboard/script updates)
- 현재 기준 개수는 대시보드 상단/섹션별 카운터를 우선 확인 / For current counts, trust dashboard counters first
- 결과 JSON 내보내기 (Export Report → Claude에 전달 가능)
- localStorage에 체크 상태 저장

**AI 코드 리뷰** — Claude Code에서:

> "pre-release review"

9개 버그 카테고리별 구조화 분석:
1. Thread safety & lifetime
2. Audio device state & fallback
3. RT audio callback safety
4. Lock ordering & deadlock
5. State persistence
6. Plugin chain async loading
7. UI modal dialog safety
8. Server protocol correctness
9. Stream Deck & version sync

### Step 3: 릴리스 판정

| 결과 | 판정 |
|------|------|
| Step 1 PASS + Dashboard auto PASS + Manual 전체 체크 + AI Critical 0 | 릴리스 가능 |
| Step 1 FAIL (버전 불일치) | 버전 수정 후 재실행 |
| Dashboard auto FAIL | 해당 API 확인 후 수정 |
| AI Critical > 0 | 코드 수정 후 재실행 |

---

## 자동 테스트 (test_api.js)

### 요구사항
- Node.js 18+
- DirectPipe.exe 실행 중 (HTTP API port 8766, WebSocket port 8765)

### 실행
```bash
node test_api.js
```

### 동작 방식
1. 시작 시 현재 상태(볼륨, 슬롯, 뮤트, IPC, 바이패스 등) 스냅샷 저장
2. 섹션별 체크 항목을 순차 실행 (현재 `test_api.js` 기준 약 33개 섹션, 114개 체크)
3. 테스트 완료 또는 에러 시 자동으로 원래 상태 복원 (`try/finally`)

### 테스트 섹션 (33개)
| # | 섹션 | 체크 수 |
|---|------|---------|
| 1 | Volume Control | 9 |
| 2 | Input Gain Adjust | 3 |
| 3 | Global Mute Toggle | 3 |
| 4 | Input Mute Toggle | 2 |
| 5 | Panic Mute Lockout | 8 |
| 6 | Slot Switching | 8 |
| 7 | IPC Toggle | 2 |
| 8 | Master Bypass | 2 |
| 9 | Plugin Bypass | 4 |
| 10 | Recording | 3 |
| 11 | API Error Handling | 9 |
| 12 | Auto-Save | 1 |
| 13 | Device State | 4 |
| 14 | State JSON Completeness | 1 |
| 15 | WebSocket Connection | 2 |
| 16 | Panic State Restore | 3 |
| 17 | Slot Data Integrity | 4 |
| 18 | Preset File Integrity | 2 |
| 19 | Monitor Toggle | 2 |
| 20 | API Info & CORS | 3 |
| 21 | Preset Load | 3 |
| 22 | State Field Types | 10 |
| 23 | Plugin Slot Info | 3 |
| 24 | Multiple Plugin Bypass | 2 |
| 25 | Gain Adjust Clamp | 2 |
| 26 | Recording File | 2 |
| 27 | Slot Switch Timing | 2 |
| 28 | WebSocket Dead Client | 1 |
| 29 | Concurrent API Requests | 2 |
| 30 | API Edge Cases | 6 |
| 31 | Settings File | 1 |
| 32 | Volume Precision | 2 |
| 33 | Panic During Operations | 3 |

> 참고: 위 숫자는 문서 작성 시점 기준입니다. 실제 실행에서는 환경(플러그인 유무, 장치 상태, SKIP 조건, 반복 실행 횟수)에 따라 PASS/SKIP 수치가 달라질 수 있습니다.
>
> Note: The numbers above reflect the document snapshot. Actual PASS/SKIP counts may vary by environment (plugin availability, device state, skip conditions, rerun loops).

### 주의사항
- 플러그인이 로드된 상태(슬롯 A에 플러그인 있음)에서 실행해야 모든 항목이 PASS
- 플러그인 없으면 관련 항목 SKIP 처리
- 앱 시작 직후에는 초기화 시간 필요 — 5초 이상 대기 후 실행
- Ctrl+C로 중단하면 상태 복원 안 됨 — 수동 복원 필요

---

## 수동 테스트 (자동화 불가 항목)

자동 테스트로 커버할 수 없는 GUI, 하드웨어, 외부 연동 항목.

### 앱 시작/종료/트레이
1. DirectPipe.exe 실행 → 정상 시작, 시스템 트레이 아이콘 표시
2. 트레이 아이콘 더블클릭/좌클릭 → 창 복원
3. 창 닫기(X 버튼) → 트레이로 최소화(종료 아님)
4. 트레이 우클릭 → Show/Quit/자동 시작 토글 메뉴 표시
5. Quit 클릭 → 완전 종료
6. 앱 종료 후 재시작 → 마지막 설정(슬롯, 볼륨, 장치 등) 정상 복원
7. 앱 두 번 실행 시도 → 두번째 인스턴스가 첫번째를 포커스하고 종료
8. 자동 시작 토글 → 플랫폼별 반영 확인 (Windows: 레지스트리, macOS: LaunchAgent, Linux: XDG autostart)
9. 트레이 툴팁 마우스 올리기 → 현재 프리셋/플러그인/볼륨 정보 표시

### 오디오 장치
10. Audio 탭에서 드라이버 타입 변경 (Windows Audio → Low Latency → Exclusive → ASIO) → 각각 정상 전환
11. 입력 장치 변경 → 마이크 입력 정상 캡처
12. 출력 장치 변경 → 오디오 정상 출력
13. Sample Rate 변경 (44100/48000/96000) → 정상 전환, 상태바 반영
14. Buffer Size 변경 → 정상 전환, 레이턴시 표시 업데이트
15. 입력/출력 콤보박스 클릭 → 장치 목록 새로고침(click-to-refresh) 확인
16. ASIO 드라이버 선택 시 → 채널 라우팅 옵션 표시, 정상 동작
17. 존재하지 않는 버퍼 사이즈 요청 → 가장 가까운 사이즈로 자동 폴백 + NotificationBar 알림

### 장치 자동 재연결
18. 오디오 장치 사용 중 USB 분리 → NotificationBar에 주황 경고 표시
19. USB 재연결 → 원하는 장치 감지 시 자동 재연결 + 보라색 알림 표시 (다른 장치로 폴백하지 않음)
20. 재연결 후 SR/BS/채널 설정 보존 확인
21. 모니터 장치 분리 → 독립적으로 monitor_lost 감지
22. 모니터 장치 재연결 → 독립적으로 자동 재연결

### VST 플러그인 스캐너
23. 플러그인 스캐너 열기 → 스캔 시작
24. 스캔 완료 후 VST2/VST3 플러그인 목록 표시
25. 검색창에 텍스트 입력 → 실시간 필터링 동작
26. 열 헤더(Name/Vendor/Format) 클릭 → 정렬 동작
27. 크래시하는 플러그인 → 블랙리스트 처리, 다음 스캔에서 스킵

### 플러그인 체인 에디터
28. 스캐너에서 플러그인 선택 → 체인에 추가 → 오디오 처리 반영
29. 플러그인 드래그로 순서 변경 → 처리 순서 변경 확인 (귀로)
30. 플러그인 삭제 버튼 → 체인에서 제거, 에디터 창도 닫힘
31. 플러그인 더블클릭/Edit → 네이티브 에디터 GUI 열기
32. 에디터 창 닫기 → 상태 정상 저장
33. 여러 플러그인 에디터 동시 열기 → 각각 독립 동작

### [Auto] 프리셋 슬롯
- [ ] [Auto] 클릭 → 3개 내장 프로세서 삽입 확인 (Filter + Noise Removal + Auto Gain) / Click [Auto] → verify 3 built-in processors inserted
- [ ] [Auto] 상태에서 각 프로세서 Edit 패널 열기 → 기본값 확인 / Open each processor's Edit panel in Auto state → verify defaults
- [ ] 비-48kHz SR에서 [Auto] → "Noise Removal requires 48kHz" 경고 확인 / [Auto] at non-48kHz → verify NR warning shown
- [ ] [Auto] 우클릭 → Reset to Defaults → 기본 체인 복원 확인 / Right-click [Auto] → Reset to Defaults → verify chain restored
- [ ] 다른 슬롯(A-E) 선택 → Auto 비활성화 확인 / Select slot A-E → verify Auto deactivated

### Safety Limiter
- [ ] Safety Limiter ON/OFF 토글 → 반영 확인 / Toggle Safety Limiter → verify state reflected
- [ ] Safety Limiter Ceiling 변경 (-0.3 → -3.0) → 반영 확인 / Change ceiling → verify applied
- [ ] 클리핑 신호 입력 시 [LIM] 상태바 표시 확인 / Input clipping signal → verify [LIM] indicator in status bar

### 프리셋 슬롯 (Keep-Old-Until-Ready)
34. 슬롯 전환 중 마이크에 소리를 내면서 확인 → old chain 이펙트 유지되다가 new chain으로 전환
35. 무거운 플러그인(6개) 있는 슬롯 전환 → 이전 대비 끊김 감소 체감 확인
36. 슬롯 전환 후 플러그인 내부 상태(파라미터, 프리셋) 정확히 복원되는지 확인
37. 슬롯에 플러그인 추가/삭제 후 자동 저장 → 슬롯 파일 업데이트 확인
38. 플러그인 에디터 창 열린 상태에서 슬롯 전환 → 에디터 정상 닫힘 + 새 슬롯 로드

### 볼륨/뮤트 (UI)
39. 모니터 볼륨 슬라이더 드래그 → 실시간 반영
40. 입력 게인 슬라이더 드래그 → 실시간 반영
41. OUT 버튼 클릭 → Output Mute 토글 (초록→빨강)
42. MON 버튼 클릭 → Monitor Enable 토글
43. VST 버튼 클릭 → Master Bypass 토글
44. PANIC MUTE 클릭 → 전체 음소거, OUT/MON/VST 버튼 잠김(빨강)

### 모니터 출력
45. Output 탭에서 모니터 장치 선택 → 별도 WASAPI 출력 활성화
46. 모니터 활성 시 상태 표시: Active + 레이턴시 ms 표시
47. 메인 ASIO 출력 사용 중에도 모니터(WASAPI) 독립 동작 확인
48. 모니터 장치 SR이 메인과 다를 때 → SampleRateMismatch 상태 표시
49. 모니터 장치 없음 선택 → No device 상태, 레이턴시 0

### 레벨 미터
50. 마이크 입력 시 좌/우 입력 미터 반응
51. 출력 미터가 VST 체인 처리 후 레벨 표시
52. Mute 시 출력 미터 0
53. 볼륨 변경 시 미터 레벨 반영

### 녹음 (UI)
54. REC 버튼 클릭 → 녹음 시작, 타이머 카운트 표시
55. Play 버튼 → 마지막 녹음 파일 재생
56. Open Folder → 녹음 폴더 탐색기 열기
57. 녹음 중 장치 변경 → 녹음 자동 중지

### 단축키
58. Ctrl+Shift+1~9 → 해당 플러그인 바이패스 토글
59. F1~F5 → 슬롯 A~E 전환
60. Ctrl+Shift+O → Output Mute 토글
61. Ctrl+Shift+H → Monitor 토글
62. Ctrl+Shift+I → IPC 토글
63. Controls > Hotkeys 탭에서 단축키 변경 → 반영 확인

### MIDI
64. MIDI 장치 연결 → Controls > MIDI 탭에서 인식
65. MIDI CC/Note 바인딩 추가 → 해당 컨트롤러로 조작 확인
66. MIDI Learn(플러그인 파라미터 매핑) → 3단계 팝업(플러그인→파라미터→CC) 동작
67. Continuous MIDI 바인딩 → 볼륨/파라미터 부드럽게 조절

### Stream Deck
68. DirectPipe 시작 → UDP discovery → Stream Deck 자동 연결
69. Bypass 액션 → 플러그인 바이패스 토글 + 피드백 표시
70. Volume 다이얼 → 모니터/입력 볼륨 조절 + LCD 피드백
71. Preset 액션 → 슬롯 전환 + 피드백 표시
72. Panic 액션 → PANIC MUTE 토글 + 피드백
73. Recording 액션 → 녹음 토글 + REC mm:ss 표시
74. Monitor/IPC 액션 → 각각 토글
75. Stream Deck 소프트웨어 종료 후 재시작 → 자동 재연결

### IPC / Receiver VST
76. IPC 토글 → 활성화 시 공유 메모리 출력 시작
77. OBS에서 Receiver VST 로드 → DirectPipe 오디오 수신
78. Receiver 버퍼 사이즈 변경(5단계) → 반영 확인
79. SR 불일치 시 Receiver에 경고 표시

### 설정 내보내기/가져오기
80. Settings 탭 > Save Settings → .dpbackup 파일 생성
81. Settings 탭 > Load Settings → .dpbackup에서 복원, 모든 설정 반영

### 로그 / 유지보수
82. Settings 탭 → 실시간 로그 표시, 타임스탬프 포함
83. Export Log → .txt 파일 저장
84. Clear Log → 로그 패널 비움
85. Clear Plugin Cache → 스캔 캐시 삭제 + 확인 다이얼로그
86. Clear All Presets → 모든 슬롯 파일 삭제 + 확인 다이얼로그
87. Reset Settings → 설정 초기화 + 확인 다이얼로그

### NotificationBar
88. 오디오 장치 에러 → 빨강 알림 표시 후 3~8초 자동 페이드
89. 장치 재연결 → 보라색 알림 표시
90. 플러그인 로드 실패 → 주황 경고 알림

### UI 일반
91. 4개 탭(Audio/Output/Controls/Settings) 전환 → 각 패널 정상 표시
92. Controls 서브탭(Hotkeys/MIDI/Stream Deck) 전환 → 정상
93. 창 리사이즈 → 레이아웃 정상 유지
94. 한글/CJK 장치 이름 → □□□ 없이 정상 렌더링
95. 상태바: 레이턴시(ms), CPU(%), XRun 카운트 표시
96. XRun 발생 시 빨간 표시, 0이면 기본 색상
97. 버전 라벨 "Created by LiveTrack" 하단 표시
98. 새 버전 존재 시 "NEW vX.Y.Z" 주황 표시 → 클릭 시 업데이트 다이얼로그
