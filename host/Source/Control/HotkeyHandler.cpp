// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
//
// This file is part of DirectPipe.
//
// DirectPipe is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// DirectPipe is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with DirectPipe. If not, see <https://www.gnu.org/licenses/>.

/**
 * @file HotkeyHandler.cpp
 * @brief Global hotkey handler implementation
 */

#include "HotkeyHandler.h"
#include <algorithm>

namespace directpipe {

HotkeyHandler::HotkeyHandler(ActionDispatcher& dispatcher)
    : dispatcher_(dispatcher)
{
}

HotkeyHandler::~HotkeyHandler()
{
    shutdown();
}

#ifdef _WIN32
// ═══════════════════════════════════════════════════════════════
// Windows Implementation
// ═══════════════════════════════════════════════════════════════

static HotkeyHandler* s_hotkeyInstance = nullptr;

LRESULT CALLBACK HotkeyHandler::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_HOTKEY && s_hotkeyInstance) {
        s_hotkeyInstance->processHotkeyMessage(static_cast<int>(wParam));
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void HotkeyHandler::initialize()
{
    if (initialized_) return;

    s_hotkeyInstance = this;

    // Create a hidden message-only window
    WNDCLASSA wc = {};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "DirectPipeHotkeyWnd";
    RegisterClassA(&wc);

    messageWindow_ = CreateWindowExA(
        0, "DirectPipeHotkeyWnd", "", 0,
        0, 0, 0, 0,
        HWND_MESSAGE,  // Message-only window
        nullptr, wc.hInstance, nullptr);

    initialized_ = (messageWindow_ != nullptr);

    // Start polling for messages
    if (initialized_) {
        startTimer(16);  // ~60Hz
    }
}

void HotkeyHandler::shutdown()
{
    stopTimer();
    unregisterAll();

    if (messageWindow_) {
        DestroyWindow(messageWindow_);
        messageWindow_ = nullptr;
    }

    UnregisterClassA("DirectPipeHotkeyWnd", GetModuleHandle(nullptr));
    s_hotkeyInstance = nullptr;
    initialized_ = false;
}

bool HotkeyHandler::registerHotkey(uint32_t modifiers, uint32_t virtualKey,
                                    const ActionEvent& action, const std::string& displayName)
{
    if (!initialized_) return false;

    int id = nextId_++;
    UINT winMods = 0;
    if (modifiers & HK_ALT)   winMods |= MOD_ALT;
    if (modifiers & HK_CTRL)  winMods |= MOD_CONTROL;
    if (modifiers & HK_SHIFT) winMods |= MOD_SHIFT;
    if (modifiers & HK_WIN)   winMods |= MOD_WIN;
    winMods |= MOD_NOREPEAT;

    BOOL result = RegisterHotKey(messageWindow_, id, winMods, virtualKey);
    if (!result) {
        juce::Logger::writeToLog("[HOTKEY] Failed to register: " +
                                 juce::String(displayName.c_str()));
        return false;
    }

    HotkeyBinding binding;
    binding.id = id;
    binding.modifiers = modifiers;
    binding.virtualKey = virtualKey;
    binding.action = action;
    binding.displayName = displayName;
    binding.registered = true;
    bindings_.push_back(binding);
    juce::Logger::writeToLog("[HOTKEY] Registered: " + juce::String(displayName.c_str()));

    return true;
}

void HotkeyHandler::unregisterHotkey(int id)
{
    if (messageWindow_) {
        UnregisterHotKey(messageWindow_, id);
    }
    bindings_.erase(
        std::remove_if(bindings_.begin(), bindings_.end(),
                       [id](const HotkeyBinding& b) { return b.id == id; }),
        bindings_.end());
}

bool HotkeyHandler::updateHotkey(int id, uint32_t newModifiers, uint32_t newVirtualKey, const std::string& newDisplayName)
{
    auto it = std::find_if(bindings_.begin(), bindings_.end(),
                           [id](const HotkeyBinding& b) { return b.id == id; });
    if (it == bindings_.end()) return false;

    // Unregister old key
    if (messageWindow_)
        UnregisterHotKey(messageWindow_, id);

    // Register new key with same ID
    UINT winMods = 0;
    if (newModifiers & HK_ALT)   winMods |= MOD_ALT;
    if (newModifiers & HK_CTRL)  winMods |= MOD_CONTROL;
    if (newModifiers & HK_SHIFT) winMods |= MOD_SHIFT;
    if (newModifiers & HK_WIN)   winMods |= MOD_WIN;
    winMods |= MOD_NOREPEAT;

    BOOL result = RegisterHotKey(messageWindow_, id, winMods, newVirtualKey);
    if (!result) {
        juce::Logger::writeToLog("[HOTKEY] Failed to re-register: " + juce::String(newDisplayName.c_str()));
        bindings_.erase(it);
        return false;
    }

    it->modifiers = newModifiers;
    it->virtualKey = newVirtualKey;
    it->displayName = newDisplayName;
    it->registered = true;
    return true;
}

void HotkeyHandler::unregisterAll()
{
    for (auto& binding : bindings_) {
        if (binding.registered && messageWindow_) {
            UnregisterHotKey(messageWindow_, binding.id);
        }
    }
    bindings_.clear();
}

void HotkeyHandler::timerCallback()
{
    if (!messageWindow_) return;

    // Recording mode: poll keyboard for key combo capture
    if (recording_ && recordCallback_) {
        // Check modifier state
        uint32_t mods = 0;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= HK_CTRL;
        if (GetAsyncKeyState(VK_MENU)    & 0x8000) mods |= HK_ALT;
        if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) mods |= HK_SHIFT;
        if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) mods |= HK_WIN;

        // Scan for a non-modifier key press
        for (uint32_t vk = 0x08; vk <= 0xFE; ++vk) {
            // Skip modifier keys themselves
            if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) continue;
            if (vk == VK_MENU    || vk == VK_LMENU    || vk == VK_RMENU)    continue;
            if (vk == VK_SHIFT   || vk == VK_LSHIFT   || vk == VK_RSHIFT)   continue;
            if (vk == VK_LWIN    || vk == VK_RWIN)    continue;

            // Detect key-down (high bit = currently pressed)
            if (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) {
                // Require at least one modifier (prevent plain key capture like just 'A')
                if (mods == 0) continue;

                auto name = keyToString(mods, vk);
                auto cb = std::move(recordCallback_);
                recording_ = false;
                recordCallback_ = nullptr;
                cb(mods, vk, name);
                return;
            }
        }
        return;  // Still recording, skip WM_HOTKEY processing
    }

    MSG msg;
    while (PeekMessage(&msg, messageWindow_, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

#elif defined(__APPLE__)
// ═══════════════════════════════════════════════════════════════
// macOS Implementation (CGEventTap)
// ═══════════════════════════════════════════════════════════════

#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

// ─── CGKeyCode → Windows-style VK code mapping ──────────────────

static uint32_t cgKeyToVK(int64_t cgKey)
{
    // Letters (macOS CGKeyCode is non-sequential)
    static const struct { int64_t cg; uint32_t vk; } letters[] = {
        {0x00,'A'},{0x0B,'B'},{0x08,'C'},{0x02,'D'},{0x0E,'E'},{0x03,'F'},
        {0x05,'G'},{0x04,'H'},{0x22,'I'},{0x26,'J'},{0x28,'K'},{0x25,'L'},
        {0x2E,'M'},{0x2D,'N'},{0x1F,'O'},{0x23,'P'},{0x0C,'Q'},{0x0F,'R'},
        {0x01,'S'},{0x11,'T'},{0x20,'U'},{0x09,'V'},{0x0D,'W'},{0x07,'X'},
        {0x10,'Y'},{0x06,'Z'},
    };
    for (const auto& m : letters) if (m.cg == cgKey) return m.vk;

    // Numbers
    static const struct { int64_t cg; uint32_t vk; } nums[] = {
        {0x1D,'0'},{0x12,'1'},{0x13,'2'},{0x14,'3'},{0x15,'4'},
        {0x17,'5'},{0x16,'6'},{0x1A,'7'},{0x1C,'8'},{0x19,'9'},
    };
    for (const auto& m : nums) if (m.cg == cgKey) return m.vk;

    // Function keys (F1-F12): VK 0x70-0x7B
    static const struct { int64_t cg; uint32_t vk; } fkeys[] = {
        {0x7A,0x70},{0x78,0x71},{0x63,0x72},{0x76,0x73},{0x60,0x74},
        {0x61,0x75},{0x62,0x76},{0x64,0x77},{0x65,0x78},{0x6D,0x79},
        {0x67,0x7A},{0x6F,0x7B},
    };
    for (const auto& m : fkeys) if (m.cg == cgKey) return m.vk;

    // Arrow keys
    if (cgKey == 0x7E) return 0x26;  // Up
    if (cgKey == 0x7D) return 0x28;  // Down
    if (cgKey == 0x7B) return 0x25;  // Left
    if (cgKey == 0x7C) return 0x27;  // Right

    // Common keys
    if (cgKey == 0x31) return 0x20;  // Space
    if (cgKey == 0x24) return 0x0D;  // Return
    if (cgKey == 0x35) return 0x1B;  // Escape
    if (cgKey == 0x30) return 0x09;  // Tab

    return 0;  // Unknown
}

static uint32_t cgFlagsToMods(CGEventFlags flags)
{
    uint32_t mods = 0;
    if (flags & kCGEventFlagMaskControl)   mods |= HK_CTRL;
    if (flags & kCGEventFlagMaskAlternate) mods |= HK_ALT;
    if (flags & kCGEventFlagMaskShift)     mods |= HK_SHIFT;
    if (flags & kCGEventFlagMaskCommand)   mods |= HK_WIN;
    return mods;
}

// ─── CGEventTap callback ────────────────────────────────────────

static CGEventRef macEventTapCallback(
    CGEventTapProxy /*proxy*/, CGEventType type,
    CGEventRef event, void* refcon)
{
    // Let non-keydown events pass through
    if (type != kCGEventKeyDown)
        return event;

    auto* handler = static_cast<HotkeyHandler*>(refcon);
    if (!handler) return event;

    int64_t cgKeyCode = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
    CGEventFlags cgFlags = CGEventGetFlags(event);

    uint32_t vk = cgKeyToVK(cgKeyCode);
    uint32_t mods = cgFlagsToMods(cgFlags);

    if (vk == 0) return event;  // Unknown key

    if (handler->macHandleKeyDown(mods, vk))
        return nullptr;  // Consumed — don't pass to other apps

    return event;  // Pass through
}

// ─── macHandleKeyDown (called by event tap callback) ────────────

bool HotkeyHandler::macHandleKeyDown(uint32_t mods, uint32_t vk)
{
    // Recording mode: capture key combo
    if (recording_ && recordCallback_) {
        if (mods != 0 && vk != 0) {
            auto name = keyToString(mods, vk);
            auto cb = std::move(recordCallback_);
            recording_ = false;
            recordCallback_ = nullptr;
            cb(mods, vk, name);
            return true;
        }
        return false;
    }

    // Match against registered bindings
    for (const auto& binding : bindings_) {
        if (binding.modifiers == mods && binding.virtualKey == vk) {
            processHotkeyMessage(binding.id);
            return true;
        }
    }
    return false;
}

// ─── Lifecycle ──────────────────────────────────────────────────

void HotkeyHandler::initialize()
{
    if (initialized_) return;

    // Check accessibility permission (required for CGEventTap).
    // AXIsProcessTrustedWithOptions with kAXTrustedCheckOptionPrompt=true
    // automatically shows the system "allow accessibility" dialog on first run.
    {
        const void* keys[]   = { kAXTrustedCheckOptionPrompt };
        const void* values[] = { kCFBooleanTrue };
        CFDictionaryRef opts = CFDictionaryCreate(
            kCFAllocatorDefault, keys, values, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        bool trusted = AXIsProcessTrustedWithOptions(opts);
        CFRelease(opts);

        if (!trusted) {
            juce::Logger::writeToLog(
                "[HOTKEY] Accessibility permission not granted — "
                "global hotkeys disabled until permission is granted in "
                "System Settings > Privacy & Security > Accessibility");
        }
    }

    // Create a global event tap for key-down events
    CGEventMask mask = CGEventMaskBit(kCGEventKeyDown);
    CFMachPortRef tap = CGEventTapCreate(
        kCGSessionEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionDefault,  // Active tap (can consume events)
        mask,
        macEventTapCallback,
        this);

    if (!tap) {
        juce::Logger::writeToLog(
            "[HOTKEY] Failed to create event tap — hotkeys will not work");
        initialized_ = true;  // Allow binding storage even without tap
        return;
    }

    // Add to main run loop (= JUCE message thread on macOS)
    CFRunLoopSourceRef src = CFMachPortCreateRunLoopSource(
        kCFAllocatorDefault, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), src, kCFRunLoopCommonModes);
    CGEventTapEnable(tap, true);

    eventTap_ = tap;
    runLoopSource_ = src;
    initialized_ = true;

    // Timer for re-enabling disabled tap
    startTimer(500);

    juce::Logger::writeToLog("[HOTKEY] macOS event tap initialized");
}

void HotkeyHandler::shutdown()
{
    stopTimer();
    unregisterAll();

    if (eventTap_) {
        auto* tap = static_cast<CFMachPortRef>(eventTap_);
        CGEventTapEnable(tap, false);  // 1. Disable tap first (stops callbacks)

        if (runLoopSource_) {
            auto* src = static_cast<CFRunLoopSourceRef>(runLoopSource_);
            CFRunLoopRemoveSource(CFRunLoopGetMain(), src, kCFRunLoopCommonModes);
            CFRelease(src);            // 2. Release source
            runLoopSource_ = nullptr;
        }

        CFRelease(tap);                // 3. Release port last
        eventTap_ = nullptr;
    }

    initialized_ = false;
}

bool HotkeyHandler::registerHotkey(uint32_t modifiers, uint32_t virtualKey,
                                    const ActionEvent& action, const std::string& displayName)
{
    // macOS: store binding; matching is done in event tap callback
    HotkeyBinding binding;
    binding.id = nextId_++;
    binding.modifiers = modifiers;
    binding.virtualKey = virtualKey;
    binding.action = action;
    binding.displayName = displayName;
    binding.registered = (eventTap_ != nullptr);
    bindings_.push_back(binding);

    if (eventTap_)
        juce::Logger::writeToLog("[HOTKEY] Registered: " + juce::String(displayName.c_str()));

    return true;
}

void HotkeyHandler::unregisterHotkey(int id)
{
    bindings_.erase(
        std::remove_if(bindings_.begin(), bindings_.end(),
                       [id](const HotkeyBinding& b) { return b.id == id; }),
        bindings_.end());
}

bool HotkeyHandler::updateHotkey(int id, uint32_t newModifiers, uint32_t newVirtualKey,
                                  const std::string& newDisplayName)
{
    auto it = std::find_if(bindings_.begin(), bindings_.end(),
                           [id](const HotkeyBinding& b) { return b.id == id; });
    if (it == bindings_.end()) return false;
    it->modifiers = newModifiers;
    it->virtualKey = newVirtualKey;
    it->displayName = newDisplayName;
    return true;
}

void HotkeyHandler::unregisterAll() { bindings_.clear(); }

void HotkeyHandler::timerCallback()
{
    // Re-enable event tap if system disabled it (timeout protection)
    if (eventTap_) {
        auto* tap = static_cast<CFMachPortRef>(eventTap_);
        if (!CGEventTapIsEnabled(tap)) {
            CGEventTapEnable(tap, true);
            juce::Logger::writeToLog("[HOTKEY] Re-enabled event tap");
        }
    }
}

#else
// ═══════════════════════════════════════════════════════════════
// Linux Stub
// ═══════════════════════════════════════════════════════════════

void HotkeyHandler::initialize() { initialized_ = true; }
void HotkeyHandler::shutdown() { initialized_ = false; bindings_.clear(); }

bool HotkeyHandler::registerHotkey(uint32_t modifiers, uint32_t virtualKey,
                                    const ActionEvent& action, const std::string& displayName)
{
    HotkeyBinding binding;
    binding.id = nextId_++;
    binding.modifiers = modifiers;
    binding.virtualKey = virtualKey;
    binding.action = action;
    binding.displayName = displayName;
    binding.registered = false;
    bindings_.push_back(binding);
    return true;
}

void HotkeyHandler::unregisterHotkey(int id)
{
    bindings_.erase(
        std::remove_if(bindings_.begin(), bindings_.end(),
                       [id](const HotkeyBinding& b) { return b.id == id; }),
        bindings_.end());
}

bool HotkeyHandler::updateHotkey(int id, uint32_t newModifiers, uint32_t newVirtualKey, const std::string& newDisplayName)
{
    auto it = std::find_if(bindings_.begin(), bindings_.end(),
                           [id](const HotkeyBinding& b) { return b.id == id; });
    if (it == bindings_.end()) return false;
    it->modifiers = newModifiers;
    it->virtualKey = newVirtualKey;
    it->displayName = newDisplayName;
    return true;
}

void HotkeyHandler::unregisterAll() { bindings_.clear(); }
void HotkeyHandler::timerCallback() {}

#endif

// ═══════════════════════════════════════════════════════════════
// Common Implementation
// ═══════════════════════════════════════════════════════════════

void HotkeyHandler::processHotkeyMessage(int hotkeyId)
{
    for (const auto& binding : bindings_) {
        if (binding.id == hotkeyId) {
            juce::Logger::writeToLog("[HOTKEY] Triggered: " + juce::String(binding.displayName.c_str()));
            dispatcher_.dispatch(binding.action);
            break;
        }
    }
}

void HotkeyHandler::loadFromMappings(const std::vector<HotkeyMapping>& mappings)
{
    unregisterAll();
    for (const auto& m : mappings) {
        registerHotkey(m.modifiers, m.virtualKey, m.action, m.displayName);
    }
}

std::vector<HotkeyMapping> HotkeyHandler::exportMappings() const
{
    std::vector<HotkeyMapping> mappings;
    for (const auto& b : bindings_) {
        HotkeyMapping m;
        m.modifiers = b.modifiers;
        m.virtualKey = b.virtualKey;
        m.action = b.action;
        m.displayName = b.displayName;
        mappings.push_back(m);
    }
    return mappings;
}

void HotkeyHandler::moveBinding(int fromIndex, int toIndex)
{
    int size = static_cast<int>(bindings_.size());
    if (fromIndex < 0 || fromIndex >= size || toIndex < 0 || toIndex >= size) return;
    if (fromIndex == toIndex) return;

    auto binding = std::move(bindings_[static_cast<size_t>(fromIndex)]);
    bindings_.erase(bindings_.begin() + fromIndex);
    bindings_.insert(bindings_.begin() + toIndex, std::move(binding));
}

void HotkeyHandler::startRecording(
    std::function<void(uint32_t, uint32_t, const std::string&)> callback)
{
    recording_ = true;
    recordCallback_ = std::move(callback);
}

void HotkeyHandler::stopRecording()
{
    recording_ = false;
    recordCallback_ = nullptr;
}

std::string HotkeyHandler::keyToString(uint32_t modifiers, uint32_t virtualKey)
{
    std::string result;
#if JUCE_MAC
    // macOS convention: ⌃ Control, ⌥ Option, ⇧ Shift, ⌘ Command
    // HK_WIN maps to Command key (kCGEventFlagMaskCommand)
    if (modifiers & HK_CTRL)  result += "\xe2\x8c\x83";  // ⌃
    if (modifiers & HK_ALT)   result += "\xe2\x8c\xa5";  // ⌥
    if (modifiers & HK_SHIFT) result += "\xe2\x87\xa7";  // ⇧
    if (modifiers & HK_WIN)   result += "\xe2\x8c\x98";  // ⌘
#else
    if (modifiers & HK_CTRL)  result += "Ctrl+";
    if (modifiers & HK_ALT)   result += "Alt+";
    if (modifiers & HK_SHIFT) result += "Shift+";
    if (modifiers & HK_WIN)   result += "Win+";
#endif

    // Common VK codes
    if (virtualKey >= '0' && virtualKey <= '9') {
        result += static_cast<char>(virtualKey);
    } else if (virtualKey >= 'A' && virtualKey <= 'Z') {
        result += static_cast<char>(virtualKey);
    } else if (virtualKey >= 0x70 && virtualKey <= 0x87) {
        result += "F" + std::to_string(virtualKey - 0x70 + 1);
    } else if (virtualKey == 0x26) {
        result += "Up";
    } else if (virtualKey == 0x28) {
        result += "Down";
    } else if (virtualKey == 0x25) {
        result += "Left";
    } else if (virtualKey == 0x27) {
        result += "Right";
    } else {
        result += "0x" + juce::String::toHexString(static_cast<int>(virtualKey)).toStdString();
    }

    return result;
}

} // namespace directpipe
