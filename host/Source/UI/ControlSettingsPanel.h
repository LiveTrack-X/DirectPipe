/**
 * @file ControlSettingsPanel.h
 * @brief Tabbed settings panel for Hotkeys, MIDI, and Stream Deck configuration
 *
 * Provides a three-tab interface for managing all external control inputs:
 * hotkey shortcuts, MIDI CC/Note mappings, and WebSocket/HTTP server status
 * for Stream Deck integration.
 */
#pragma once

#include <JuceHeader.h>
#include "../Control/ControlManager.h"

namespace directpipe {

// ═════════════════════════════════════════════════════════════════════════════
// HotkeyTab — Action → shortcut bindings with [Set] recording
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Tab content showing hotkey bindings with inline recording.
 *
 * Displays a scrollable list of action-to-shortcut bindings.
 * Each row has a [Set] button that enters recording mode and captures
 * the next keypress as the new shortcut.
 */
class HotkeyTab : public juce::Component,
                   public juce::Timer {
public:
    /**
     * @brief Construct the Hotkey tab.
     * @param manager Reference to the control manager.
     */
    explicit HotkeyTab(ControlManager& manager);
    ~HotkeyTab() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Rebuild the binding list from the current handler state.
     */
    void refreshBindings();

private:
    void timerCallback() override;

    /** @brief Handle [Set] button click — enter recording mode for a binding. */
    void onSetClicked(int bindingIndex);

    /** @brief Handle [Remove] button click — remove a binding. */
    void onRemoveClicked(int bindingIndex);

    ControlManager& manager_;

    // Header label
    juce::Label headerLabel_{"", "Keyboard Shortcuts"};

    // Scrollable viewport for the binding rows
    juce::Viewport viewport_;
    juce::Component rowContainer_;

    /// One UI row per hotkey binding
    struct BindingRow {
        juce::Label actionLabel;
        juce::Label shortcutLabel;
        juce::TextButton setButton{"Set"};
        juce::TextButton removeButton{"X"};
    };
    juce::OwnedArray<BindingRow> rows_;

    // Status label (shows "Press a key..." during recording)
    juce::Label statusLabel_{"", ""};

    // Index of the binding currently being recorded (-1 = none)
    int recordingIndex_ = -1;

    // Theme colours (dark)
    static constexpr juce::uint32 kBgColour      = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurfaceColour  = 0xFF2A2A40;
    static constexpr juce::uint32 kRowAltColour   = 0xFF252540;
    static constexpr juce::uint32 kAccentColour   = 0xFF6C63FF;
    static constexpr juce::uint32 kTextColour     = 0xFFE0E0E0;
    static constexpr juce::uint32 kDimTextColour  = 0xFF8888AA;
    static constexpr juce::uint32 kWarningColour  = 0xFFFFAA33;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HotkeyTab)
};

// ═════════════════════════════════════════════════════════════════════════════
// MidiTab — MIDI device selector + CC/Note mappings with [Learn]
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Tab content showing MIDI device selection and CC/Note mappings.
 *
 * Top section: MIDI device selector combo box with a [Rescan] button.
 * Bottom section: scrollable list of CC/Note-to-action mappings, each
 * with a [Learn] button that enters MIDI Learn mode.
 */
class MidiTab : public juce::Component,
                public juce::Timer {
public:
    /**
     * @brief Construct the MIDI tab.
     * @param manager Reference to the control manager.
     */
    explicit MidiTab(ControlManager& manager);
    ~MidiTab() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Rebuild the device list and binding list from current state.
     */
    void refreshAll();

private:
    void timerCallback() override;

    /** @brief Refresh the MIDI device combo box. */
    void refreshDeviceList();

    /** @brief Rebuild the mapping rows from the handler. */
    void refreshMappings();

    /** @brief Handle device selection change. */
    void onDeviceSelected();

    /** @brief Handle [Rescan] button. */
    void onRescanClicked();

    /** @brief Handle [Learn] button click. */
    void onLearnClicked(int mappingIndex);

    /** @brief Handle [Remove] button click. */
    void onRemoveClicked(int mappingIndex);

    /**
     * @brief Convert a MIDI binding to a display string (e.g., "CC 7 Ch 1").
     * @param binding The MIDI binding.
     * @return Human-readable representation.
     */
    static juce::String midiBindingToString(const MidiBinding& binding);

    ControlManager& manager_;

    // Device selection
    juce::Label deviceLabel_{"", "MIDI Device:"};
    juce::ComboBox deviceCombo_;
    juce::TextButton rescanButton_{"Rescan"};

    // Header
    juce::Label mappingHeaderLabel_{"", "MIDI Mappings"};

    // Scrollable mapping list
    juce::Viewport viewport_;
    juce::Component rowContainer_;

    /// One UI row per MIDI mapping
    struct MappingRow {
        juce::Label controlLabel;   // e.g., "CC 7 Ch 1"
        juce::Label actionLabel;    // e.g., "ToggleMute"
        juce::TextButton learnButton{"Learn"};
        juce::TextButton removeButton{"X"};
    };
    juce::OwnedArray<MappingRow> rows_;

    // Status label
    juce::Label statusLabel_{"", ""};

    // Index of the mapping currently in learn mode (-1 = none)
    int learningIndex_ = -1;

    // Theme colours (dark)
    static constexpr juce::uint32 kBgColour      = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurfaceColour  = 0xFF2A2A40;
    static constexpr juce::uint32 kRowAltColour   = 0xFF252540;
    static constexpr juce::uint32 kAccentColour   = 0xFF6C63FF;
    static constexpr juce::uint32 kTextColour     = 0xFFE0E0E0;
    static constexpr juce::uint32 kDimTextColour  = 0xFF8888AA;
    static constexpr juce::uint32 kWarningColour  = 0xFFFFAA33;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiTab)
};

