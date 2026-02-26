/**
 * @file ControlMapping.h
 * @brief Persistent storage for hotkey/MIDI/server control mappings
 *
 * Serializes and deserializes control configurations to JSON files.
 */
#pragma once

#include <JuceHeader.h>
#include "ActionDispatcher.h"

#include <string>
#include <vector>

namespace directpipe {

// Forward declarations for MIDI types
enum class MidiMappingType;

/// Hotkey mapping data
struct HotkeyMapping {
    uint32_t modifiers = 0;
    uint32_t virtualKey = 0;
    ActionEvent action;
    std::string displayName;
};

/// MIDI mapping data
struct MidiMapping {
    int cc = -1;
    int note = -1;
    int channel = 0;
    MidiMappingType type{};
    ActionEvent action;
    std::string deviceName;
};

/// Server configuration
struct ServerConfig {
    int websocketPort = 8765;
    bool websocketEnabled = true;
    int httpPort = 8766;
    bool httpEnabled = true;
};

/// Complete control configuration
struct ControlConfig {
    std::vector<HotkeyMapping> hotkeys;
    std::vector<MidiMapping> midiMappings;
    ServerConfig server;
};

/**
 * @brief Manages loading and saving of control mappings.
 *
 * Supports portable mode: if `portable.flag` exists next to the executable,
 * config is stored alongside the exe. Otherwise uses %AppData%.
 */
class ControlMappingStore {
public:
    ControlMappingStore() = default;

    /**
     * @brief Save configuration to file.
     * @param config Configuration to save.
     * @param file Target file (if empty, uses default path).
     * @return true if saved successfully.
     */
    bool save(const ControlConfig& config, const juce::File& file = {});

    /**
     * @brief Load configuration from file.
     * @param file Source file (if empty, uses default path).
     * @return Loaded configuration (or defaults if file doesn't exist).
     */
    ControlConfig load(const juce::File& file = {});

    /**
     * @brief Get the config file path (respects portable mode).
     */
    static juce::File getDefaultConfigFile();

    /**
     * @brief Check if running in portable mode.
     * Portable mode is active when `portable.flag` exists next to the exe.
     */
    static bool isPortableMode();

    /**
     * @brief Get the config directory path.
     * Portable: `./config/` next to exe. Normal: `%AppData%/DirectPipe/`.
     */
    static juce::File getConfigDirectory();

    /**
     * @brief Create a default configuration with common hotkeys.
     */
    static ControlConfig createDefaults();

private:
    static juce::var actionEventToVar(const ActionEvent& event);
    static ActionEvent varToActionEvent(const juce::var& v);
};

} // namespace directpipe
