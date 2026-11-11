// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "Audio/AudioEngine.h"
#include "Platform/WindowsMicLevelKeeper.h"
#include "UI/AudioSettings.h"
#include "UI/PresetManager.h"

using namespace directpipe;

namespace {

struct NamedInputState {
    juce::String typeName { "Audio Settings Test" };
    juce::StringArray inputNames { "Analog 1", "Analog 2", "Analog 3" };
    int rejectedInput = -1;
    bool rejectSingleInput = false;
};

class NamedInputDevice final : public juce::AudioIODevice {
public:
    explicit NamedInputDevice(std::shared_ptr<NamedInputState> state)
        : juce::AudioIODevice("Test Interface", state->typeName), state_(std::move(state)) {}

    juce::StringArray getInputChannelNames() override { return state_->inputNames; }
    juce::StringArray getOutputChannelNames() override { return { "Playback 1", "Playback 2" }; }
    juce::Array<double> getAvailableSampleRates() override { return { 44100.0, 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 128, 256 }; }
    int getDefaultBufferSize() override { return 128; }
    juce::String open(const juce::BigInteger& inputs, const juce::BigInteger& outputs,
                      double sampleRate, int bufferSize) override
    {
        if ((state_->rejectedInput >= 0
                && inputs.findNextSetBit(0) == state_->rejectedInput)
            || (state_->rejectSingleInput && inputs.countNumberOfSetBits() == 1))
            return "Test interface rejected the requested input";
        if (inputs.getHighestBit() >= state_->inputNames.size())
            return "Test interface input is unavailable";
        inputs_ = inputs;
        outputs_ = outputs;
        sampleRate_ = sampleRate;
        bufferSize_ = bufferSize;
        open_ = true;
        return {};
    }
    void close() override { open_ = false; }
    bool isOpen() override { return open_; }
    void start(juce::AudioIODeviceCallback* callback) override
    {
        callback_ = callback;
        if (callback_) callback_->audioDeviceAboutToStart(this);
        playing_ = true;
    }
    void stop() override
    {
        if (playing_ && callback_) callback_->audioDeviceStopped();
        playing_ = false;
    }
    bool isPlaying() override { return playing_; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return bufferSize_; }
    double getCurrentSampleRate() override { return sampleRate_; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveInputChannels() const override { return inputs_; }
    juce::BigInteger getActiveOutputChannels() const override { return outputs_; }
    juce::String getWindowsInputEndpointId() const override
    {
        return state_->typeName == "Windows Audio" && open_ ? "capture-a" : juce::String{};
    }
    int getInputLatencyInSamples() override { return 0; }
    int getOutputLatencyInSamples() override { return 0; }

private:
    std::shared_ptr<NamedInputState> state_;
    juce::BigInteger inputs_, outputs_;
    juce::AudioIODeviceCallback* callback_ = nullptr;
    double sampleRate_ = 0.0;
    int bufferSize_ = 0;
    bool open_ = false, playing_ = false;
};

class NamedInputType final : public juce::AudioIODeviceType {
public:
    explicit NamedInputType(std::shared_ptr<NamedInputState> state)
        : juce::AudioIODeviceType(state->typeName), state_(std::move(state)) {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool) const override { return { "Test Interface" }; }
    int getDefaultDeviceIndex(bool) const override { return 0; }
    int getIndexOfDevice(juce::AudioIODevice* device, bool) const override { return device ? 0 : -1; }
    bool hasSeparateInputsAndOutputs() const override { return true; }
    juce::AudioIODevice* createDevice(const juce::String& output, const juce::String& input) override
    {
        return output.isNotEmpty() || input.isNotEmpty() ? new NamedInputDevice(state_) : nullptr;
    }

private:
    std::shared_ptr<NamedInputState> state_;
};

struct AudioSettingsHarness {
    juce::ScopedJuceInitialiser_GUI juce;
    AudioEngine engine;
    std::shared_ptr<NamedInputState> state = std::make_shared<NamedInputState>();
    std::unique_ptr<AudioSettings> settings;

