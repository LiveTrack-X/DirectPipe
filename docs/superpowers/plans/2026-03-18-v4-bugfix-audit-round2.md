# v4 Bug Audit Round 2 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix 18 issues found in the 2nd code audit — 3 critical, 8 high, 7 medium.

**Architecture:** Each fix is isolated to 1-3 files. H5 (broadcastToClients) and H6 (audioDeviceAboutToStart thread safety) require design judgment. The rest are mechanical fixes.

**Tech Stack:** C++17, JUCE 7.0.12, Google Test. Build: CMake via VS2022.

**Spec:** `docs/superpowers/specs/2026-03-18-v4-bugfix-audit-round2-design.md`

**Build command:**
```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release
```

**Test command:**
```bash
./build/tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe
./build/bin/Release/directpipe-tests.exe
```

---

## Phase 1: CRITICAL

### Task 1: Move LoadLibraryA out of RT audio callback

**Files:**
- Modify: `host/Source/Audio/AudioEngine.h` (add cached function pointers)
- Modify: `host/Source/Audio/AudioEngine.cpp:904-919` (RT callback) + `~1066` (audioDeviceAboutToStart)

- [ ] **Step 1: Add cached MMCSS function pointers to AudioEngine.h**

In the private section, near `mmcssRegistered_` (around line 319):

```cpp
#if defined(_WIN32)
    // Cached MMCSS function pointers — loaded in audioDeviceAboutToStart (message/device thread),
    // called from RT callback. Avoids LoadLibraryA heap allocation in the RT path.
    using AvSetMmThreadCharFn = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
    using AvSetMmThreadPrioFn = BOOL(WINAPI*)(HANDLE, int);
    AvSetMmThreadCharFn avSetMmThreadCharacteristics_ = nullptr;
    AvSetMmThreadPrioFn avSetMmThreadPriority_ = nullptr;
#endif
```

- [ ] **Step 2: Load DLL in audioDeviceAboutToStart instead of RT callback**

In `AudioEngine.cpp`, in `audioDeviceAboutToStart()`, after `mmcssRegistered_.store(false, ...)` (line 1066), add:

```cpp
#if defined(_WIN32)
    // Pre-load avrt.dll on device thread (safe for heap alloc) — cache function pointers
    // so the RT callback can call them without LoadLibraryA.
    if (!avSetMmThreadCharacteristics_) {
        if (auto* avrt = LoadLibraryA("avrt.dll")) {
            avSetMmThreadCharacteristics_ = reinterpret_cast<AvSetMmThreadCharFn>(
                GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW"));
            avSetMmThreadPriority_ = reinterpret_cast<AvSetMmThreadPrioFn>(
                GetProcAddress(avrt, "AvSetMmThreadPriority"));
        }
    }
#endif
```

- [ ] **Step 3: Simplify the RT callback to use cached pointers**

Replace lines 903-920 in `audioDeviceIOCallbackWithContext`:

```cpp
#if defined(_WIN32)
    if (!mmcssRegistered_.load(std::memory_order_relaxed)) {
        mmcssRegistered_.store(true, std::memory_order_relaxed);
        if (avSetMmThreadCharacteristics_ && avSetMmThreadPriority_) {
            DWORD taskIndex = 0;
            HANDLE task = avSetMmThreadCharacteristics_(L"Pro Audio", &taskIndex);
            if (task)
                avSetMmThreadPriority_(task, 2);  // AVRT_PRIORITY_HIGH
        }
    }
#endif
```

- [ ] **Step 4: Build and test**

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release
./build/tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe
```

- [ ] **Step 5: Commit**

```bash
git add host/Source/Audio/AudioEngine.h host/Source/Audio/AudioEngine.cpp
git commit -m "fix(audio): move LoadLibraryA out of RT callback into audioDeviceAboutToStart"
```

---

### Task 2: Fix MidiHandler LearnTimeout use-after-free

**Files:**
- Modify: `host/Source/Control/MidiHandler.cpp:136-143`

- [ ] **Step 1: Rewrite timerCallback to avoid self-destruction**

Replace lines 136-143:

```cpp
// OLD:
    struct LearnTimeout : juce::Timer {
        MidiHandler& handler;
        LearnTimeout(MidiHandler& h) : handler(h) {}
        void timerCallback() override {
            juce::Logger::writeToLog("[MIDI] Learn timed out after 30s");
            handler.stopLearn();
            stopTimer();
        }
    };

