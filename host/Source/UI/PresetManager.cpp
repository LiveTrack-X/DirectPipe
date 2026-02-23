/**
 * @file PresetManager.cpp
 * @brief Preset management implementation
 */

#include "PresetManager.h"

namespace directpipe {

PresetManager::PresetManager(AudioEngine& engine)
    : engine_(engine)
{
}

bool PresetManager::savePreset(const juce::File& file)
{
    auto json = exportToJSON();
    if (json.isEmpty()) return false;

    return file.replaceWithText(json);
}

bool PresetManager::loadPreset(const juce::File& file)
{
    if (!file.existsAsFile()) return false;

    auto json = file.loadFileAsString();
    return importFromJSON(json);
}

juce::File PresetManager::getPresetsDirectory()
{
    auto appData = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory);
    auto presetsDir = appData.getChildFile("DirectPipe").getChildFile("Presets");
    presetsDir.createDirectory();
    return presetsDir;
}

juce::File PresetManager::getAutoSaveFile()
{
    auto appData = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory);
    auto dir = appData.getChildFile("DirectPipe");
    dir.createDirectory();
    return dir.getChildFile("settings.dppreset");
}

juce::Array<juce::File> PresetManager::getAvailablePresets()
{
    auto dir = getPresetsDirectory();
    return dir.findChildFiles(juce::File::findFiles, false,
                              juce::String("*") + kPresetExtension);
}

juce::String PresetManager::exportToJSON()
{
    auto root = std::make_unique<juce::DynamicObject>();

    root->setProperty("version", 2);

    // Audio settings
    auto& monitor = engine_.getLatencyMonitor();
    root->setProperty("sampleRate", monitor.getSampleRate());
    root->setProperty("bufferSize", monitor.getBufferSize());
    root->setProperty("inputGain", static_cast<double>(engine_.getInputGain()));

    // Input/output device names
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    engine_.getDeviceManager().getAudioDeviceSetup(setup);
    root->setProperty("inputDevice", setup.inputDeviceName);
    root->setProperty("outputDevice", setup.outputDeviceName);

    // VST Chain
    juce::Array<juce::var> plugins;
    auto& chain = engine_.getVSTChain();
    for (int i = 0; i < chain.getPluginCount(); ++i) {
        if (auto* slot = chain.getPluginSlot(i)) {
            auto plugin = std::make_unique<juce::DynamicObject>();
            plugin->setProperty("name", slot->name);
            plugin->setProperty("path", slot->path);
            plugin->setProperty("bypassed", slot->bypassed);
            plugins.add(juce::var(plugin.release()));
        }
    }
    root->setProperty("plugins", plugins);

    // Output settings
    auto& router = engine_.getOutputRouter();
    auto outputs = std::make_unique<juce::DynamicObject>();
    outputs->setProperty("obsVolume",
        static_cast<double>(router.getVolume(OutputRouter::Output::SharedMemory)));
    outputs->setProperty("obsEnabled",
        router.isEnabled(OutputRouter::Output::SharedMemory));
    outputs->setProperty("vmicVolume",
        static_cast<double>(router.getVolume(OutputRouter::Output::VirtualMic)));
    outputs->setProperty("vmicEnabled",
        router.isEnabled(OutputRouter::Output::VirtualMic));
    outputs->setProperty("monitorVolume",
        static_cast<double>(router.getVolume(OutputRouter::Output::Monitor)));
    outputs->setProperty("monitorEnabled",
        router.isEnabled(OutputRouter::Output::Monitor));
    root->setProperty("outputs", juce::var(outputs.release()));

    return juce::JSON::toString(juce::var(root.release()), true);
}

bool PresetManager::importFromJSON(const juce::String& json)
{
    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) return false;

    auto* root = parsed.getDynamicObject();
    if (!root) return false;

    // Check version
    int version = root->getProperty("version");
    if (version < 1) return false;

    // Audio settings
    if (root->hasProperty("sampleRate")) {
        engine_.setSampleRate(root->getProperty("sampleRate"));
    }
    if (root->hasProperty("bufferSize")) {
        engine_.setBufferSize(root->getProperty("bufferSize"));
    }
    if (root->hasProperty("inputGain")) {
        engine_.setInputGain(static_cast<float>((double)root->getProperty("inputGain")));
    }

    // Restore devices
    if (root->hasProperty("inputDevice")) {
        juce::String inputDev = root->getProperty("inputDevice").toString();
        if (inputDev.isNotEmpty()) {
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            engine_.getDeviceManager().getAudioDeviceSetup(setup);
            setup.inputDeviceName = inputDev;
            engine_.getDeviceManager().setAudioDeviceSetup(setup, true);
        }
    }
    if (root->hasProperty("outputDevice")) {
        juce::String outputDev = root->getProperty("outputDevice").toString();
        if (outputDev.isNotEmpty()) {
            engine_.setOutputDevice(outputDev);
        }
    }

    // VST Chain — load plugins
    if (root->hasProperty("plugins")) {
        auto* pluginsArray = root->getProperty("plugins").getArray();
        if (pluginsArray) {
            // Clear existing chain
            auto& chain = engine_.getVSTChain();
            while (chain.getPluginCount() > 0) {
                chain.removePlugin(0);
            }

            // Load plugins from preset
            auto& knownPlugins = chain.getKnownPlugins();
            for (auto& pluginVar : *pluginsArray) {
                if (auto* plugin = pluginVar.getDynamicObject()) {
                    juce::String path = plugin->getProperty("path").toString();
                    juce::String name = plugin->getProperty("name").toString();
                    bool bypassed = plugin->getProperty("bypassed");

                    // Try to find in known plugins first (preferred for VST3)
                    int idx = -1;
                    for (const auto& desc : knownPlugins.getTypes()) {
                        if (desc.fileOrIdentifier == path || desc.name == name) {
                            idx = chain.addPlugin(desc);
                            break;
                        }
                    }

                    // Fallback to file path
                    if (idx < 0) {
                        idx = chain.addPlugin(path);
                    }

                    if (idx >= 0 && bypassed) {
                        chain.setPluginBypassed(idx, true);
                    }
                }
            }
        }
    }

    // Output settings
    if (root->hasProperty("outputs")) {
        auto* outputs = root->getProperty("outputs").getDynamicObject();
        if (outputs) {
            auto& router = engine_.getOutputRouter();

            if (outputs->hasProperty("obsVolume"))
                router.setVolume(OutputRouter::Output::SharedMemory,
                                 static_cast<float>((double)outputs->getProperty("obsVolume")));
            if (outputs->hasProperty("obsEnabled"))
                router.setEnabled(OutputRouter::Output::SharedMemory,
                                  outputs->getProperty("obsEnabled"));

            if (outputs->hasProperty("vmicVolume"))
                router.setVolume(OutputRouter::Output::VirtualMic,
                                 static_cast<float>((double)outputs->getProperty("vmicVolume")));
            if (outputs->hasProperty("vmicEnabled"))
                router.setEnabled(OutputRouter::Output::VirtualMic,
                                  outputs->getProperty("vmicEnabled"));

            if (outputs->hasProperty("monitorVolume"))
                router.setVolume(OutputRouter::Output::Monitor,
                                 static_cast<float>((double)outputs->getProperty("monitorVolume")));
            if (outputs->hasProperty("monitorEnabled"))
                router.setEnabled(OutputRouter::Output::Monitor,
                                  outputs->getProperty("monitorEnabled"));
        }
    }

    return true;
}

} // namespace directpipe
