# Guarded JUCE build extensions

DirectPipe pins upstream [JUCE 7.0.12](https://github.com/juce-framework/JUCE/tree/7.0.12).
`DirectPipeJuceEndpointIdentity.cmake` applies a small extension to the fetched
build tree, retaining upstream source, notices and licensing. All three
normalized source hashes and resulting hashes are checked before any file is written.
An upstream change fails configuration and requires an explicit patch review.

## Exact Windows capture endpoint identity

The base `AudioIODevice::getWindowsInputEndpointId()` returns empty. Only WASAPI
overrides it, returning the existing `inputDeviceId` when an input object exists.
The query performs no COM calls, enumeration or device changes. Main/UI only
pass the ID of an actively playing, non-lost device with an active input mask.
This identifies the device that JUCE opened without inferring it from display
names, default endpoints, or potentially incomplete audio-session enumeration.
The level-control backend independently checks the exact ID, capture direction
and active state before binding and before writing.

All native targets rebuild with the same header. The extension changes an
internal C++ interface; Receiver IPC, plugin ABI and saved settings are unchanged.
ASIO and other backends retain the empty default and do not support mic hold.

## macOS host-helper deployment target

The third guarded file is `extras/Build/juceaide/CMakeLists.txt`. It backports
upstream JUCE [5023fc6](https://github.com/juce-framework/JUCE/commit/5023fc69d5680f4d8c1cd9c9140956fa99dd8d9c)
and its [f95a5d4 cross-compilation correction](https://github.com/juce-framework/JUCE/commit/f95a5d433d83b5f1aa156833519f1ae943d43f88).
The nested host-tool configure receives the selected
`CMAKE_OSX_DEPLOYMENT_TARGET` when set and not cross-compiling. Existing compiler
flags remain intact. An explicit target overrides a stale helper cache too.

JUCE 7.0.12 omitted this argument. On a macOS 15 runner, its nested `juceaide`
build therefore defaulted to the host minimum and failed at
`CGWindowListCreateImage`, despite the app's explicit 10.15 minimum. Forwarding
the existing minimum preserves universal arm64/x86_64 application builds and
the configured deployment target. It adds no framework, removes no screenshot
behavior and suppresses no unavailable-API diagnostics. Actual macOS CI must
still build/test the candidate and verify both outer and helper caches.

`PATCH_COMMAND` applies all extensions before JUCE configures its helper on a
fresh fetch. Existing populated trees and explicit source overrides are guarded
before `FetchContent_MakeAvailable`; the call afterward verifies the final tree
idempotently. The bootstrap's normalized SHA-256 changes from
`70a92317dd9b5bb283f0d1ba0e3c7f603ccd97da322ee69d9c473ad03b690912` to
`5c9549ea2698b671551498af72e50b4772ac21bccf9183e7feb56879d2a2d495`.

## Mechanical verification

CTest `JuceEndpointIdentityGuard` exercises clean application, idempotence,
mixed original/patched inputs, CRLF checkouts, and rejection without partial
writes when either the second audio input or third bootstrap input is unknown.
It also executes the inserted argument block for custom macOS minimums,
cross-compilation and an unset target. It obtains pristine fixtures from the pinned fetched
Git checkout, independently of the patched working tree. Runtime behavior is
covered by fake-device/controller tests; these do not write a real mic level.
