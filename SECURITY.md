# Security Policy / 보안 정책

## 보안 취약점 보고 / Reporting Security Vulnerabilities

DirectPipe에서 보안 취약점을 발견하셨다면, **공개 이슈로 보고하지 마세요**. 악용 가능성이 있으므로 비공개 채널을 사용해 주세요.

If you discover a security vulnerability in DirectPipe, **please do NOT report it as a public issue**. Use private channels to prevent potential exploitation.

- **GitHub Security Advisory**: [Report a vulnerability](../../security/advisories/new)

48시간 이내에 응답하겠습니다. / We will respond within 48 hours.

## 범위 / Scope

다음 영역의 취약점을 보고해 주세요 / Please report vulnerabilities in these areas:

- WebSocket/HTTP 서버 (인증 우회, 명령 주입 등) / WebSocket/HTTP server (auth bypass, command injection)
- IPC 공유 메모리 접근 제어 / IPC shared memory access control
- 자동 업데이터 보안 (다운로드 무결성) / Auto-updater security (download integrity)
- VST 플러그인 로딩 경로 주입 / VST plugin loading path injection

## 지원 버전 / Supported Versions

| Version | Supported |
|---|---|
| v4.0.x (alpha) | ✅ |
| v3.10.x (stable) | ✅ |
| < v3.10 | ❌ |