// NEW:
    struct LearnTimeout : juce::Timer {
        MidiHandler& handler;
        LearnTimeout(MidiHandler& h) : handler(h) {}
        void timerCallback() override {
            stopTimer();  // Stop BEFORE any cleanup
            Log::info("MIDI", "Learn timed out after 30s");
            // Clear learn state directly — do NOT call stopLearn() which would
            // call learnTimer_.reset() and destroy us while we're on the call stack.
            handler.learning_.store(false, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lock(handler.bindingsMutex_);
                handler.learnCallback_ = nullptr;
            }
            // Schedule our own cleanup after this callback returns
            juce::MessageManager::callAsync([&h = handler]() {
                h.learnTimer_.reset();
            });
        }
    };
```

Make sure `#include "Log.h"` is present at the top.

- [ ] **Step 2: Build and test**

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release
./build/tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe --gtest_filter="*Midi*"
```

- [ ] **Step 3: Commit**

```bash
git add host/Source/Control/MidiHandler.cpp
git commit -m "fix(midi): prevent LearnTimeout use-after-free in timerCallback"
```

---

### Task 3: Add loadingSlot_ guard to Reset Auto

**Files:**
- Modify: `host/Source/MainComponent.cpp:795-825`

- [ ] **Step 1: Wrap the Reset Auto handler with loadingSlot_ guard**

Replace lines 795-825:

```cpp
                if (menuResult == 1) {
                    if (loadingSlot_) return;  // Prevent re-entry during async load
                    loadingSlot_ = true;

                    auto& chain = audioEngine_.getVSTChain();

                    while (chain.getPluginCount() > 0) {
                        if (!chain.removePlugin(0)) {
                            showNotification("Auto reset: failed to remove plugin", NotificationLevel::Error);
                            loadingSlot_ = false;
                            return;
                        }
                    }

                    auto result = chain.addAutoProcessors();
                    if (!result.success) {
                        showNotification("Auto reset failed: " + result.message, NotificationLevel::Error);
                        partialLoad_ = true;  // Prevent auto-save of incomplete chain
                        loadingSlot_ = false;
                        return;
                    }

                    int autoIdx = PresetSlotBar::kAutoSlotIndex;
                    if (presetManager_) {
                        presetManager_->setActiveSlot(autoIdx);
                        presetManager_->saveSlot(autoIdx);
                    }

                    loadingSlot_ = false;

                    if (pluginChainEditor_)
                        pluginChainEditor_->refreshList();
                    updateAutoButtonVisual();
                    markSettingsDirty();
                }
```

- [ ] **Step 2: Build and test**

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release
./build/tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe
```

- [ ] **Step 3: Commit**

```bash
git add host/Source/MainComponent.cpp
git commit -m "fix(preset): add loadingSlot_ guard to Reset Auto, set partialLoad_ on failure"
```

---

## Phase 2: HIGH

### Task 4: Fix audioDeviceStopped + Auto partialLoad_ + pendingSlot_ (3 small fixes)

**Files:**
- Modify: `host/Source/Audio/AudioEngine.cpp:1253-1273` (H1)
- Modify: `host/Source/MainComponent.cpp:184-190` (H2)
- Modify: `host/Source/UI/PresetSlotBar.h` + `host/Source/MainComponent.cpp` (H3)

- [ ] **Step 1: H1 — Add recorder stop to audioDeviceStopped**

In `AudioEngine.cpp`, after line 1263 (`sharedMemWriter_.shutdown()`), add:

```cpp
    // Stop recording to prevent WAV corruption at wrong sample rate
    if (recorder_.isRecording())
        recorder_.stopRecording();
```

