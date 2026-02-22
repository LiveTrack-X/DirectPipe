/**
 * @file HotkeyHandler.cpp
 * @brief Global hotkey handler implementation
 */

#include "HotkeyHandler.h"

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

LRESULT CALLBACK HotkeyHandler::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM /*lParam*/)
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
        juce::Logger::writeToLog("Failed to register hotkey: " +
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

    MSG msg;
    while (PeekMessage(&msg, messageWindow_, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

#else
// ═══════════════════════════════════════════════════════════════
// Non-Windows Stub
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
    binding.registered = true;
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
    if (modifiers & HK_CTRL)  result += "Ctrl+";
    if (modifiers & HK_ALT)   result += "Alt+";
    if (modifiers & HK_SHIFT) result += "Shift+";
    if (modifiers & HK_WIN)   result += "Win+";

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