    juce::String open(bool asio = false, int inputCount = 3)
    {
        if (asio) state->typeName = "Audio Settings Test ASIO";
        state->inputNames.clear();
        for (int index = 0; index < inputCount; ++index)
            state->inputNames.add("Analog " + juce::String(index + 1));
        auto& manager = engine.getDeviceManager();
        manager.addAudioDeviceType(std::make_unique<NamedInputType>(state));
        manager.setCurrentAudioDeviceType(state->typeName, true);
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        setup.inputDeviceName = "Test Interface";
        setup.outputDeviceName = "Test Interface";
        setup.sampleRate = 48000.0;
        setup.bufferSize = 128;
        setup.useDefaultInputChannels = false;
        setup.useDefaultOutputChannels = false;
        setup.inputChannels.setRange(0, juce::jmin(2, inputCount), true);
        setup.outputChannels.setRange(0, 2, true);
        const auto error = manager.setAudioDeviceSetup(setup, true);
        if (error.isEmpty()) {
            settings = std::make_unique<AudioSettings>(engine);
            settings->setSize(335, 700);
        }
        return error;
    }

    juce::ComboBox* combo(const juce::String& componentId) const
    {
        return dynamic_cast<juce::ComboBox*>(settings->findChildWithID(componentId));
    }

    juce::BigInteger activeInputs()
    {
        if (auto* device = engine.getDeviceManager().getCurrentAudioDevice())
            return device->getActiveInputChannels();
        return {};
    }
};

struct MicLevelUiState {
    WindowsMicLevelKeeper::Backend::Callback callback;
    juce::String attachedId;
    float scalar = 0.45f;
    int writes = 0;
};

class MicLevelUiBackend final : public WindowsMicLevelKeeper::Backend {
public:
    explicit MicLevelUiBackend(std::shared_ptr<MicLevelUiState> state)
        : state_(std::move(state)) {}

    WindowsMicLevelKeeper::Resolution resolve(
        const WindowsMicLevelKeeper::InputContext& context) override
    {
        if (context.deviceType.containsIgnoreCase("ASIO"))
            return { WindowsMicLevelKeeper::State::unsupported, {},
                     "ASIO does not identify a supported Windows capture endpoint." };
        if (!context.captureActive)
            return { WindowsMicLevelKeeper::State::unavailable, {},
                     "No active Windows microphone is selected." };
        if (context.endpointId != "capture-a")
            return { WindowsMicLevelKeeper::State::unavailable, {},
                     "The opened input has no exact Windows endpoint ID." };
        return { WindowsMicLevelKeeper::State::ready,
                 { "capture-a", "Test Interface" }, "Selected Windows microphone is ready." };
    }

    bool attach(const juce::String& id, Callback callback, juce::String& error) override
    {
        if (id != "capture-a") {
            error = "Unknown test endpoint";
            return false;
        }
        state_->attachedId = id;
        state_->callback = std::move(callback);
        error.clear();
        return true;
    }

    bool readScalar(float& value, juce::String& error) override
    {
        value = state_->scalar;
        error.clear();
        return true;
    }

    bool setScalar100(juce::String& error) override
    {
        ++state_->writes;
        state_->scalar = 1.0f;
        error.clear();
        return true;
    }