- [ ] **Step 2: H2 — Set partialLoad_ on Auto first-click failure**

In `MainComponent.cpp`, around line 186-190, change the failure path:

```cpp
// OLD:
            if (!result.success) {
                showNotification("Auto setup failed: " + result.message, NotificationLevel::Error);
                loadingSlot_ = false;
                return;
            }

// NEW:
            if (!result.success) {
                showNotification("Auto setup failed: " + result.message, NotificationLevel::Error);
                partialLoad_ = true;  // Prevent auto-save of incomplete chain
                loadingSlot_ = false;
                return;
            }
```

- [ ] **Step 3: H3 — Add resetPendingSlot to PresetSlotBar**

In `host/Source/UI/PresetSlotBar.h`, add a public method:

```cpp
    void resetPendingSlot() { pendingSlot_ = -1; }
```

In `MainComponent.cpp`, find `onPresetsCleared` and `onResetSettings` callbacks. At the start of each, add:

```cpp
    if (presetSlotBar_) presetSlotBar_->resetPendingSlot();
```

- [ ] **Step 4: Build and test**

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release
./build/tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe
```

- [ ] **Step 5: Commit**

```bash
git add host/Source/Audio/AudioEngine.cpp host/Source/MainComponent.cpp host/Source/UI/PresetSlotBar.h
git commit -m "fix: recorder stop on device loss, Auto partialLoad, pendingSlot reset"
```

---

### Task 5: Fix quickStateHash + broadcastToClients + Receiver consumer_active

**Files:**
- Modify: `host/Source/Control/StateBroadcaster.cpp:77-83` (H4)
- Modify: `host/Source/Control/WebSocketServer.cpp:521-543` (H5)
- Modify: `plugins/receiver/Source/PluginProcessor.cpp:194-204` (H7)

- [ ] **Step 1: H4 — Add plugin name to quickStateHash**

In `StateBroadcaster.cpp`, after line 83 (the existing `p.type` hash loop), add:

```cpp
    for (const auto& p : s.plugins)
        h = h * 31u + static_cast<uint32_t>(std::hash<std::string>{}(p.name));
```

- [ ] **Step 2: H5 — Reduce lock scope in broadcastToClients**

Read `WebSocketServer.cpp` around lines 511-550. Refactor `broadcastToClients` to:
1. Under `clientsMutex_`: sweep dead connections + copy live client raw pointers to a local vector
2. Release `clientsMutex_`
3. Iterate the snapshot — per-client `sendMutex` for `sendFrame`
4. Join dead connection threads outside the lock (already done)

Key constraint: the `clients_` vector owns the `unique_ptr<ClientConnection>` objects. The raw pointers in the snapshot are valid as long as the connections remain in `clients_`. Since removal only happens in the sweep (step 1) and in `stop()`, and `stop()` cannot run concurrently with `broadcastToClients` (same thread or serialized by design), this is safe.

```cpp
void WebSocketServer::broadcastToClients(const std::string& message)
{
    std::vector<ClientConnection*> liveSnapshot;
    std::vector<std::unique_ptr<ClientConnection>> deadConns;

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        // Sweep dead connections
        for (auto it = clients_.begin(); it != clients_.end(); ) {
            if (!(*it)->socket || !(*it)->socket->isConnected()) {
                deadConns.push_back(std::move(*it));
                it = clients_.erase(it);
            } else {
                liveSnapshot.push_back(it->get());
                ++it;
            }
        }
    }
    // clientsMutex_ released — socket writes cannot block other operations

    for (auto* conn : liveSnapshot) {
        std::lock_guard<std::mutex> sl(conn->sendMutex);
        if (!sendFrame(conn->socket.get(), message))
            conn->socket->close();
    }

    // Join dead connection threads outside lock
    for (auto& dead : deadConns) {
        if (dead->thread.joinable())
            dead->thread.join();
    }
}
```

- [ ] **Step 3: H7 — Add detach before close in Receiver tryConnect**

Read `plugins/receiver/Source/PluginProcessor.cpp`. Find the `tryConnect()` function. In the path where `producer_active` is false and connection is abandoned, add `ringBuffer_.detach()` before `sharedMemory_.close()`:

```cpp
    if (!header->producer_active.load(std::memory_order_acquire)) {
        ringBuffer_.detach();   // Clear consumer_active flag before closing
        sharedMemory_.close();
        return;
    }
