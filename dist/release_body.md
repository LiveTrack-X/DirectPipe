## What's New in v3.9.11

### 출력 장치 변경 시 입력 끊김 수정 / Fix Input Loss When Changing Output Device
- **Fallback 오탐 수정** — AudioSettings UI에서 출력 장치 변경 시 `intentionalChange_` 플래그가 설정되지 않아 fallback protection이 원래 장치로 강제 복귀시키던 문제 해결 (예: CABLE Input 선택 불가) — Fixed `intentionalChange_` flag not being set when changing output device from AudioSettings UI, causing fallback protection to revert device changes (e.g., unable to select CABLE Input)
- **입력 채널 보존** — 출력 장치 변경 시 JUCE가 `inputChannels` 비트마스크를 리셋하여 입력이 끊기던 문제 해결. `setOutputDevice`/`setInputDevice`에서 채널 비트가 0이면 `channelMode_`에 맞게 자동 복원 — Fixed JUCE resetting `inputChannels` bitmask when output device changes, causing input audio loss. `setOutputDevice`/`setInputDevice` now restore channel bits based on `channelMode_` when cleared
- **ASIO 장치 설정 통합** — `setAsioDevice()` 메서드 추가로 ASIO input/output 동시 설정 시 `intentionalChange_` + `desiredDevice` 추적 보장 — Added `setAsioDevice()` method ensuring proper `intentionalChange_` + desired device tracking for ASIO single-device setup

---

> **출력 장치 변경 버그 수정.** WASAPI 모드에서 출력 장치 전환 시 입력이 끊기거나 장치가 되돌아가던 문제 해결. Receiver VST2와 Stream Deck 플러그인은 버전만 동기화 (3.9.11.0) — 기능 변경 없음, 기존 버전 그대로 사용 가능합니다.
>
> **Output device switching bug fix.** Fixed input audio loss and unwanted device revert when switching output devices in WASAPI mode. Receiver VST2 and Stream Deck plugin version synced to 3.9.11.0 — no functional changes, existing installations do not need to be updated.

---

## Downloads

| 파일 / File | 설명 / Description |
|---|---|
| `DirectPipe-v3.9.11-win64.zip` | DirectPipe.exe + DirectPipe Receiver.dll |
| `com.directpipe.directpipe.streamDeckPlugin` | Stream Deck 플러그인 (v3.9.11.0) |

## Full Changelog
https://github.com/LiveTrack-X/DirectPipe/compare/v3.9.10...v3.9.11