// ═════════════════════════════════════════════════════════════════════════════
// StreamDeckTab — WebSocket/HTTP server status display
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Tab content showing Stream Deck server status.
 *
 * Displays:
 * - WebSocket server: port, running/stopped, connected client count
 * - HTTP API server: port, running/stopped
 * - Start/Stop toggle buttons for each server
 */
class StreamDeckTab : public juce::Component,
                      public juce::Timer {
public:
    /**
     * @brief Construct the Stream Deck tab.
     * @param manager Reference to the control manager.
     */
    explicit StreamDeckTab(ControlManager& manager);
    ~StreamDeckTab() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    /** @brief Refresh all status labels from server state. */
    void updateStatus();

    ControlManager& manager_;

    // WebSocket section
    juce::Label wsSectionLabel_{"", "WebSocket Server"};
    juce::Label wsPortLabel_{"", "Port:"};
    juce::Label wsPortValueLabel_{"", "8765"};
    juce::Label wsStatusLabel_{"", "Status:"};
    juce::Label wsStatusValueLabel_{"", "Stopped"};
    juce::Label wsClientsLabel_{"", "Clients:"};
    juce::Label wsClientsValueLabel_{"", "0"};
    juce::TextButton wsToggleButton_{"Start"};

    // HTTP section
    juce::Label httpSectionLabel_{"", "HTTP API Server"};
    juce::Label httpPortLabel_{"", "Port:"};
    juce::Label httpPortValueLabel_{"", "8766"};
    juce::Label httpStatusLabel_{"", "Status:"};
    juce::Label httpStatusValueLabel_{"", "Stopped"};
    juce::TextButton httpToggleButton_{"Start"};

    // Info text
    juce::Label infoLabel_{"", "Stream Deck plugins connect via WebSocket.\n"
                                "HTTP API is available for custom integrations."};

    // Theme colours (dark)
    static constexpr juce::uint32 kBgColour       = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurfaceColour   = 0xFF2A2A40;
    static constexpr juce::uint32 kAccentColour    = 0xFF6C63FF;
    static constexpr juce::uint32 kGreenColour     = 0xFF4CAF50;
    static constexpr juce::uint32 kRedColour       = 0xFFE05050;
    static constexpr juce::uint32 kTextColour      = 0xFFE0E0E0;
    static constexpr juce::uint32 kDimTextColour   = 0xFF8888AA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StreamDeckTab)
};

// ═════════════════════════════════════════════════════════════════════════════
// ControlSettingsPanel — top-level tabbed container
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Tabbed settings panel combining Hotkey, MIDI, and Stream Deck tabs.
 *
 * Uses juce::TabbedComponent to switch between the three configuration
 * sub-panels. All tabs share a reference to the same ControlManager.
 */
class ControlSettingsPanel : public juce::Component {
public:
    /**
     * @brief Construct the control settings panel.
     * @param manager Reference to the control manager that owns all handlers.
     */
    explicit ControlSettingsPanel(ControlManager& manager);
    ~ControlSettingsPanel() override;

    // Non-copyable
    ControlSettingsPanel(const ControlSettingsPanel&) = delete;
    ControlSettingsPanel& operator=(const ControlSettingsPanel&) = delete;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Refresh all tabs to reflect the current control configuration.
     */
    void refreshAll();

private:
    ControlManager& manager_;

    // Tabbed component (owns the tab bar and content area)
    juce::TabbedComponent tabbedComponent_{juce::TabbedButtonBar::TabsAtTop};

    // Tab content components (owned separately, added to tabbed component)
    std::unique_ptr<HotkeyTab> hotkeyTab_;
    std::unique_ptr<MidiTab> midiTab_;
    std::unique_ptr<StreamDeckTab> streamDeckTab_;

    // Theme colours (dark)
    static constexpr juce::uint32 kBgColour      = 0xFF1E1E2E;
    static constexpr juce::uint32 kTabBarColour   = 0xFF2A2A40;
    static constexpr juce::uint32 kAccentColour   = 0xFF6C63FF;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlSettingsPanel)
};

} // namespace directpipe