```

- [ ] **Step 4: Build and test**

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release
./build/tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe
./build/bin/Release/directpipe-tests.exe
```

- [ ] **Step 5: Commit**

```bash
git add host/Source/Control/StateBroadcaster.cpp host/Source/Control/WebSocketServer.cpp plugins/receiver/Source/PluginProcessor.cpp
git commit -m "fix: state hash plugin name, broadcast lock scope, receiver consumer_active leak"
```

---

### Task 6: Fix audioDeviceAboutToStart thread safety + UpdateChecker batch script

**Files:**
- Modify: `host/Source/Audio/AudioEngine.cpp:1138-1157` (H6)
- Modify: `host/Source/UI/UpdateChecker.cpp:402-421` (H8)

- [ ] **Step 1: H6 — Wrap message-thread-only variable writes in callAsync**

In `audioDeviceAboutToStart`, the `else` branch (line 1138-1159) writes `desiredInputDevice_`, `desiredOutputDevice_`, `reconnectMissCount_` etc. Wrap these in `callAsync`:

```cpp
        } else {
            deviceLost_.store(false, std::memory_order_relaxed);
            inputDeviceLost_.store(false, std::memory_order_relaxed);
            // Atomics above are fine from any thread. Non-atomic variables
            // must be written on the message thread to prevent data races.
            auto aliveFlag = alive_;
            auto inputName = setup.inputDeviceName;
            auto outputName = setup.outputDeviceName;
            auto sr = setup.sampleRate;
            auto bs = setup.bufferSize;
            bool srbsSet = desiredSRBSSet_;
            bool wasAutoMuted = outputAutoMuted_.load(std::memory_order_relaxed);
            if (wasAutoMuted) {
                outputAutoMuted_.store(false, std::memory_order_relaxed);
                if (!outputNone_.load(std::memory_order_relaxed))
                    outputMuted_.store(false, std::memory_order_relaxed);
            }
            juce::MessageManager::callAsync([this, aliveFlag, inputName, outputName, sr, bs, srbsSet] {
                if (!aliveFlag->load()) return;
                reconnectMissCount_ = 0;
                if (inputName.isNotEmpty())
                    desiredInputDevice_ = inputName;
                if (outputName.isNotEmpty())
                    desiredOutputDevice_ = outputName;
                if (!srbsSet) {
                    desiredSampleRate_ = sr;
                    desiredBufferSize_ = bs;
                }
            });
        }
```

Note: `outputAutoMuted_`, `outputMuted_`, `deviceLost_`, `inputDeviceLost_` are already `std::atomic` — safe to write from any thread.

- [ ] **Step 2: H8 — Escape single quotes in UpdateChecker batch script**

Read `UpdateChecker.cpp` around lines 400-425 to find the PowerShell Expand-Archive command. Add a helper or inline-escape single quotes in paths:

```cpp
// Before constructing the PowerShell command:
auto escapePSPath = [](const juce::String& path) -> juce::String {
    return path.replace("'", "''");
};
// Use escapePSPath(zipPath) and escapePSPath(extractDir) in the command string
```