    void detach() noexcept override
    {
        state_->attachedId.clear();
        state_->callback = {};
    }

private:
    std::shared_ptr<MicLevelUiState> state_;
};

void saveRequestedMonoSnapshot(AudioSettings& settings)
{
    const auto path = juce::SystemStats::getEnvironmentVariable("DIRECTPIPE_TEST_UI_SNAPSHOT", {});
    if (path.isEmpty()) return;
    const juce::File file(path);
    ASSERT_TRUE(file.getParentDirectory().createDirectory().wasOk());
    juce::FileOutputStream stream(file);
    ASSERT_TRUE(stream.openedOk());
    ASSERT_TRUE(stream.setPosition(0));
    ASSERT_TRUE(stream.truncate().wasOk());
    ASSERT_TRUE(juce::PNGImageFormat().writeImageToStream(
        settings.createComponentSnapshot(settings.getLocalBounds()), stream));
}

} // namespace

TEST(AudioSettingsTest, WindowsMicLevelHoldDefaultsOffAndExplainsUnavailableDevice)
{
    juce::ScopedJuceInitialiser_GUI juce;
    AudioEngine engine;
    AudioSettings settings(engine);
    settings.setSize(335, 700);

    auto* hold = dynamic_cast<juce::ToggleButton*>(
        settings.findChildWithID("windowsMicLevel100"));
    ASSERT_NE(hold, nullptr)
        << "The system microphone level hold must be an explicit default-off option";
    EXPECT_EQ(hold->getButtonText(), "Keep Windows mic level at 100");
    EXPECT_FALSE(hold->getToggleState());
    EXPECT_FALSE(hold->isEnabled());
    EXPECT_TRUE(hold->isVisible());
    EXPECT_TRUE(settings.getLocalBounds().contains(hold->getBounds()));

    auto* reason = dynamic_cast<juce::Label*>(
        settings.findChildWithID("windowsMicLevelStatus"));
    ASSERT_NE(reason, nullptr);
    EXPECT_TRUE(reason->isVisible());
    EXPECT_TRUE(reason->getText().isNotEmpty());
    EXPECT_TRUE(settings.getLocalBounds().contains(reason->getBounds()));
    EXPECT_FALSE(engine.isMonoInputEnabled());
    EXPECT_EQ(engine.getChannelMode(), 2);
}

TEST(AudioSettingsTest, WindowsMicLevelHoldToggleChangesOnlyExplicitEndpointPreference)
{
    AudioSettingsHarness harness;
    harness.state->typeName = "Windows Audio";
    ASSERT_TRUE(harness.open().isEmpty());
    auto state = std::make_shared<MicLevelUiState>();
    WindowsMicLevelKeeper keeper(std::make_unique<MicLevelUiBackend>(state), false);
    keeper.setStartupReady(true);
    AudioSettings settings(harness.engine, &keeper);
    settings.setSize(335, 700);
    WindowsMicLevelKeeperTestAccess::drain(keeper, 0);
    settings.refreshFromEngine();

    auto* hold = dynamic_cast<juce::ToggleButton*>(
        settings.findChildWithID("windowsMicLevel100"));
    ASSERT_NE(hold, nullptr);
    ASSERT_TRUE(hold->isEnabled());
    EXPECT_FALSE(hold->getToggleState());
    EXPECT_EQ(state->writes, 0);
    const auto inputChannels = harness.activeInputs();
    const auto outputVolume = harness.engine.getOutputRouter().getVolume(OutputRouter::Output::Main);
    const auto inputGain = harness.engine.getInputGain();
    int saves = 0;
    settings.onSettingsChanged = [&saves] { ++saves; };

    hold->setToggleState(true, juce::sendNotificationSync);
    WindowsMicLevelKeeperTestAccess::drain(keeper, 100);
    EXPECT_TRUE(keeper.getPreferences().enabledEndpointIds.contains("capture-a"));
    EXPECT_EQ(state->writes, 0) << "Enabling queues a correction outside the UI callback";
    EXPECT_FLOAT_EQ(state->scalar, 0.45f);
    WindowsMicLevelKeeperTestAccess::drain(keeper, 150);
    EXPECT_FLOAT_EQ(state->scalar, 1.0f);
    EXPECT_EQ(state->writes, 1);
    EXPECT_EQ(saves, 1);
    settings.refreshFromEngine();
    EXPECT_TRUE(hold->getToggleState());
    EXPECT_EQ(saves, 1) << "Refreshing a saved preference must never manufacture user consent";
    saveRequestedMonoSnapshot(settings);

    hold->setToggleState(false, juce::sendNotificationSync);
    WindowsMicLevelKeeperTestAccess::drain(keeper, 200);
    EXPECT_TRUE(keeper.getPreferences().enabledEndpointIds.isEmpty());
    EXPECT_EQ(saves, 2);
    EXPECT_EQ(state->writes, 1) << "Disabling must not restore an old system volume";
    EXPECT_EQ(harness.activeInputs(), inputChannels);
    EXPECT_EQ(harness.engine.getChannelMode(), 2);
    EXPECT_FLOAT_EQ(harness.engine.getInputGain(), inputGain);
    EXPECT_FLOAT_EQ(harness.engine.getOutputRouter().getVolume(OutputRouter::Output::Main), outputVolume);
    EXPECT_TRUE(settings.getLocalBounds().contains(hold->getBounds()));
}

TEST(AudioSettingsTest, WindowsMicLevelHoldExplainsAsioWithoutCreatingConsent)
{
    AudioSettingsHarness harness;
    ASSERT_TRUE(harness.open(true).isEmpty());
    auto state = std::make_shared<MicLevelUiState>();
    WindowsMicLevelKeeper keeper(std::make_unique<MicLevelUiBackend>(state), false);
    keeper.setStartupReady(true);
    AudioSettings settings(harness.engine, &keeper);
    settings.setSize(335, 700);
    WindowsMicLevelKeeperTestAccess::drain(keeper, 0);
    settings.refreshFromEngine();

    auto* hold = dynamic_cast<juce::ToggleButton*>(
        settings.findChildWithID("windowsMicLevel100"));
    auto* reason = dynamic_cast<juce::Label*>(
        settings.findChildWithID("windowsMicLevelStatus"));
    ASSERT_NE(hold, nullptr);
    ASSERT_NE(reason, nullptr);
    EXPECT_FALSE(hold->isEnabled());
    EXPECT_FALSE(hold->getToggleState());
    EXPECT_TRUE(reason->getText().containsIgnoreCase("ASIO"));
    EXPECT_TRUE(keeper.getPreferences().enabledEndpointIds.isEmpty());
    EXPECT_EQ(state->writes, 0);
    EXPECT_TRUE(settings.getLocalBounds().contains(reason->getBounds()));
}

TEST(AudioSettingsTest, CaptureSelectorDefaultsToExistingPairWithoutChangingLegacyMono)
{
    juce::ScopedJuceInitialiser_GUI juce;
    AudioEngine engine;
    engine.setChannelMode(1);
    AudioSettings settings(engine);

    auto* capture = dynamic_cast<juce::ComboBox*>(
        settings.findChildWithID("inputCaptureMode"));
    ASSERT_NE(capture, nullptr)
        << "Input capture must be separate from the existing Mono/Stereo processing setting";
    EXPECT_EQ(capture->getSelectedId(), 1);
    EXPECT_TRUE(capture->isVisible());
    EXPECT_FALSE(capture->isEnabled())
        << "No physical input names are available without an open device";
    EXPECT_EQ(engine.getChannelMode(), 1);

    juce::ToggleButton* legacyMono = nullptr;
    for (int index = 0; index < settings.getNumChildComponents(); ++index) {
        if (auto* button = dynamic_cast<juce::ToggleButton*>(
                settings.getChildComponent(index))) {
            if (button->getButtonText() == "Mono")
                legacyMono = button;
        }
    }
    ASSERT_NE(legacyMono, nullptr);
    EXPECT_TRUE(legacyMono->getToggleState());
}

TEST(AudioSettingsTest, MonoCaptureListsDriverNamesAndSelectsPhysicalAnalog2)
{
    AudioSettingsHarness harness;
    harness.engine.setChannelMode(1);
    ASSERT_TRUE(harness.open(true).isEmpty());
    auto* capture = harness.combo("inputCaptureMode");
    auto* input = harness.combo("inputChannel");
    auto* output = harness.combo("outputChannel");
    ASSERT_NE(capture, nullptr);
    ASSERT_NE(input, nullptr);
    ASSERT_NE(output, nullptr);
    ASSERT_EQ(input->getNumItems(), 1);
    EXPECT_EQ(input->getItemText(0), "Analog 1 + Analog 2");
    const auto originalOutput = output->getText();

    capture->setSelectedId(2, juce::sendNotificationSync);
    ASSERT_TRUE(harness.engine.isMonoInputEnabled());
    ASSERT_EQ(input->getNumItems(), 3);
    EXPECT_EQ(input->getItemText(0), "Analog 1");
    EXPECT_EQ(input->getItemText(1), "Analog 2");
    EXPECT_EQ(input->getItemText(2), "Analog 3");
    input->setSelectedId(2, juce::sendNotificationSync);
    EXPECT_EQ(harness.engine.getActiveInputChannelOffset(), 1);
    EXPECT_EQ(harness.activeInputs().countNumberOfSetBits(), 1);
    EXPECT_TRUE(harness.activeInputs()[1]);
    EXPECT_EQ(harness.engine.getChannelMode(), 1);
    EXPECT_EQ(output->getText(), originalOutput);
    EXPECT_EQ(output->getNumItems(), 1);
    EXPECT_TRUE(output->isVisible());
    EXPECT_TRUE(harness.settings->getLocalBounds().contains(capture->getBounds()));
    EXPECT_TRUE(harness.settings->getLocalBounds().contains(input->getBounds()));
    saveRequestedMonoSnapshot(*harness.settings);
}

TEST(AudioSettingsTest, NonAsioInputSelectionAppearsOnlyAfterOptingIntoMonoCapture)
{
    AudioSettingsHarness harness;
    ASSERT_TRUE(harness.open().isEmpty());
    auto* capture = harness.combo("inputCaptureMode");
    auto* input = harness.combo("inputChannel");
    auto* output = harness.combo("outputChannel");
    ASSERT_NE(capture, nullptr);
    ASSERT_NE(input, nullptr);
    ASSERT_NE(output, nullptr);
    EXPECT_TRUE(capture->isVisible());
    EXPECT_FALSE(input->isVisible());
    EXPECT_FALSE(output->isVisible());

    capture->setSelectedId(2, juce::sendNotificationSync);
    ASSERT_TRUE(harness.engine.isMonoInputEnabled());
    EXPECT_TRUE(input->isVisible());
    input->setSelectedId(2, juce::sendNotificationSync);
    capture->setSelectedId(1, juce::sendNotificationSync);
    EXPECT_FALSE(harness.engine.isMonoInputEnabled());
    EXPECT_FALSE(input->isVisible());
    EXPECT_FALSE(output->isVisible());
    EXPECT_EQ(harness.activeInputs().countNumberOfSetBits(), 2);
    EXPECT_TRUE(harness.activeInputs()[0]);
    EXPECT_TRUE(harness.activeInputs()[1]);
}

TEST(AudioSettingsTest, UnpairedOddTailKeepsMonoSelectionAndReportsErrorWithoutSaving)
{
    AudioSettingsHarness harness;
    ASSERT_TRUE(harness.open().isEmpty());
    auto* capture = harness.combo("inputCaptureMode");
    auto* input = harness.combo("inputChannel");
    ASSERT_NE(capture, nullptr);
    ASSERT_NE(input, nullptr);
    capture->setSelectedId(2, juce::sendNotificationSync);
    input->setSelectedId(3, juce::sendNotificationSync);
    ASSERT_EQ(harness.engine.getActiveInputChannelOffset(), 2);
    int savedChanges = 0;
    juce::String error;
    harness.settings->onSettingsChanged = [&] { ++savedChanges; };
    harness.settings->onError = [&](const juce::String& message) { error = message; };

    capture->setSelectedId(1, juce::sendNotificationSync);
    EXPECT_TRUE(harness.engine.isMonoInputEnabled());
    EXPECT_EQ(capture->getSelectedId(), 2);
    EXPECT_EQ(input->getSelectedId(), 3);
    EXPECT_TRUE(harness.activeInputs()[2]);
    EXPECT_EQ(harness.activeInputs().countNumberOfSetBits(), 1);
    EXPECT_TRUE(error.isNotEmpty());
    EXPECT_EQ(savedChanges, 0);
}

TEST(AudioSettingsTest, FailedCaptureChangeRestoresVisiblePairModeWithoutSaving)
{
    AudioSettingsHarness harness;
    ASSERT_TRUE(harness.open(true).isEmpty());
    auto* capture = harness.combo("inputCaptureMode");
    ASSERT_NE(capture, nullptr);
    harness.state->rejectSingleInput = true;
    int savedChanges = 0;
    juce::String error;
    harness.settings->onSettingsChanged = [&] { ++savedChanges; };
    harness.settings->onError = [&](const juce::String& message) { error = message; };

    capture->setSelectedId(2, juce::sendNotificationSync);
    EXPECT_FALSE(harness.engine.isMonoInputEnabled());
    EXPECT_EQ(capture->getSelectedId(), 1);
    EXPECT_EQ(harness.activeInputs().countNumberOfSetBits(), 2);
    EXPECT_TRUE(error.isNotEmpty());
    EXPECT_EQ(savedChanges, 0);
}

TEST(AudioSettingsTest, FailedChannelChangeRestoresVisiblePhysicalSelectionWithoutSaving)
{
    AudioSettingsHarness harness;
    ASSERT_TRUE(harness.open().isEmpty());
    auto* capture = harness.combo("inputCaptureMode");
    auto* input = harness.combo("inputChannel");
    ASSERT_NE(capture, nullptr);
    ASSERT_NE(input, nullptr);
    capture->setSelectedId(2, juce::sendNotificationSync);
    ASSERT_TRUE(harness.engine.isMonoInputEnabled());
    harness.state->rejectedInput = 1;
    int savedChanges = 0;
    juce::String error;
    harness.settings->onSettingsChanged = [&] { ++savedChanges; };
    harness.settings->onError = [&](const juce::String& message) { error = message; };

    input->setSelectedId(2, juce::sendNotificationSync);
    EXPECT_TRUE(harness.engine.isMonoInputEnabled());
    EXPECT_EQ(input->getSelectedId(), 1);
    EXPECT_TRUE(harness.activeInputs()[0]);
    EXPECT_FALSE(harness.activeInputs()[1]);
    EXPECT_TRUE(error.isNotEmpty());
    EXPECT_EQ(savedChanges, 0);
}

TEST(AudioSettingsTest, NativeOneChannelDeviceCanReturnToLegacyCaptureMode)
{
    AudioSettingsHarness harness;
    ASSERT_TRUE(harness.open(false, 1).isEmpty());
    auto* capture = harness.combo("inputCaptureMode");
    ASSERT_NE(capture, nullptr);
    capture->setSelectedId(2, juce::sendNotificationSync);
    ASSERT_TRUE(harness.engine.isMonoInputEnabled());
    capture->setSelectedId(1, juce::sendNotificationSync);
    EXPECT_FALSE(harness.engine.isMonoInputEnabled());
    EXPECT_EQ(capture->getSelectedId(), 1);
    EXPECT_EQ(harness.activeInputs().countNumberOfSetBits(), 1);
    EXPECT_TRUE(harness.activeInputs()[0]);
}

TEST(AudioSettingsTest, UnavailableDeviceKeepsSavedMonoModeVisibleWithoutInventedChannels)
{
    juce::ScopedJuceInitialiser_GUI juce;
    AudioEngine engine;
    engine.setMonoInputEnabled(true);
    AudioSettings settings(engine);
    auto* capture = dynamic_cast<juce::ComboBox*>(settings.findChildWithID("inputCaptureMode"));
    auto* input = dynamic_cast<juce::ComboBox*>(settings.findChildWithID("inputChannel"));
    ASSERT_NE(capture, nullptr);
    ASSERT_NE(input, nullptr);
    EXPECT_TRUE(capture->isVisible());
    EXPECT_EQ(capture->getSelectedId(), 2);
    EXPECT_FALSE(capture->isEnabled());
    EXPECT_TRUE(input->isVisible());
    EXPECT_FALSE(input->isEnabled());
    EXPECT_EQ(input->getNumItems(), 0);
    EXPECT_EQ(engine.getChannelMode(), 2);
}

TEST(AudioSettingsTest, LegacyPairPresetRestoresRateAndBufferAfterMonoCapture)
{
    AudioSettingsHarness harness;
    ASSERT_TRUE(harness.open().isEmpty());
    ASSERT_TRUE(harness.engine.setInputCaptureMode(true, 1));
    ASSERT_EQ(harness.activeInputs().countNumberOfSetBits(), 1);
    ASSERT_TRUE(harness.activeInputs()[1]);
    PresetManager manager(harness.engine);
    juce::StringArray warnings;
    manager.onImportWarning = [&](const juce::String& warning) { warnings.add(warning); };

    ASSERT_TRUE(manager.importFromJSON(R"({"version":4,"channelMode":2,
        "sampleRate":44100,"bufferSize":256,
        "inputChannelMask":[0,1],"outputChannelMask":[0,1]})"));

    EXPECT_FALSE(harness.engine.isMonoInputEnabled());
    EXPECT_EQ(harness.engine.getChannelMode(), 2);
    auto* device = harness.engine.getDeviceManager().getCurrentAudioDevice();
    ASSERT_NE(device, nullptr);
    EXPECT_DOUBLE_EQ(device->getCurrentSampleRate(), 44100.0);
    EXPECT_EQ(device->getCurrentBufferSizeSamples(), 256);
    EXPECT_EQ(harness.activeInputs().countNumberOfSetBits(), 2);
    EXPECT_TRUE(harness.activeInputs()[0]);
    EXPECT_TRUE(harness.activeInputs()[1]);
    EXPECT_TRUE(warnings.isEmpty()) << warnings.joinIntoString("; ").toStdString();
}

TEST(AudioSettingsTest, LegacySettingsWithoutInputMaskRestoreValidPairAfterMonoCapture)
{
    AudioSettingsHarness harness;
    ASSERT_TRUE(harness.open().isEmpty());
    ASSERT_TRUE(harness.engine.setInputCaptureMode(true, 1));
    ASSERT_TRUE(harness.activeInputs()[1]);
    PresetManager manager(harness.engine);
    juce::StringArray warnings;
    manager.onImportWarning = [&](const juce::String& warning) { warnings.add(warning); };

    ASSERT_TRUE(manager.importFromJSON(R"({"version":4,"channelMode":2,
        "sampleRate":44100,"bufferSize":256,"outputChannelMask":[0,1]})"));

    EXPECT_FALSE(harness.engine.isMonoInputEnabled());
    EXPECT_EQ(harness.engine.getChannelMode(), 2);
    auto* device = harness.engine.getDeviceManager().getCurrentAudioDevice();
    ASSERT_NE(device, nullptr);
    EXPECT_DOUBLE_EQ(device->getCurrentSampleRate(), 44100.0);
    EXPECT_EQ(device->getCurrentBufferSizeSamples(), 256);
    EXPECT_EQ(harness.activeInputs().countNumberOfSetBits(), 2);
    EXPECT_TRUE(harness.activeInputs()[0]);
    EXPECT_TRUE(harness.activeInputs()[1]);
    EXPECT_TRUE(warnings.isEmpty()) << warnings.joinIntoString("; ").toStdString();
}

TEST(AudioSettingsTest, UnavailableSavedAsioTargetKeepsCurrentMonoInputSelection)
{
    AudioSettingsHarness harness;
    ASSERT_TRUE(harness.open(true).isEmpty());
    ASSERT_TRUE(harness.engine.setInputCaptureMode(true, 1));
    ASSERT_EQ(harness.activeInputs().countNumberOfSetBits(), 1);
    ASSERT_TRUE(harness.activeInputs()[1]);
    PresetManager manager(harness.engine);
    juce::StringArray warnings;
    manager.onImportWarning = [&](const juce::String& warning) { warnings.add(warning); };

    ASSERT_TRUE(manager.importFromJSON(R"({"version":4,"channelMode":2,
        "deviceType":"Audio Settings Test ASIO",
        "inputDevice":"Missing Interface","outputDevice":"Missing Interface",
        "inputChannelMask":[0,1],"outputChannelMask":[0,1]})"));

    EXPECT_TRUE(harness.engine.isMonoInputEnabled());
    EXPECT_EQ(harness.activeInputs().countNumberOfSetBits(), 1);
    EXPECT_FALSE(harness.activeInputs()[0]);
    EXPECT_TRUE(harness.activeInputs()[1]);
    auto* device = harness.engine.getDeviceManager().getCurrentAudioDevice();
    ASSERT_NE(device, nullptr);
    EXPECT_EQ(device->getName(), "Test Interface");
    EXPECT_EQ(harness.engine.getActiveInputChannelOffset(), 1);
    EXPECT_FALSE(warnings.isEmpty());
}
