# Exact Windows capture endpoint identity

DirectPipe pins upstream [JUCE 7.0.12](https://github.com/juce-framework/JUCE/tree/7.0.12).
`DirectPipeJuceEndpointIdentity.cmake` applies a small extension to the fetched
build tree, retaining upstream source, notices and licensing. Both normalized
source hashes and resulting hashes are checked before either file is written.
An upstream change fails configuration and requires an explicit patch review.

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

CTest `JuceEndpointIdentityGuard` exercises clean application, idempotence,
mixed original/patched inputs, and rejection without partial writes when the
second input is unknown. It obtains pristine fixtures from the pinned fetched
Git checkout, independently of the patched working tree. Runtime behavior is
covered by fake-device/controller tests; these do not write a real mic level.
