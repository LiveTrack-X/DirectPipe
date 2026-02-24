/**
 * @file HotkeyHandler.h
 * @brief Global keyboard shortcut handler (Windows RegisterHotKey)
 *
 * Registers system-wide hotkeys that work even when DirectPipe
 * is minimized or in the system tray.
 */
#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <JuceHeader.h>
#include "ActionDispatcher.h"
#include "ControlMapping.h"

#include <map>
#include <string>
#include <vector>
#include <functional>

namespace directpipe {

/// Modifier key flags (matches Windows MOD_ constants)
enum HotkeyModifier : uint32_t {
    MOD_NONE  = 0,
    HK_ALT   = 0x0001,
    HK_CTRL  = 0x0002,
    HK_SHIFT = 0x0004,
    HK_WIN   = 0x0008,
};

/// Represents a single hotkey binding
struct HotkeyBinding {
    int id = 0;                 ///< Unique registration ID
    uint32_t modifiers = 0;     ///< Modifier keys (HotkeyModifier flags)
    uint32_t virtualKey = 0;    ///< Virtual key code
    ActionEvent action;         ///< Action to dispatch when triggered
    std::string displayName;    ///< Human-readable description (e.g., "Ctrl+Shift+1")
    bool registered = false;    ///< Whether currently registered with the OS
};

/**
 * @brief Manages global keyboard shortcuts.
 *
 * Uses Windows RegisterHotKey API. Works in background/minimized state.
 * On non-Windows platforms, provides a stub implementation.
 */
class HotkeyHandler : public juce::Timer {
public:
    explicit HotkeyHandler(ActionDispatcher& dispatcher);
    ~HotkeyHandler() override;

    /**
     * @brief Initialize the hotkey handler.
     * Creates a hidden message window for receiving WM_HOTKEY messages.
     */
    void initialize();

    /**
     * @brief Shut down and unregister all hotkeys.
     */
    void shutdown();

    /**
     * @brief Register a new hotkey binding.
     * @param modifiers Modifier key flags.
     * @param virtualKey Virtual key code.
     * @param action Action to dispatch.
     * @param displayName Readable name.
     * @return true if registered successfully.
     */
    bool registerHotkey(uint32_t modifiers, uint32_t virtualKey,
                        const ActionEvent& action, const std::string& displayName);

    /**
     * @brief Unregister a hotkey by ID.
     */
    void unregisterHotkey(int id);

    /**
     * @brief Unregister all hotkeys.
     */
    void unregisterAll();

    /**
     * @brief Get all registered hotkey bindings.
     */
    const std::vector<HotkeyBinding>& getBindings() const { return bindings_; }

    /**
     * @brief Load hotkey bindings from mapping config.
     */
    void loadFromMappings(const std::vector<HotkeyMapping>& mappings);

    /**
     * @brief Export current bindings to mapping format.
     */
    std::vector<HotkeyMapping> exportMappings() const;

    /**
     * @brief Enter "recording" mode — next keypress will be captured.
     * @param callback Called with the captured key combination.
     */
    void startRecording(std::function<void(uint32_t mods, uint32_t vk, const std::string& name)> callback);

    /**
     * @brief Cancel recording mode.
     */
    void stopRecording();

    /**
     * @brief Check if currently in recording mode.
     */
    bool isRecording() const { return recording_; }

    /**
     * @brief Convert modifier+vk to display string (e.g., "Ctrl+Shift+1").
     */
    static std::string keyToString(uint32_t modifiers, uint32_t virtualKey);

private:
    void timerCallback() override;
    void processHotkeyMessage(int hotkeyId);

    ActionDispatcher& dispatcher_;
    std::vector<HotkeyBinding> bindings_;
    int nextId_ = 1;
    bool initialized_ = false;
    bool recording_ = false;
    std::function<void(uint32_t, uint32_t, const std::string&)> recordCallback_;

#ifdef _WIN32
    HWND messageWindow_ = nullptr;
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif
};

} // namespace directpipe
