# Windows DPWIM 설치와 업데이트

DirectPipe 4.5.1의 DPWIM은 **별도 VST3 플러그인**입니다. 내장 프로세서는
Filter, Noise Removal, Auto Gain의 3종이며 DPWIM은 Auto에 포함되지 않습니다.
DPWIM을 설치하지 않아도 DirectPipe와 내장 프로세서는 사용할 수 있습니다.

## 설치·업데이트

1. Windows에서 `+ Add Plugin`을 엽니다.
2. **Built-in 바로 아래의 Install DPWIM...**을 클릭합니다.
3. 공식 GitHub 최신 안정 릴리즈를 확인합니다. 미설치 상태면 설치하고,
   DirectPipe가 관리하는 버전보다 새 버전이 있으면 업데이트합니다.
   같은 버전이나 더 최신 버전이면 그대로 유지합니다.
4. 완료 창에서 **Open Plugin List**를 누르면 설치된 버전의 폴더만 별도
   스캐너 프로세스로 검사합니다. 플러그인을 선택해 원하는 체인 위치에 추가하세요.
5. 기존 버전을 교체하려면 현재 설정을 확인한 뒤 새 설치본을 추가·설정하고
   프리셋/슬롯을 저장합니다. 설치 동작만으로 현재 체인이나 Auto는 바뀌지 않습니다.

이 메뉴는 Windows에서만 표시됩니다. 직접 다운로드할 수도 있습니다:
[공식 DPWIM 릴리즈](https://github.com/LiveTrack-X/DPWIM/releases/latest).
2026-11-13 확인 기준 최신 공개 버전은
[v0.3.4](https://github.com/LiveTrack-X/DPWIM/releases/tag/v0.3.4)이며, unsigned
Windows public preview입니다. DirectPipe의 버전과 DPWIM의 버전은 별개입니다.

## 설치 위치와 기존 설치본

설치본은 관리자 권한 없이 다음 사용자 폴더에서 버전별로 관리합니다.

```text
%LOCALAPPDATA%\Programs\Common\VST3\DirectPipe-DPWIM\v<version>-<id>\
```

ZIP의 LICENSE와 README도 보존합니다. 현재 관리 버전은 `installed.json`이
가리키며, 기본 플러그인 스캐너에도 이 버전의 폴더가 포함됩니다.

업데이트는 새 폴더에 설치하므로 사용 중인 DLL/VST3를 덮어쓰지 않습니다.
기존 인스턴스와 프리셋에 저장된 파일 경로는 이전 버전을 계속 가리킵니다.
**새 설치본을 명시적으로 추가·교체해야 새 버전을 사용합니다.** 앱을 재시작하거나
이전 프리셋을 다시 여는 것만으로 경로가 자동 치환되지는 않습니다.
이전 버전은 자동 삭제하지 않습니다.

기존에 직접 설치한 VST2나 다른 폴더의 VST3도 유지합니다. Install DPWIM은
DirectPipe가 관리하는 별도 VST3 복사본을 설치·업데이트하며, 외부 설치본을
검색해 덮어쓰거나 제거하지 않습니다. 다른 DAW에서는 관리 버전의 폴더를 별도
검색 경로로 지정할 수 있습니다.

## 신호와 기본 설정

DPWIM의 Dry Input은 **DPWIM 앞단의 마이크·효과 처리 신호**입니다. 앱 또는
Desktop 소스를 최대 4개 합칠 수 있습니다. 앱 소리에 음성용 RNNoise나 AGC를
적용하지 않으려면 DPWIM을 그 효과들 뒤에 배치하세요.

| 항목 | 공개 DPWIM 0.3.4 동작 |
|---|---|
| Dry Input | 기본 ON, Gain 0 dB. OFF여도 앱 캡처는 계속됩니다. |
| Source 1–4 | 초기 OFF. 앱 프로세스 트리 또는 Desktop 선택. |
| Gain | 소스별 -60~+12 dB. |
| Base Latency | 10~250 ms, 기본 10 ms. 캡처 활성 시 실제 하한은 20 ms이며 블록·싱크·피치에 따라 더 커집니다. |
| Offset | 소스별 ±200 ms. 음수는 다른 활성 경로의 지연도 조정해 공통 타임라인을 맞춥니다. |
| ADV | Transpose ±12 semitones, Fine Pitch ±100 cents. |
| 내부 BYPASS | 앞단 입력 unity 통과, 캡처 소스 제외, DPWIM 보고 지연 0. |
| OUT / SYNC | 플러그인 자체의 지연 보고. 하드웨어 왕복 지연 실측값은 아닙니다. |

ASIO 마이크를 사용하면서 Windows 앱 소리를 합칠 수 있지만, Windows 프로세스
루프백이 동작해야 합니다. Desktop은 DPWIM이 실행되는 호스트 프로세스 트리를
제외합니다. 다른 앱이 DirectPipe 출력을 다시 재생하면 재캡처될 수 있으므로
같은 소리를 Receiver나 다른 경로와 중복 합치지 마세요.
이는 여러 USB 마이크를 묶거나 오인페 L/R에 독립 체인을 배치하는 기능이 아닙니다.

## 실패 시 동작과 검증 범위

공식 안정 릴리즈의 정확한 Windows x64 VST3 asset과 GitHub의 SHA-256 metadata를
확인합니다. 해시 누락·불일치, 경로 이탈·중복·과대 ZIP, 불완전한 bundle,
다른 아키텍처·버전은 거절합니다. 네트워크·권한 실패 시 기존 설치 기록은
유지하고 오류를 표시합니다. 실행 시 자동 다운로드하거나 백그라운드에서
주기적으로 설치하지 않습니다.

설치 기능의 파일·네트워크 오류 처리 검증과, 외부 DPWIM 자체의 앱 캡처·청취
품질은 별개입니다. 이번 DirectPipe 변경은 DPWIM 0.3.4 바이너리를 수정하지
않습니다. 공개 preview의 지원 범위와 알려진 문제는 DPWIM 저장소를 따릅니다.
통합 검토에서 발견한 PID 복원·ADV 창 수명 수정은 보류된 빌트인 작업에만
존재하며 공개 0.3.4에 적용됐다고 주장하지 않습니다. 0.3.4 사용 시 복원한 앱
소스를 다시 선택하고 ADV 창을 닫은 뒤 플러그인을 제거·교체하는 것이 해당
경로를 피하는 방법입니다.

[현재 릴리즈 검증 기록](RELEASE_4_5_1.md).
