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
| v4.0.x (latest) | ✅ |
| v3.10.x (legacy) | ⚠️ Critical fixes only / 치명적 버그만 |
| < v3.10 | ❌ |

---

## 보안 설계 / Security Design

### 네트워크 바인딩 / Network Binding

DirectPipe의 모든 네트워크 서비스는 **localhost (127.0.0.1)에만** 바인딩됩니다. 외부 네트워크에서 접근할 수 없습니다.

All network services bind to **localhost (127.0.0.1) only**. No remote access is possible.

| 서비스 / Service | 포트 / Port | 프로토콜 / Protocol | 용도 / Purpose |
|---|---|---|---|
| WebSocket | 8765 | TCP | 실시간 상태 브로드캐스트 + 명령 수신 / Real-time state broadcast + command input |
| HTTP REST API | 8766 | TCP | GET-only REST API (상태 조회, 명령 실행) / Status query, command execution |
| UDP Discovery | 8767 | UDP | Stream Deck 플러그인 자동 감지 / Stream Deck plugin auto-discovery |

**인증 없음 (No Authentication)**: localhost 전용이므로 인증을 요구하지 않습니다. 같은 PC의 모든 프로세스가 접근 가능합니다.

**No authentication**: Since services are localhost-only, no auth is required. Any process on the same machine can access them.

**연결 제한 / Connection Limits**:
- WebSocket: 최대 32개 동시 연결 (초과 시 거부) / Max 32 concurrent connections (excess rejected)
- HTTP: 최대 64개 동시 핸들러 (초과 시 거부) / Max 64 concurrent handlers (excess rejected). HTTP는 Connection: close 방식이므로 각 요청이 수 ms 내에 완료됨 / HTTP uses Connection: close, each request completes in milliseconds

### CORS 정책 / CORS Policy

HTTP API는 `Access-Control-Allow-Origin: *`를 사용합니다. 이는 브라우저 기반 클라이언트(OBS 브라우저 소스, 커스텀 웹 대시보드 등)를 지원하기 위한 설계입니다.

The HTTP API uses `Access-Control-Allow-Origin: *` to support browser-based clients (OBS browser sources, custom web dashboards).

**알려진 제한사항**: 이 설정으로 인해 악의적인 웹사이트가 사용자의 브라우저를 통해 DirectPipe API에 접근할 수 있습니다 (CSRF). localhost 전용이므로 원격 공격은 불가하지만, 로컬 브라우저를 통한 간접 접근은 가능합니다.

**Known limitation**: This allows malicious websites to access the DirectPipe API through the user's browser (CSRF). Remote attacks are impossible (localhost-only), but indirect access via local browser is possible.

> **참고**: CORS preflight 응답의 `Access-Control-Allow-Methods`에 `POST, PUT, DELETE`가 포함되어 있지만, 실제로는 GET과 OPTIONS만 처리됩니다. 향후 최소 권한 원칙에 따라 `GET, OPTIONS`로 제한하는 것을 검토 중입니다.
> **Note**: The CORS preflight response includes `POST, PUT, DELETE` in `Access-Control-Allow-Methods`, but only GET and OPTIONS are actually handled. Restricting to `GET, OPTIONS` per least-privilege principle is under consideration.

### GET-only REST API 설계 근거 / GET-only REST API Design Rationale

모든 HTTP 엔드포인트가 GET으로 동작하는 이유:

Why all HTTP endpoints use GET:

- **curl/브라우저 호환**: `curl http://localhost:8766/api/mute/panic` 한 줄로 즉시 테스트 가능 / One-liner testing with curl or browser address bar
- **OBS 브라우저 소스**: `fetch()` API로 즉시 호출 가능, CORS preflight 불필요 (simple request) / Instant invocation from OBS browser sources
- **AutoHotkey/스크립트**: POST body 구성 없이 URL만으로 제어 가능 / Control via URL only, no POST body construction

REST 의미론상 상태 변경은 POST가 맞지만, localhost 전용 + 단일 사용자 환경에서는 실용성을 우선했습니다. 향후 제품 성숙도가 올라가면 조회=GET, 변경=POST, 로컬 토큰 인증 분리를 검토합니다.

