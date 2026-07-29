# Contributing to DirectPipe / DirectPipe 기여 가이드

DirectPipe에 기여해 주셔서 감사합니다! / Thank you for contributing to DirectPipe!

## 개발 환경 설정 / Development Setup

[빌드 가이드](docs/BUILDING.md) 참조. / See [Build Guide](docs/BUILDING.md).

## 코드 스타일 / Code Style

- C++17, JUCE 7.0.12 conventions
- 4 spaces indentation (no tabs)
- `camelCase` for methods/variables, `PascalCase` for classes
- `memberVariable_` trailing underscore for member variables
- RT audio callback: **no heap alloc, no mutex, no logging** (must always be enforced in review)

### 코드 주석 원칙 / Code Comment Principles

- 코드가 이미 보여 주는 동작을 반복하지 말고 **이유, 불변식, 스레드 소유권, 수명, 호환성 제약, 실패 위험**을 설명합니다. / Explain **why, invariants, thread ownership, lifetime, compatibility constraints, and failure modes** instead of restating the code.
- 공개 API 또는 비자명한 내부 API에는 필요할 때 Doxygen으로 전제 조건, 호출 스레드, 소유권, 결과를 기록합니다. / Use Doxygen for public or non-obvious internal APIs when preconditions, calling thread, ownership, or results need clarification.
- `WARNING`은 실제 데이터 손상·교착·RT 위반·호환성 위험이 있는 경우에만 사용합니다. / Reserve `WARNING` for concrete corruption, deadlock, real-time, or compatibility hazards.
- `TODO`는 추적 가능한 패킷 또는 이슈를 포함해야 합니다(예: `TODO(DP-NOISE-RESAMPLE-0001)`). 담당 근거 없는 TODO를 새로 만들지 않습니다. / Every `TODO` must reference a trackable packet or issue; do not add ownerless TODOs.
- 변할 수 있는 테스트 수, 타이밍, 성능 수치를 주석에 고정하지 말고 심볼·테스트·문서를 참조합니다. 수치를 써야 한다면 상수나 검증 근거와 일치시킵니다. / Avoid embedding drift-prone test counts, timings, or performance claims; refer to symbols, tests, or documentation, and keep necessary values tied to verified constants.
- 주석 처리된 코드와 변경 이력 설명은 남기지 않습니다. 삭제된 코드는 Git 이력으로 확인합니다. / Do not keep commented-out code or change-history narration; Git preserves history.
- 사용자 문서는 한국어/영어 병기를 유지합니다. 코드 주석은 간결한 영어 또는 한영 병기를 사용할 수 있으며, 병기할 때는 한국어 다음 영어 순서로 씁니다. / Keep user documentation bilingual. Code comments may be concise English or bilingual; when bilingual, use Korean before English.

## Pull Request 규칙 / PR Guidelines

1. `main` 브랜치에서 feature 브랜치를 생성하세요 / Create a feature branch from `main`
2. 커밋 메시지: `type: description` 형식 (예: `feat:`, `fix:`, `docs:`, `refactor:`, `test:`) / Commit message format: `type: description`
3. 테스트 통과 확인 / Verify tests pass:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ctest --test-dir build -C Release --output-on-failure
   ```
4. Action/API/상태 모델 변경 시 동기화 문서를 함께 갱신하세요 (`README.md`, `docs/USER_GUIDE.md`, `docs/PRODUCT_SPEC.md`, `docs/CONTROL_API.md`, `docs/ReleaseNote.md`, `TESTING.md`) / When changing Action/API/state model behavior, update sync docs (`README.md`, `docs/USER_GUIDE.md`, `docs/PRODUCT_SPEC.md`, `docs/CONTROL_API.md`, `docs/ReleaseNote.md`, `TESTING.md`)
5. 릴리즈/검증 기준은 [Build Guide](docs/BUILDING.md)와 [Testing Guide](TESTING.md)를 따르세요 / Follow the [Build Guide](docs/BUILDING.md) and [Testing Guide](TESTING.md) for release and validation rules
6. 문서 업데이트도 포함해 주세요 (해당 시) / Include documentation updates when applicable

## 테스트 / Testing

- Google Test 기반 현재 `main` **CTest 492개 등록**: core 59 + host 431 + focused endpoint 2. 로컬 v4.2.3 후보 실행은 490개 통과, 환경 의존 2개 skip, 실패 0개이며 상세 내용은 `docs/ReleaseNote.md` 참조 / Current `main` has **492 CTest registrations**: 59 core + 431 host + 2 focused endpoint tests. The local v4.2.3 candidate run passed 490, skipped 2 environment-dependent tests, and failed 0; see `docs/ReleaseNote.md`.
- `tools/pre-release-test.sh` — `--version-only` metadata gate, exact candidate `--api-only`, candidate 종료 후 `--skip-api` Release build/core/host/endpoint/Stream Deck 검증 / `--version-only` metadata gate, exact-candidate `--api-only`, then `--skip-api` Release build/core/host/endpoint/Stream Deck verification after closing the candidate (Windows Git Bash)
- `tools/pre-release-dashboard.html` — 수동 테스트 대시보드 / Manual test dashboard

## 버그 리포트 / Bug Reports

[GitHub Issues](https://github.com/LiveTrack-X/DirectPipe/issues)에 버그를 보고해 주세요. 다음 정보를 포함하면 더 빠르게 대응할 수 있습니다:

Please report bugs via [GitHub Issues](https://github.com/LiveTrack-X/DirectPipe/issues). Include the following for faster resolution:

- OS 및 버전 / OS and version
- 오디오 드라이버 타입 (WASAPI/ASIO/CoreAudio 등) / Audio driver type
- 재현 가능한 단계 / Steps to reproduce
- 로그 파일 (Settings > 로그 경로 확인) / Log file (check log path in Settings)

## 라이선스 / License

기여한 코드는 [GPL-3.0 라이선스](LICENSE)에 따라 배포됩니다. / Contributions are licensed under [GPL-3.0](LICENSE).