- [ ] **Step 3: Build and test**

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release
./build/tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe
```

- [ ] **Step 4: Commit**

```bash
git add host/Source/Audio/AudioEngine.cpp host/Source/UI/UpdateChecker.cpp
git commit -m "fix: audioDeviceAboutToStart thread safety, UpdateChecker path escaping"
```

---

## Phase 3: MEDIUM

### Task 7: Six small medium-severity fixes (M1-M5, M7)

**Files:**
- Modify: `host/Source/Audio/AudioEngine.cpp:937-939` (M1)
- Modify: `host/Source/UI/PresetManager.cpp:378-388` (M2)
- Modify: `host/Source/UI/PluginChainEditor.cpp:66-77` (M3)
- Modify: `host/Source/Control/WebSocketServer.cpp:245` (M4)
- Modify: `host/Source/UI/SettingsExporter.cpp:95,180` (M5)
- Modify: `host/Source/Platform/macOS/MacAutoStart.cpp:74` + `Linux:69` (M7)

- [ ] **Step 1: M1 — Add outputChannelData null guard**

In `AudioEngine.cpp`, muted fast-path (~line 937) and main output write loop, add:

```cpp
for (int ch = 0; ch < numOutputChannels; ++ch)
    if (outputChannelData[ch])  // ASIO legacy drivers may pass nullptr for inactive channels
        std::memset(outputChannelData[ch], 0, ...);
```

Apply the same `if (outputChannelData[ch])` guard to the main output write loop.

- [ ] **Step 2: M2 — Use loadFileWithBackupFallback in parseSlotFile**

In `PresetManager.cpp`, line 382:

```cpp
// OLD:
    auto json = file.loadFileAsString();
// NEW:
    auto json = loadFileWithBackupFallback(file);
```

- [ ] **Step 3: M3 — Improve plugin removal identification**

In `PluginChainEditor.cpp`, the remove confirmation callback: capture both `rowIndex_` and `pluginName` at dialog-open time. In the callback, first try the captured index (check if name matches), then fall back to name scan.

- [ ] **Step 4: M4 — Send RFC 6455 close frame for >1MB frames**

In `WebSocketServer.cpp`, before the >1MB rejection at line 245:

```cpp
if (payloadLen > 1024 * 1024) {
    // RFC 6455 §7.4.1: 1009 = Message Too Big
    uint8_t closePayload[2] = { 0x03, 0xF1 };  // 1009 big-endian
    sendFrame(client, std::string(reinterpret_cast<char*>(closePayload), 2), 0x8);
    opcodeOut = 0xFF;
    return {};
}
```

- [ ] **Step 5: M5 — Add platform check inside import functions**

In `SettingsExporter.cpp`, at the top of both `importAll` and `importFullBackup`:

```cpp
    auto backupPlatform = getBackupPlatform(parsed);
    if (!isPlatformCompatible(backupPlatform)) {
        Log::warn("APP", "Cross-platform backup restore blocked: " + backupPlatform);
        return false;  // or return appropriate error
    }
```

- [ ] **Step 6: M7 — Use atomicWriteFile in macOS/Linux AutoStart**

In `MacAutoStart.cpp` and `LinuxAutoStart.cpp`, replace `replaceWithText(...)` with `atomicWriteFile(file, content)`. Add `#include "../../Util/AtomicFileIO.h"` if needed.

- [ ] **Step 7: Build and test all**

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release
./build/tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe
./build/bin/Release/directpipe-tests.exe
```

- [ ] **Step 8: Commit each fix separately or as a batch**

```bash
git add -A
git commit -m "fix: medium-severity batch — null guard, backup fallback, plugin removal, WS close frame, import platform check, atomic autostart write"
```

---

### Task 8: Document M6 (pushNotification juce::String) + update CHANGELOG

**Files:**
- Modify: `host/Source/Audio/AudioEngine.cpp:1447` (comment only)
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Add documentation comment to pushNotification**

```cpp
// NOTE: notifQueue_[slot] assignment copies a juce::String (heap allocation).
// This is acceptable because pushNotification is only called from the device
// error thread (audioDeviceError) and message thread — never from the RT
// audio callback. If a future caller needs to push from the RT callback,
// switch to a fixed-size char array or pre-allocated string pool.
```

- [ ] **Step 2: Update CHANGELOG.md with round 2 fixes**

Add entries under the existing `[4.0.1]` section for all fixes from this round.

- [ ] **Step 3: Commit**

```bash
git add host/Source/Audio/AudioEngine.cpp CHANGELOG.md
git commit -m "docs: document pushNotification thread safety, update changelog with round 2 fixes"
```