Strictly speaking, state-changing actions should use POST per REST semantics, but for a localhost-only single-user tool, practicality was prioritized. As the product matures, separating GET (query) / POST (mutation) with local token auth is under consideration.

### IPC 공유 메모리 / IPC Shared Memory

호스트 ↔ Receiver VST 간 오디오 전송은 OS 공유 메모리를 사용합니다.

Audio transfer between host and Receiver VST uses OS shared memory.

| 플랫폼 / Platform | 메커니즘 / Mechanism | 권한 / Permissions |
|---|---|---|
| **Windows** | `CreateFileMapping` + `CreateEvent` | 기본 DACL (같은 사용자 접근 가능) / Default DACL (same-user access) |
| **macOS/Linux** | `shm_open` + `sem_open` | `0600` (소유자 전용) / Owner-only |

**알려진 제한사항 (Windows)**: Windows의 기본 DACL은 같은 사용자 계정의 다른 프로세스가 공유 메모리에 접근할 수 있습니다. 악의적 프로세스가 오디오 데이터를 읽거나 주입할 수 있습니다.

**Known limitation (Windows)**: Default DACL allows other processes running as the same user to access shared memory. A malicious process could read or inject audio data.

### 자동 업데이터 / Auto-Updater

자동 업데이터는 GitHub Releases API에서 최신 버전을 확인하고, 바이너리를 다운로드합니다.

The auto-updater checks the GitHub Releases API for new versions and downloads binaries.

**무결성 검증 / Integrity Verification**:
- 릴리스에 `checksums.sha256` 파일이 포함되어 있으면, 다운로드된 파일의 SHA-256 해시를 검증합니다
- `checksums.sha256`가 없는 경우(구 릴리스 호환) 검증을 건너뜁니다
- 최소 파일 크기 검증 (100KB 이상)

- If the release includes a `checksums.sha256` file, the downloaded file's SHA-256 hash is verified
- Verification is skipped if `checksums.sha256` is absent (backward compatibility with older releases)
- Minimum file size check (>100KB)

**플랫폼별 동작 / Platform Behavior**:
- **Windows**: 자동 다운로드 → 배치 스크립트로 교체 → 재시작 / Auto-download → batch script replacement → restart
- **macOS/Linux**: "View on GitHub" 버튼 (수동 다운로드) / Manual download only

### VST 플러그인 보안 / VST Plugin Security

- **Out-of-process 스캔**: VST 플러그인 스캔은 별도 자식 프로세스에서 실행됩니다. 플러그인이 스캔 중 크래시해도 호스트에 영향 없음
- **블랙리스트**: 스캔 중 크래시한 플러그인은 자동으로 블랙리스트에 등록
- **런타임 크래시 보호**: `processBlock` 호출은 Windows에서 `__try/__except` (SEH), 기타 플랫폼에서 `try/catch(...)`로 보호. 크래시 시 체인 자동 바이패스 + 사용자 알림

- **Out-of-process scanning**: Plugin scanning runs in a separate child process. Host is not affected by scanner crashes
- **Blacklisting**: Plugins that crash during scanning are automatically blacklisted
- **Runtime crash protection**: `processBlock` is wrapped in `__try/__except` (SEH) on Windows, `try/catch(...)` on other platforms. On crash: chain auto-bypassed + user notified

### 설정 백업 보안 / Settings Backup Security

- **크로스 OS 보호**: 백업 파일에 플랫폼 정보가 포함되어 있으며, 다른 OS에서 복원 시 차단됩니다
- **파일 무결성**: 모든 설정/프리셋 저장은 `atomicWriteFile()` 패턴 (.tmp → .bak → rename)을 사용하여 전원 중단 시에도 파일이 손상되지 않습니다

- **Cross-OS protection**: Backup files include platform info; restore is blocked on different OS
- **File integrity**: All settings/preset saves use `atomicWriteFile()` pattern (.tmp → .bak → rename) for crash-safe writes
