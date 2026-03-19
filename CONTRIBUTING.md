# Contributing to DirectPipe / DirectPipe 기여 가이드

DirectPipe에 기여해 주셔서 감사합니다! / Thank you for contributing to DirectPipe!

## 개발 환경 설정 / Development Setup

[빌드 가이드](docs/BUILDING.md) 참조. / See [Build Guide](docs/BUILDING.md).

## 코드 스타일 / Code Style

- C++17, JUCE 7.0.12 conventions
- 4 spaces indentation (no tabs)
- `camelCase` for methods/variables, `PascalCase` for classes
- `memberVariable_` trailing underscore for member variables
- Bilingual comments: Korean first, English second (한국어 → 영어 순서)
- RT audio callback: **no heap alloc, no mutex, no logging** (see `CLAUDE.md` Coding Rules)

## Pull Request 규칙 / PR Guidelines

1. `main` 브랜치에서 feature 브랜치를 생성하세요 / Create a feature branch from `main`
2. 커밋 메시지: `type: description` 형식 (예: `feat:`, `fix:`, `docs:`, `refactor:`, `test:`) / Commit message format: `type: description`
3. 테스트 통과 확인 / Verify tests pass:
   ```bash
   cmake --build build --config Release
   ctest --test-dir build -C Release --output-on-failure
   ```
4. 새 Action 추가 시 `CLAUDE.md` Maintenance Recipes 체크리스트를 따르세요 / When adding new Actions, follow the Maintenance Recipes checklist in `CLAUDE.md`
5. 문서 업데이트도 포함해 주세요 (해당 시) / Include documentation updates when applicable

## 테스트 / Testing

- Google Test 기반 295 테스트 / 295 tests
- `tools/pre-release-test.sh` — 빌드 + 단위 테스트 + API 테스트 / Build + unit tests + API tests
- `tools/pre-release-dashboard.html` — 수동 테스트 대시보드 / Manual test dashboard

## 버그 리포트 / Bug Reports

[GitHub Issues](../../issues)에 버그를 보고해 주세요. 다음 정보를 포함하면 더 빠르게 대응할 수 있습니다:

Please report bugs via [GitHub Issues](../../issues). Include the following for faster resolution:

- OS 및 버전 / OS and version
- 오디오 드라이버 타입 (WASAPI/ASIO/CoreAudio 등) / Audio driver type
- 재현 가능한 단계 / Steps to reproduce
- 로그 파일 (Settings > 로그 경로 확인) / Log file (check log path in Settings)

## 라이선스 / License

기여한 코드는 [GPL-3.0 라이선스](LICENSE)에 따라 배포됩니다. / Contributions are licensed under [GPL-3.0](LICENSE).
