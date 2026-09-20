# 4.4.0 문서·주석 정합성 검토 / Documentation and comment audit

> **과거 검사 기록 / Historical checkpoint — 2026-09-20, before release preparation.**
> 아래 상태·개수·로컬 증거 경로는 당시 문서 전용 작업의 기록입니다. 이후 변경과
> 현재 검증 범위는 [4.4.0 변경·검증](MULTI_RECEIVER_4_4_0.md)을 참고하세요.
> / Local artifact paths below are historical references, not downloadable release
> assets or claims about the final source/tag.

2026-09-20, 미공개 4.4.0 후보에 대한 문서 후속 작업입니다. 현재 구현과 테스트를
기준으로 프로젝트 문서·관련 소스 주석을 대조했습니다. 실행 로직, 플러그인 식별자,
설정 형식, 기존 후보 바이너리는 변경하지 않았습니다.

This is a documentation-only follow-up to the verified local candidate. Current
implementation/tests establish behavior; previous release results remain historical.

## 정정한 내용 / Corrections

| 영역 / Area | 확인·정정 / Correction |
| --- | --- |
| 제품·사용 안내 | 단일 Receiver 설명을 새 전송 최대 8개 + 별도 legacy 1개로 구분. 양쪽 모두 4.4.0 필요, 동일 샘플레이트, 공통 처리 음성, 기존 A–E/Auto·OBS 설정 유지 명시 |
| Receiver 상태·조작 | 연결·legacy·producer 확인 대기·한도·비호환 상태와 실제 UI 치수/컨트롤 설명 정정. 편집 창만 닫아서는 연결 슬롯이 해제되지 않음을 명시 |
| API·Stream Deck | IPC 토글은 모든 Receiver의 공통 전송 스위치이며, 한 곳만 끄려면 해당 Receiver의 로컬 Mute를 사용하도록 명시 |
| 전송 계약·주석 | 두 mapping/ABI 구분, claim/ack 순서, 실제 프로세스 종료 확인 후 회수, overflow 후 묵은 음성 폐기, pending 연결 수명, callback lease/drain 설명 보완 |
| 실시간 처리 | 새 전송에는 OS 호출을 추가하지 않으며 legacy event signal은 유지됨을 구분. 전송 drop 합계와 장치 XRun을 구분 |
| 프리셋·오디오 | 무중단·고정 전환 시간 표현 삭제. 상태 복원·graph 변경 시 진입 차단과 callback drain, 중첩 scope와 public boolean suspend, 짧은 무음 가능성을 설명 |
| 버퍼·페이드 | 충분한 callback을 임의로 반감한다는 옛 설명 정정. 큰 callback 분할, mute 중 배출, 마지막 출력 샘플에서 이어지는 transition과 reported latency의 범위 설명 |
| 갱신 | Windows 설치본 검색·경로/버전 미리보기, 사용 앱 직접 종료 후 Retry/Later, Receiver-only 실행 유지, PowerShell bundle 교체·검증·복구와 host-only 경로 구분 |
| 개발·검증 | 4.4.0 후보와 과거 공개 버전의 테스트/플랫폼 근거 구분. 실제 Receiver/child helper 대상, 올바른 집중 빌드 명령, 수동 다중 수신·갱신 검사 추가 |
| 문서 연결 | Quick Start의 존재하지 않는 사용자 가이드 heading 링크 두 곳 수정. 현재 보고서/로컬 문서 router 연결 정리 |

주요 공개 문서는 [README](../README.md), [사용자 가이드](USER_GUIDE.md),
[제품 명세](PRODUCT_SPEC.md), [구조](ARCHITECTURE.md), [빌드](BUILDING.md),
[테스트](../TESTING.md), [빠른 시작](QUICKSTART.md), [플랫폼](PLATFORM_GUIDE.md),
[API](CONTROL_API.md), [예제](API_EXAMPLES.md), [Stream Deck](STREAMDECK_GUIDE.md),
[보안 설계](../SECURITY.md), [로그](LOGRULE.md)입니다.
기존 로컬 개발 가이드와 모듈 README도 대조했으며 기존 추적/비공개 정책은 유지했습니다.

## 검증 / Verification

| 검사 / Check | 결과 / Result |
| --- | --- |
| 프로젝트 Markdown 문서 | 96개 UTF-8·코드 블록 및 표 208개 열 수 검사, 오류 0 |
| 문서 내 로컬 파일·heading 링크 | 149개 검사, 오류 0 |
| 소스/CMake 주석 변경 무결성 | 수정 전 94개 파일과 비교; 46개 파일 주석 수정, 실행 토큰 변경 0 |
| 문자열 보존 | 문자열·char·raw string 및 다중 문자 연산자를 보존해 비교 |
| 버전·릴리즈 메타데이터 | 14개 검사 통과 |
| 텍스트·patch | `check_text_integrity.py`, `git diff --check` 통과 |
| 로컬 제어 문서 | SDAD Doctor 3.2.2 strict 오류 0·경고 0 |
| 과거 공개 릴리즈 | 버전별 release body 19개가 HEAD 내용과 동일 |
| 후보·실제 설치 | 기존 후보 ZIP/Stream Deck/구성 파일과 설치된 본체·Receiver SHA-256 불변 |

검증 기록은 후보 폴더의 `evidence/documentation-audit/`에 보관합니다.
원래 빌드의 소스 해시 기록은 보존하고, 이번 주석 수정 후 해시·토큰 비교는 별도로
기록했습니다. 주석 작업이므로 C++ 전체 빌드/회귀 테스트는 재실행하지 않았습니다.

문서 수정 전 Windows 빌드·CTest 결과(646 통과, 2 건너뜀, 실패 0)와 Stream Deck
22/22는 [원래 후보 검증](MULTI_RECEIVER_4_4_0.md)의 결과입니다. 이번 문서 작업의
검사 결과와 합산하거나 새로 실행한 테스트인 것처럼 표시하지 않습니다.

## 범위와 보존 / Boundaries

- 버전별 릴리즈 기록·이전 후보·날짜가 있는 설계 자료는 당시 근거를 유지했습니다.
  보존된 4.3.1 보고서는 당시 소스 버전임을 명확히 했습니다.
- 제3자 SDK, 의존성, 빌드·생성 파일과 다른 제품 버전의 문서는 재작성하지 않았습니다.
- `SharedMemWriter`의 기존 실패 로그 문구 중 `retained`는 포괄적인 표현입니다.
  실제 fallback 금지 조건은 **Full/Invalid**이며 **Inactive/absent**는 legacy를
  시도할 수 있습니다. 실행 문자열은 이번 주석 작업에서 바꾸지 않고 문서·주석에는
  실제 분기를 기록했습니다. `Protocol.h`의 기존 static-assert 진단 문자열 역시
  유지하며, legacy ABI를 변경하지 않는 설계 원칙은 주석에 명시했습니다.
- 실제 OBS/DAW 동시 수신·장시간 청취·UAC·설치·배포 확인은 여전히 별도입니다.

Historical artifacts and candidate binaries are preserved. Documentation checks
and unchanged-token checks do not add live OBS, device, UAC or release evidence.
