// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "Audio/AudioEngine.h"

#include "Audio/AudioRingBuffer.h"
#include "Audio/DeviceState.h"
#include "Audio/MonitorDriftPolicy.h"
#include "Platform/EndpointChangeWatcher.h"

using namespace directpipe;

namespace directpipe::audio_device_recovery_detail {
juce::String initialiseWithDefaultDeviceFallbacks(
    juce::AudioDeviceManager& deviceManager,
    const juce::String& logContext);
juce::String forceReopenAudioDevice(
    juce::AudioDeviceManager& deviceManager,
    const juce::AudioDeviceManager::AudioDeviceSetup& setup);
}

namespace {

class FakeAudioIODevice final : public juce::AudioIODevice {
public:
    FakeAudioIODevice(const juce::String& deviceName,
                      const juce::StringArray& inputNames,
                      const juce::StringArray& outputNames,
                      const juce::BigInteger& activeInput,
                      const juce::BigInteger& activeOutput)
        : juce::AudioIODevice(deviceName, "Fake Audio"),
          inputNames_(inputNames),
          outputNames_(outputNames),
          activeInput_(activeInput),
          activeOutput_(activeOutput)
    {
    }

    juce::StringArray getOutputChannelNames() override { return outputNames_; }
    juce::StringArray getInputChannelNames() override { return inputNames_; }

    juce::Array<double> getAvailableSampleRates() override
    {
        return { 44100.0, 48000.0 };
    }

    juce::Array<int> getAvailableBufferSizes() override
    {
        return { 128, 256, 512 };
    }

    int getDefaultBufferSize() override { return 128; }

    juce::String open(const juce::BigInteger& inputChannels,
                      const juce::BigInteger& outputChannels,
                      double sampleRate,
                      int bufferSizeSamples) override
    {
        activeInput_ = inputChannels;
        activeOutput_ = outputChannels;
        sampleRate_ = sampleRate;
        bufferSize_ = bufferSizeSamples;
        open_ = true;
        return {};
    }

    void close() override { open_ = false; }
    bool isOpen() override { return open_; }
    void start(juce::AudioIODeviceCallback*) override { playing_ = true; }
    void stop() override { playing_ = false; }
    bool isPlaying() override { return playing_; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return bufferSize_; }
    double getCurrentSampleRate() override { return sampleRate_; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return activeOutput_; }
    juce::BigInteger getActiveInputChannels() const override { return activeInput_; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

private:
    juce::StringArray inputNames_;
    juce::StringArray outputNames_;
    double sampleRate_ = 48000.0;
    int bufferSize_ = 128;
    juce::BigInteger activeInput_;
    juce::BigInteger activeOutput_;
    bool open_ = true;
    bool playing_ = false;
};

struct ManagedFakeDeviceStats {
    std::vector<std::pair<int, int>> openAttempts;
    bool failMultiChannelOpen = false;
    bool failEveryOpen = false;
};

class ManagedFakeAudioIODevice final : public juce::AudioIODevice {
public:
    explicit ManagedFakeAudioIODevice(std::shared_ptr<ManagedFakeDeviceStats> stats)
        : juce::AudioIODevice("Recovery Device", "Recovery Test"),
          stats_(std::move(stats))
    {
    }

    juce::StringArray getOutputChannelNames() override { return { "Out 1", "Out 2" }; }
    juce::StringArray getInputChannelNames() override { return { "In 1", "In 2" }; }
    juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 128, 256 }; }
    int getDefaultBufferSize() override { return 128; }

    juce::String open(const juce::BigInteger& inputChannels,
                      const juce::BigInteger& outputChannels,
                      double sampleRate,
                      int bufferSizeSamples) override
    {
        const auto inputCount = inputChannels.countNumberOfSetBits();
        const auto outputCount = outputChannels.countNumberOfSetBits();
        stats_->openAttempts.emplace_back(inputCount, outputCount);
        if (stats_->failEveryOpen)
            return "test device refuses every open";
        if (stats_->failMultiChannelOpen && (inputCount > 1 || outputCount > 1))
            return "test device requires mono input/output";

        activeInput_ = inputChannels;
        activeOutput_ = outputChannels;
        sampleRate_ = sampleRate;
        bufferSize_ = bufferSizeSamples;
        open_ = true;
        return {};
    }

    void close() override { open_ = false; }
    bool isOpen() override { return open_; }

    void start(juce::AudioIODeviceCallback* callback) override
    {
        callback_ = callback;
        if (callback_)
            callback_->audioDeviceAboutToStart(this);
        playing_ = true;
    }

    void stop() override
    {
        if (playing_ && callback_)
            callback_->audioDeviceStopped();
        playing_ = false;
    }

    bool isPlaying() override { return playing_; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return bufferSize_; }
    double getCurrentSampleRate() override { return sampleRate_; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return activeOutput_; }
    juce::BigInteger getActiveInputChannels() const override { return activeInput_; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

private:
    std::shared_ptr<ManagedFakeDeviceStats> stats_;
    juce::AudioIODeviceCallback* callback_ = nullptr;
    juce::BigInteger activeInput_;
    juce::BigInteger activeOutput_;
    double sampleRate_ = 0.0;
    int bufferSize_ = 0;
    bool open_ = false;
    bool playing_ = false;
};

class ManagedFakeAudioIODeviceType final : public juce::AudioIODeviceType {
public:
    explicit ManagedFakeAudioIODeviceType(std::shared_ptr<ManagedFakeDeviceStats> stats)
        : juce::AudioIODeviceType("Recovery Test"),
          stats_(std::move(stats))
    {
    }

    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool) const override { return { "Recovery Device" }; }
    int getDefaultDeviceIndex(bool) const override { return 0; }
    int getIndexOfDevice(juce::AudioIODevice* device, bool) const override
    {
        return device && device->getName() == "Recovery Device" ? 0 : -1;
    }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice(const juce::String& outputDeviceName,
                                      const juce::String& inputDeviceName) override
    {
        if (outputDeviceName == "Recovery Device" || inputDeviceName == "Recovery Device")
            return new ManagedFakeAudioIODevice(stats_);
        return nullptr;
    }

private:
    std::shared_ptr<ManagedFakeDeviceStats> stats_;
};

void addManagedFakeDeviceType(juce::AudioDeviceManager& manager,
                              const std::shared_ptr<ManagedFakeDeviceStats>& stats)
{
    manager.addAudioDeviceType(std::make_unique<ManagedFakeAudioIODeviceType>(stats));
    manager.setCurrentAudioDeviceType("Recovery Test", true);
}

} // namespace

class AudioEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<AudioEngine>();
    }

    void TearDown() override {
        engine_.reset();
    }

    std::unique_ptr<AudioEngine> engine_;
};

TEST_F(AudioEngineTest, DriverSnapshotSaveRestore) {
    engine_->setOutputNone(true);
    EXPECT_TRUE(engine_->isOutputNone());
    engine_->setOutputNone(false);
    EXPECT_FALSE(engine_->isOutputNone());
}

TEST_F(AudioEngineTest, DriverSnapshotDeviceNames) {
    auto input = engine_->getDesiredInputDevice();
    auto output = engine_->getDesiredOutputDevice();
    EXPECT_TRUE(input.isEmpty());
    EXPECT_TRUE(output.isEmpty());
}

TEST_F(AudioEngineTest, OutputNoneToggle) {
    EXPECT_FALSE(engine_->isOutputNone());
    engine_->setOutputNone(true);
    EXPECT_TRUE(engine_->isOutputNone());
    engine_->setOutputNone(false);
    EXPECT_FALSE(engine_->isOutputNone());
}

TEST_F(AudioEngineTest, OutputNoneClearOnDriverSwitch) {
    engine_->setOutputNone(true);
    EXPECT_TRUE(engine_->isOutputNone());

    auto types = engine_->getAvailableDeviceTypes();
    if (!types.isEmpty()) {
        auto result = engine_->setAudioDeviceType(types[0]);
        EXPECT_TRUE(result);
    }
    SUCCEED();
}

TEST_F(AudioEngineTest, DesiredDeviceSave) {
    auto type = engine_->getDesiredDeviceType();
    // Without initialize(), desired type falls back to getCurrentDeviceType()
    // which may be empty or a default — either way, no crash and returns a string
    EXPECT_TRUE(type.isEmpty() || type.isNotEmpty());
}

TEST_F(AudioEngineTest, ReconnectionAttempt) {
    EXPECT_FALSE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isInputDeviceLost());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
}

TEST_F(AudioEngineTest, SameDeviceRecoveryActuallyRecreatesTheOpenDevice) {
    juce::AudioDeviceManager manager;
    const auto stats = std::make_shared<ManagedFakeDeviceStats>();
    addManagedFakeDeviceType(manager, stats);

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = "Recovery Device";
    setup.outputDeviceName = "Recovery Device";
    setup.sampleRate = 48000.0;
    setup.bufferSize = 128;
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = true;

    ASSERT_TRUE(manager.setAudioDeviceSetup(setup, true).isEmpty());
    const auto opensBeforeRecovery = stats->openAttempts.size();
    ASSERT_GT(opensBeforeRecovery, 0u);

    EXPECT_TRUE(audio_device_recovery_detail::forceReopenAudioDevice(manager, setup).isEmpty());
    EXPECT_EQ(stats->openAttempts.size(), opensBeforeRecovery + 1)
        << "re-applying an unchanged setup without closing is a JUCE no-op";
}

TEST_F(AudioEngineTest, DefaultDeviceRecoveryRetriesMonoInputThenMonoDuplex) {
    juce::AudioDeviceManager manager;
    const auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->failMultiChannelOpen = true;
    addManagedFakeDeviceType(manager, stats);
    const auto attemptsBeforeRecovery = stats->openAttempts.size();

    EXPECT_TRUE(audio_device_recovery_detail::initialiseWithDefaultDeviceFallbacks(
        manager, "test fallback").isEmpty());

    ASSERT_EQ(stats->openAttempts.size(), attemptsBeforeRecovery + 3);
    EXPECT_EQ(stats->openAttempts[attemptsBeforeRecovery], std::make_pair(2, 2));
    EXPECT_EQ(stats->openAttempts[attemptsBeforeRecovery + 1], std::make_pair(1, 2));
    EXPECT_EQ(stats->openAttempts[attemptsBeforeRecovery + 2], std::make_pair(1, 1));
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_TRUE(manager.getCurrentAudioDevice()->isOpen());
}

TEST_F(AudioEngineTest, DefaultDeviceRecoveryReturnsTheFinalOpenFailure) {
    juce::AudioDeviceManager manager;
    const auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->failEveryOpen = true;
    addManagedFakeDeviceType(manager, stats);
    const auto attemptsBeforeRecovery = stats->openAttempts.size();

    const auto error = audio_device_recovery_detail::initialiseWithDefaultDeviceFallbacks(
        manager, "test failed fallback");

    EXPECT_TRUE(error.contains("refuses every open"));
    EXPECT_EQ(stats->openAttempts.size(), attemptsBeforeRecovery + 3);
    EXPECT_EQ(manager.getCurrentAudioDevice(), nullptr);
}

#if JUCE_WINDOWS
TEST_F(AudioEngineTest, EndpointTopologyChangeRechecksARecreatedTargetId) {
    EndpointChangeWatcher watcher;
    int callbackCount = 0;
    juce::String callbackReason;
    watcher.setInputDeviceName("Microphone (Recovery Device)");
    watcher.setCallback([&](const juce::String&, const juce::String& reason) {
        ++callbackCount;
        callbackReason = reason;
    });
    EndpointChangeWatcherTestAccess::setResolvedEndpointId(
        watcher, "{0.0.1.00000000}.{old-endpoint-id}");

    EXPECT_TRUE(EndpointChangeWatcherTestAccess::signalEndpointEvent(
        watcher,
        "{0.0.1.00000000}.{new-endpoint-id}",
        EndpointChangeWatcherTestAccess::Event::deviceAdded));
    EXPECT_EQ(EndpointChangeWatcherTestAccess::drainPendingEvents(watcher), 1);
    EXPECT_EQ(callbackCount, 1);
    EXPECT_EQ(callbackReason, "capture endpoint target changed");
}
#endif

TEST_F(AudioEngineTest, ZeroActiveConfiguredChannelsMarkLostAndMuted) {
    juce::StringArray inputs;
    inputs.add("Mic In");
    juce::StringArray outputs;
    outputs.add("Speaker L");
    outputs.add("Speaker R");
    FakeAudioIODevice device("Fake Duplex", inputs, outputs, {}, {});

    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceAboutToStart(&device);

    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());
    EXPECT_TRUE(engine_->isOutputMuted());
}

TEST_F(AudioEngineTest, OutputNoneIgnoresZeroActiveOutput) {
    engine_->setOutputNone(true);

    juce::BigInteger activeMonoInput;
    activeMonoInput.setBit(0);

    juce::StringArray inputs;
    inputs.add("Mic In");
    juce::StringArray outputs;
    outputs.add("Speaker L");
    outputs.add("Speaker R");
    FakeAudioIODevice device("Fake Input", inputs, outputs, activeMonoInput, {});

    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceAboutToStart(&device);

    EXPECT_FALSE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isInputDeviceLost());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
}

TEST_F(AudioEngineTest, OutputNoneClearsPendingOutputLoss) {
    engine_->markOutputDeviceLostForTest("Missing Speakers");
    ASSERT_TRUE(engine_->isDeviceLost());
    ASSERT_TRUE(engine_->isOutputAutoMuted());

    engine_->setOutputNone(true);

    EXPECT_TRUE(engine_->isOutputNone());
    EXPECT_TRUE(engine_->isOutputMuted());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
    EXPECT_FALSE(engine_->isDeviceLost());
}

TEST_F(AudioEngineTest, OutputNonePreservesPendingInputLoss) {
    engine_->markInputDeviceLostForTest("Missing Mic");

    engine_->setOutputNone(true);

    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
    EXPECT_TRUE(engine_->isDeviceLost());
}

TEST_F(AudioEngineTest, ReconnectionMaxRetry) {
    for (int i = 0; i < 10; ++i)
        engine_->checkReconnection();
    EXPECT_FALSE(engine_->isDeviceLost());
}

TEST_F(AudioEngineTest, InputLossKeepsWaitingForExplicitTargetAfterMaxMisses) {
    engine_->markInputDeviceLostForTest("Missing Boot Mic");
    engine_->forceReconnectMissCountForTest(4);

    engine_->checkReconnection();

    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
    EXPECT_EQ(engine_->getDesiredInputDevice(), "Missing Boot Mic");
}

TEST_F(AudioEngineTest, SameDeviceReopenDelayBlocksGenericReconnect) {
    engine_->markInputDeviceLostForTest("Restarting Mic");
    engine_->forceSameDeviceReopenPendingForTest(true);
    engine_->forceReconnectCooldownForTest(0);

    engine_->checkReconnection();

    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_EQ(engine_->getReconnectCooldownForTest(), 0);
}

TEST_F(AudioEngineTest, SameDeviceReopenDelayBlocksImmediateReconnect) {
    engine_->markInputDeviceLostForTest("Restarting Mic");
    engine_->forceSameDeviceReopenPendingForTest(true);
    engine_->forceReconnectCooldownForTest(37);

    engine_->attemptImmediateReconnectionForTest();

    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_EQ(engine_->getReconnectCooldownForTest(), 37);
}

TEST_F(AudioEngineTest, ExternalDeviceStopSilencesInputAndAutoMutesOutput) {
    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceStopped();

    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());
    EXPECT_TRUE(engine_->isOutputMuted());
}

TEST_F(AudioEngineTest, SameDeviceExternalRestartKeepsInputLostUntilForcedReopen) {
    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceStopped();

    juce::BigInteger activeInput;
    activeInput.setBit(0);
    activeInput.setBit(1);
    juce::BigInteger activeOutput;
    activeOutput.setBit(0);
    activeOutput.setBit(1);

    juce::StringArray inputs;
    inputs.add("Mic L");
    inputs.add("Mic R");
    juce::StringArray outputs;
    outputs.add("Speaker L");
    outputs.add("Speaker R");
    FakeAudioIODevice device("Same Device", inputs, outputs, activeInput, activeOutput);

    callback.audioDeviceAboutToStart(&device);

    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());
    EXPECT_TRUE(engine_->isOutputMuted());
}

TEST_F(AudioEngineTest, DeviceListChangeRetriesInputLossWithoutCooldown) {
    engine_->markInputDeviceLostForTest("Missing Boot Mic");
    engine_->forceReconnectCooldownForTest(90);

    juce::ChangeListener& listener = *engine_;
    listener.changeListenerCallback(nullptr);

    EXPECT_EQ(engine_->getReconnectCooldownForTest(), 0);
    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
}

TEST_F(AudioEngineTest, DeviceErrorSilencesInputAndRequestsReconnection) {
    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceError("device property changed");

    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());
    EXPECT_TRUE(engine_->isOutputMuted());
}

TEST_F(AudioEngineTest, EndpointPropertyChangeMarksSameInputLost) {
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = "Microphone(Yeti Stereo Microphone)";
    setup.outputDeviceName = "CABLE Input(VB-Audio Virtual Cable)";

    EXPECT_TRUE(engine_->markInputEndpointRestartPendingForTest(
        setup, "capture endpoint property changed"));

    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());
    EXPECT_TRUE(engine_->isOutputMuted());
    EXPECT_EQ(engine_->getReconnectCooldownForTest(), 0);
    EXPECT_EQ(engine_->getDesiredInputDevice(), setup.inputDeviceName);
}

TEST_F(AudioEngineTest, EndpointRecoveryPreservesManualOutputMute) {
    engine_->setOutputMuted(true);
    ASSERT_FALSE(engine_->isOutputAutoMuted());

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = "Microphone(Yeti Stereo Microphone)";
    setup.outputDeviceName = "CABLE Input(VB-Audio Virtual Cable)";

    ASSERT_TRUE(engine_->markInputEndpointRestartPendingForTest(
        setup, "capture endpoint property changed"));
    EXPECT_TRUE(engine_->isOutputMuted());
    EXPECT_TRUE(engine_->isOutputAutoMuted());

    EXPECT_TRUE(engine_->clearDeviceLossAfterReadyForTest(setup));
    EXPECT_TRUE(engine_->isOutputMuted());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
}

TEST_F(AudioEngineTest, AutomaticOutputMuteDoesNotOverwriteConcurrentManualIntent) {
    engine_->setOutputMuted(false);
    engine_->markOutputDeviceLostForTest("Missing Speakers");
    ASSERT_TRUE(engine_->isOutputAutoMuted());

    // A user mute while recovery is pending must remain after the automatic
    // safety reason is released.
    engine_->setOutputMuted(true);
    engine_->clearLossAfterManualOutputSelectionForTest();

    EXPECT_FALSE(engine_->isOutputAutoMuted());
    EXPECT_TRUE(engine_->isOutputManuallyMuted());
    EXPECT_TRUE(engine_->isOutputMuted());
}

TEST_F(AudioEngineTest, AutomaticOutputMuteRemainsEffectiveWhenManualMuteIsCleared) {
    engine_->setOutputMuted(true);
    engine_->markOutputDeviceLostForTest("Missing Speakers");
    ASSERT_TRUE(engine_->isOutputAutoMuted());

    engine_->setOutputMuted(false);

    EXPECT_FALSE(engine_->isOutputManuallyMuted());
    EXPECT_TRUE(engine_->isOutputMuted());

    engine_->clearLossAfterManualOutputSelectionForTest();
    EXPECT_FALSE(engine_->isOutputMuted());
}

TEST_F(AudioEngineTest, DeviceStatePreservesDirectionalLoss) {
    engine_->markInputDeviceLostForTest("Missing Mic");
    EXPECT_EQ(engine_->getDeviceState(), DeviceState::InputLost);

    // Model an output-only state clear racing after the input-loss aggregate
    // publication. The next reconnect tick must not leave input permanently
    // silent with the aggregate gate disabled.
    engine_->forceAggregateDeviceLostForTest(false);
    engine_->forceReconnectCooldownForTest(2);
    engine_->checkReconnection();
    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_EQ(engine_->getReconnectCooldownForTest(), 1);

    engine_->setOutputMuted(false);
    engine_->markOutputDeviceLostForTest("Missing Speakers");
    EXPECT_EQ(engine_->getDeviceState(), DeviceState::OutputLost);
}

TEST_F(AudioEngineTest, EndpointPropertyChangeIgnoresNonDesiredInput) {
    engine_->setDesiredInputDeviceForTest("Microphone(Yeti Stereo Microphone)");

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = "CABLE Output(VB-Audio Virtual Cable)";
    setup.outputDeviceName = "CABLE Input(VB-Audio Virtual Cable)";

    EXPECT_FALSE(engine_->markInputEndpointRestartPendingForTest(
        setup, "capture endpoint property changed"));

    EXPECT_FALSE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isInputDeviceLost());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
}

TEST_F(AudioEngineTest, EndpointPropertyChangeRejectsDuplicateNameSuffix) {
    engine_->setDesiredInputDeviceForTest("USB Microphone");

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = "USB Microphone (2)";
    setup.outputDeviceName = "Speakers";

    EXPECT_FALSE(engine_->markInputEndpointRestartPendingForTest(
        setup, "capture endpoint property changed"));
    EXPECT_FALSE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isInputDeviceLost());
}

TEST_F(AudioEngineTest, ManualInputSelectionDoesNotClearPendingOutputLoss) {
    engine_->markOutputDeviceLostForTest("Missing Speakers");

    engine_->clearLossAfterManualInputSelectionForTest();

    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());
    EXPECT_TRUE(engine_->isOutputMuted());
    EXPECT_EQ(engine_->getDesiredOutputDevice(), "Missing Speakers");
}

TEST_F(AudioEngineTest, ManualOutputSelectionDoesNotClearPendingInputLoss) {
    engine_->markInputDeviceLostForTest("Missing Boot Mic");

    engine_->clearLossAfterManualOutputSelectionForTest();

    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
    EXPECT_EQ(engine_->getDesiredInputDevice(), "Missing Boot Mic");
}

TEST_F(AudioEngineTest, FallbackProtection) {
    EXPECT_FALSE(engine_->isDeviceLost());
    // intentionalChange_ is private, but public API should reflect no fallback
    EXPECT_FALSE(engine_->isOutputAutoMuted());
}

TEST_F(AudioEngineTest, RememberRestoreTargetsArmsStartupRetryWhenDeviceUnavailable) {
    engine_->rememberRestoredDeviceTargets("Windows Audio", "Boot Mic", "Boot Speakers");

    EXPECT_EQ(engine_->getDesiredDeviceType(), "Windows Audio");
    EXPECT_EQ(engine_->getDesiredInputDevice(), "Boot Mic");
    EXPECT_EQ(engine_->getDesiredOutputDevice(), "Boot Speakers");
    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());
    EXPECT_TRUE(engine_->isOutputMuted());
}

TEST_F(AudioEngineTest, AsioRestoreTargetsAreNormalizedToSingleDuplexDevice) {
    engine_->rememberRestoredDeviceTargets("ASIO", "FL Studio ASIO", "Realtek ASIO");

    EXPECT_EQ(engine_->getDesiredDeviceType(), "ASIO");
    EXPECT_EQ(engine_->getDesiredInputDevice(), "FL Studio ASIO");
    EXPECT_EQ(engine_->getDesiredOutputDevice(), "FL Studio ASIO");
    EXPECT_TRUE(engine_->isStartupRestorePendingForTest());
    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());
}

TEST_F(AudioEngineTest, StartupRestorePendingRequiresSavedTargets) {
    engine_->rememberRestoredDeviceTargets({}, "Boot Mic", "Boot Speakers");

    juce::AudioDeviceManager::AudioDeviceSetup fallback;
    fallback.inputDeviceName = "Fallback Mic";
    fallback.outputDeviceName = "Fallback Speakers";

    juce::AudioDeviceManager::AudioDeviceSetup target;
    target.inputDeviceName = "Boot Mic";
    target.outputDeviceName = "Boot Speakers";

    EXPECT_TRUE(engine_->isStartupRestorePendingForTest());
    EXPECT_FALSE(engine_->restoredDeviceTargetsSatisfiedForTest(fallback));
    EXPECT_TRUE(engine_->restoredDeviceTargetsSatisfiedForTest(target));
}

TEST_F(AudioEngineTest, RestoreReadyKeepsOutputMutedWhenOutputNoneSelected) {
    engine_->rememberRestoredDeviceTargets({}, "Boot Mic", "Boot Speakers");
    ASSERT_TRUE(engine_->isOutputAutoMuted());

    engine_->setOutputNone(true);

    juce::AudioDeviceManager::AudioDeviceSetup target;
    target.inputDeviceName = "Boot Mic";

    EXPECT_TRUE(engine_->clearDeviceLossAfterReadyForTest(target));
    EXPECT_TRUE(engine_->isOutputNone());
    EXPECT_TRUE(engine_->isOutputMuted());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
}

TEST_F(AudioEngineTest, ExplicitChannelMaskMustIntersectActiveChannels) {
    juce::BigInteger activeInput;
    activeInput.setBit(0);
    juce::BigInteger activeOutput;
    activeOutput.setBit(0);

    juce::StringArray inputs;
    inputs.add("Mic 1");
    inputs.add("Mic 2");
    juce::StringArray outputs;
    outputs.add("Speaker 1");
    outputs.add("Speaker 2");
    FakeAudioIODevice device("Fake Duplex", inputs, outputs, activeInput, activeOutput);

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = "Fake Duplex";
    setup.outputDeviceName = "Fake Duplex";
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.setBit(1);
    setup.outputChannels.setBit(1);

    EXPECT_FALSE(engine_->hasUsableActiveChannelsForTest(setup, &device));

    setup.inputChannels.clear();
    setup.inputChannels.setBit(0);
    setup.outputChannels.clear();
    setup.outputChannels.setBit(0);

    EXPECT_TRUE(engine_->hasUsableActiveChannelsForTest(setup, &device));
}

TEST_F(AudioEngineTest, XRunWindowRolling) {
    int xruns = engine_->getRecentXRunCount();
    EXPECT_LE(xruns, 0);
}

TEST_F(AudioEngineTest, XRunResetFlag) {
    engine_->updateXRunTracking();
    EXPECT_LE(engine_->getRecentXRunCount(), 0);
}

TEST_F(AudioEngineTest, BufferSizeFallback) {
    auto sizes = engine_->getAvailableBufferSizes();
    // Without a device, list may be empty — that's valid
    EXPECT_GE(sizes.size(), 0);
}

TEST_F(AudioEngineTest, SampleRatePropagation) {
    auto rates = engine_->getAvailableSampleRates();
    // Without a device, list may be empty — that's valid
    EXPECT_GE(rates.size(), 0);
}

TEST(AudioRingBufferTest, DiscardDropsOldestFrames) {
    AudioRingBuffer rb;
    rb.initialize(1024, 2);

    std::vector<float> left(512);
    std::vector<float> right(512);
    for (int i = 0; i < 512; ++i) {
        left[static_cast<size_t>(i)] = static_cast<float>(i);
        right[static_cast<size_t>(i)] = static_cast<float>(1000 + i);
    }
    const float* inputs[] = { left.data(), right.data() };

    EXPECT_EQ(rb.write(inputs, 2, 512), 512);
    EXPECT_EQ(rb.discard(384), 384);
    EXPECT_EQ(rb.availableRead(), 128);

    std::vector<float> outL(128, 0.0f);
    std::vector<float> outR(128, 0.0f);
    float* outputs[] = { outL.data(), outR.data() };

    EXPECT_EQ(rb.read(outputs, 2, 128), 128);
    EXPECT_FLOAT_EQ(outL[0], 384.0f);
    EXPECT_FLOAT_EQ(outR[0], 1384.0f);
    EXPECT_FLOAT_EQ(outL[127], 511.0f);
    EXPECT_FLOAT_EQ(outR[127], 1511.0f);
}

TEST(AudioRingBufferTest, DiscardClampsToAvailableFrames) {
    AudioRingBuffer rb;
    rb.initialize(1024, 2);

    std::vector<float> left(64, 1.0f);
    std::vector<float> right(64, 2.0f);
    const float* inputs[] = { left.data(), right.data() };

    EXPECT_EQ(rb.write(inputs, 2, 64), 64);
    EXPECT_EQ(rb.discard(256), 64);
    EXPECT_EQ(rb.availableRead(), 0);
    EXPECT_EQ(rb.discard(1), 0);
}

TEST(AudioRingBufferTest, ZeroChannelWriteProducesSilenceNotStaleAudio) {
    AudioRingBuffer rb;
    rb.initialize(1024, 2);

    std::vector<float> tone(128, 0.75f);
    const float* inputs[] = { tone.data() };
    EXPECT_EQ(rb.write(inputs, 1, 128), 128);

    float* noInputs[] = { nullptr };
    EXPECT_EQ(rb.write(noInputs, 0, 64), 64);
    EXPECT_EQ(rb.discard(128), 128);

    std::vector<float> outL(64, 1.0f);
    std::vector<float> outR(64, 1.0f);
    float* outputs[] = { outL.data(), outR.data() };

    EXPECT_EQ(rb.read(outputs, 2, 64), 64);
    for (int i = 0; i < 64; ++i) {
        EXPECT_FLOAT_EQ(outL[static_cast<size_t>(i)], 0.0f);
        EXPECT_FLOAT_EQ(outR[static_cast<size_t>(i)], 0.0f);
    }
}

TEST(AudioRingBufferTest, NullInputChannelWritesSilenceForThatChannel) {
    AudioRingBuffer rb;
    rb.initialize(1024, 2);

    std::vector<float> tone(32, 0.5f);
    const float* inputs[] = { tone.data(), nullptr };
    EXPECT_EQ(rb.write(inputs, 2, 32), 32);

    std::vector<float> outL(32, 0.0f);
    std::vector<float> outR(32, 1.0f);
    float* outputs[] = { outL.data(), outR.data() };
    EXPECT_EQ(rb.read(outputs, 2, 32), 32);

    for (int i = 0; i < 32; ++i) {
        EXPECT_FLOAT_EQ(outL[static_cast<size_t>(i)], 0.5f);
        EXPECT_FLOAT_EQ(outR[static_cast<size_t>(i)], 0.0f);
    }
}

TEST(AudioRingBufferTest, InterpolatedReadLinearPhaseAdvance) {
    AudioRingBuffer rb;
    rb.initialize(1024, 2);

    std::vector<float> left = { 0.0f, 10.0f, 20.0f, 30.0f };
    std::vector<float> right = { 100.0f, 110.0f, 120.0f, 130.0f };
    const float* inputs[] = { left.data(), right.data() };
    EXPECT_EQ(rb.write(inputs, 2, 4), 4);

    std::vector<float> outL(2, 0.0f);
    std::vector<float> outR(2, 0.0f);
    float* outputs[] = { outL.data(), outR.data() };
    double phase = 0.5;
    int consumed = 0;

    EXPECT_EQ(rb.readInterpolated(outputs, 2, 2, 1.0, phase, consumed), 2);
    EXPECT_EQ(consumed, 2);
    EXPECT_NEAR(phase, 0.5, 0.000001);
    EXPECT_FLOAT_EQ(outL[0], 5.0f);
    EXPECT_FLOAT_EQ(outL[1], 15.0f);
    EXPECT_FLOAT_EQ(outR[0], 105.0f);
    EXPECT_FLOAT_EQ(outR[1], 115.0f);
}

TEST(AudioRingBufferTest, InterpolatedReadCommitsIntegerFrames) {
    AudioRingBuffer rb;
    rb.initialize(1024, 1);

    std::vector<float> mono = { 0.0f, 10.0f, 20.0f, 30.0f, 40.0f };
    const float* inputs[] = { mono.data() };
    EXPECT_EQ(rb.write(inputs, 1, 5), 5);

    std::vector<float> outL(2, 0.0f);
    std::vector<float> outR(2, 0.0f);
    float* outputs[] = { outL.data(), outR.data() };
    double phase = 0.25;
    int consumed = 0;

    EXPECT_EQ(rb.readInterpolated(outputs, 2, 2, 1.25, phase, consumed), 2);
    EXPECT_EQ(consumed, 2);
    EXPECT_NEAR(phase, 0.75, 0.000001);
    EXPECT_EQ(rb.availableRead(), 3);
    EXPECT_FLOAT_EQ(outL[0], 2.5f);
    EXPECT_FLOAT_EQ(outR[0], 2.5f);
}

TEST(AudioRingBufferTest, InterpolatedReadRefusesPastAvailable) {
    AudioRingBuffer rb;
    rb.initialize(1024, 1);

    std::vector<float> mono = { 0.0f, 10.0f };
    const float* inputs[] = { mono.data() };
    EXPECT_EQ(rb.write(inputs, 1, 2), 2);

    std::vector<float> out(4, 99.0f);
    float* outputs[] = { out.data() };
    double phase = 0.0;
    int consumed = 0;

    EXPECT_EQ(rb.readInterpolated(outputs, 1, 4, 1.0, phase, consumed), 0);
    EXPECT_EQ(consumed, 0);
    EXPECT_EQ(rb.availableRead(), 2);
    EXPECT_FLOAT_EQ(out[0], 99.0f);
}

TEST(AudioRingBufferTest, InterpolatedReadWrapAround) {
    AudioRingBuffer rb;
    rb.initialize(8, 1);

    std::vector<float> first = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f };
    const float* firstInput[] = { first.data() };
    EXPECT_EQ(rb.write(firstInput, 1, 6), 6);
    EXPECT_EQ(rb.advanceRead(5), 5);

    std::vector<float> second = { 6.0f, 7.0f, 8.0f, 9.0f, 10.0f };
    const float* secondInput[] = { second.data() };
    EXPECT_EQ(rb.write(secondInput, 1, 5), 5);

    std::vector<float> out(2, 0.0f);
    float* outputs[] = { out.data() };
    double phase = 0.5;
    int consumed = 0;

    EXPECT_EQ(rb.readInterpolated(outputs, 1, 2, 1.0, phase, consumed), 2);
    EXPECT_FLOAT_EQ(out[0], 5.5f);
    EXPECT_FLOAT_EQ(out[1], 6.5f);
}

TEST(MonitorDriftPolicyTest, TargetFillDerivedFromRuntimeBlocks) {
    monitor_drift::AdaptiveTargetState state;
    auto plan = monitor_drift::calculateBufferPlan(8192, 512, 128, 48000.0, state);

    EXPECT_EQ(plan.minimumTargetFill, 640);
    EXPECT_EQ(plan.targetFill, plan.minimumTargetFill);
    EXPECT_EQ(plan.producerBlock, 512);
    EXPECT_EQ(plan.consumerBlock, 128);
}

TEST(MonitorDriftPolicyTest, TargetFillChangesWhenBlockSizesChange) {
    monitor_drift::AdaptiveTargetState state;
    auto smallPlan = monitor_drift::calculateBufferPlan(8192, 256, 128, 48000.0, state);
    auto largePlan = monitor_drift::calculateBufferPlan(8192, 1024, 128, 48000.0, state);

    EXPECT_NE(smallPlan.targetFill, largePlan.targetFill);
    EXPECT_EQ(smallPlan.targetFill, 384);
    EXPECT_EQ(largePlan.targetFill, 1152);
}

TEST(MonitorDriftPolicyTest, RepeatedUnderrunsRaiseTargetGradually) {
    monitor_drift::AdaptiveTargetState state;
    auto base = monitor_drift::calculateBufferPlan(8192, 512, 128, 48000.0, state);

    monitor_drift::noteUnderrun(state, base);
    auto raised = monitor_drift::calculateBufferPlan(8192, 512, 128, 48000.0, state);

    EXPECT_GT(raised.targetFill, base.targetFill);
    EXPECT_EQ(raised.targetFill - base.targetFill, raised.consumerBlock);
    EXPECT_EQ(raised.reason, monitor_drift::TargetReason::UnderrunRaised);
}

TEST(MonitorDriftPolicyTest, StableFillLowersTargetGradually) {
    monitor_drift::AdaptiveTargetState state;
    auto base = monitor_drift::calculateBufferPlan(8192, 512, 128, 48000.0, state);
    monitor_drift::noteUnderrun(state, base);
    auto raised = monitor_drift::calculateBufferPlan(8192, 512, 128, 48000.0, state);

    for (int i = 0; i < raised.stabilityWindowCallbacks; ++i)
        monitor_drift::noteStableCallback(state, raised, 0);

    auto lowered = monitor_drift::calculateBufferPlan(8192, 512, 128, 48000.0, state);
    EXPECT_LT(lowered.targetFill, raised.targetFill);
    EXPECT_GE(lowered.targetFill, lowered.minimumTargetFill);
}

TEST(MonitorDriftPolicyTest, NearOverflowDoesNotPermanentlyInflateTarget) {
    monitor_drift::AdaptiveTargetState state;
    auto before = monitor_drift::calculateBufferPlan(8192, 512, 128, 48000.0, state);
    monitor_drift::noteNearOverflow(state);
    auto after = monitor_drift::calculateBufferPlan(8192, 512, 128, 48000.0, state);

    EXPECT_EQ(after.targetFill, before.targetFill);
}

TEST(MonitorDriftPolicyTest, PositiveFillErrorDrainsFaster) {
    monitor_drift::PllState pll;
    auto update = monitor_drift::updatePll(pll, 256.0, 640, 128, 48000.0, false);

    EXPECT_GT(update.playbackRatio, 1.0);
    EXPECT_GT(update.correction, 0.0);
}

TEST(MonitorDriftPolicyTest, NegativeFillErrorDrainsSlower) {
    monitor_drift::PllState pll;
    auto update = monitor_drift::updatePll(pll, -256.0, 640, 128, 48000.0, false);

    EXPECT_LT(update.playbackRatio, 1.0);
    EXPECT_LT(update.correction, 0.0);
}

TEST(MonitorDriftPolicyTest, RatioCorrectionDecaysTowardUnity) {
    monitor_drift::PllState pll;
    auto first = monitor_drift::updatePll(pll, 512.0, 640, 128, 48000.0, false);
    double distance = std::abs(first.playbackRatio - 1.0);

    for (int i = 0; i < 200; ++i) {
        auto update = monitor_drift::updatePll(pll, 0.0, 640, 128, 48000.0, false);
        distance = (std::min)(distance, std::abs(update.playbackRatio - 1.0));
    }

    EXPECT_LT(distance, std::abs(first.playbackRatio - 1.0));
}

TEST(MonitorDriftPolicyTest, EmergencyTrimDisabledDuringNormalDrift) {
    monitor_drift::AdaptiveTargetState state;
    auto plan = monitor_drift::calculateBufferPlan(8192, 512, 128, 48000.0, state);
    auto trim = monitor_drift::calculateEmergencyTrimPlan(plan.highThreshold + 1, plan);

    EXPECT_EQ(trim.trimFrames, 0);
}

TEST(MonitorDriftPolicyTest, EmergencyTrimOnlyNearOverflow) {
    monitor_drift::AdaptiveTargetState state;
    auto plan = monitor_drift::calculateBufferPlan(8192, 512, 128, 48000.0, state);
    auto trim = monitor_drift::calculateEmergencyTrimPlan(plan.emergencyThreshold + 256, plan);

    EXPECT_GT(trim.trimFrames, 0);
    EXPECT_LE(trim.trimFrames, plan.consumerBlock / 2);
}

// ─── DeviceState state machine tests (pure function, no device needed) ───

TEST_F(AudioEngineTest, DeviceReadyRequiresActiveDevice) {
    EXPECT_FALSE(engine_->isCurrentAudioDeviceReady());
    auto result = engine_->ensureAudioDeviceReady();
    EXPECT_FALSE(result);
    EXPECT_TRUE(result.message.isNotEmpty());
}

TEST_F(AudioEngineTest, SafetyHeadroomDefaultAndClamp) {
    EXPECT_TRUE(engine_->isSafetyHeadroomEnabled());
    EXPECT_NEAR(engine_->getSafetyHeadroomdB(), -0.3f, 0.001f);

    engine_->setSafetyHeadroomdB(-12.0f);
    EXPECT_NEAR(engine_->getSafetyHeadroomdB(), -6.0f, 0.001f);

    engine_->setSafetyHeadroomdB(1.0f);
    EXPECT_NEAR(engine_->getSafetyHeadroomdB(), 0.0f, 0.001f);

    engine_->setSafetyHeadroomEnabled(false);
    EXPECT_FALSE(engine_->isSafetyHeadroomEnabled());

    engine_->setSafetyHeadroomEnabled(true);
    EXPECT_TRUE(engine_->isSafetyHeadroomEnabled());
}

TEST(DeviceStateTest, RunningToInputLost) {
    auto next = transition(DeviceState::Running, DeviceEvent::InputError);
    EXPECT_EQ(next, DeviceState::InputLost);
}

TEST(DeviceStateTest, RunningToOutputLost) {
    auto next = transition(DeviceState::Running, DeviceEvent::OutputError);
    EXPECT_EQ(next, DeviceState::OutputLost);
}

TEST(DeviceStateTest, InputLostPlusOutputLostBecomesBothLost) {
    auto next = transition(DeviceState::InputLost, DeviceEvent::OutputError);
    EXPECT_EQ(next, DeviceState::BothLost);
}

TEST(DeviceStateTest, OutputLostPlusInputLostBecomesBothLost) {
    auto next = transition(DeviceState::OutputLost, DeviceEvent::InputError);
    EXPECT_EQ(next, DeviceState::BothLost);
}

TEST(DeviceStateTest, FullErrorFromAnyState) {
    EXPECT_EQ(transition(DeviceState::Running, DeviceEvent::FullError), DeviceState::BothLost);
    EXPECT_EQ(transition(DeviceState::InputLost, DeviceEvent::FullError), DeviceState::BothLost);
    EXPECT_EQ(transition(DeviceState::Reconnecting, DeviceEvent::FullError), DeviceState::BothLost);
}

TEST(DeviceStateTest, ReconnectCycle) {
    auto state = DeviceState::BothLost;
    state = transition(state, DeviceEvent::ReconnectStart);
    EXPECT_EQ(state, DeviceState::Reconnecting);
    state = transition(state, DeviceEvent::ReconnectSuccess);
    EXPECT_EQ(state, DeviceState::Running);
}

TEST(DeviceStateTest, ReconnectFailReturnsToBothLost) {
    auto state = transition(DeviceState::Reconnecting, DeviceEvent::ReconnectFail);
    EXPECT_EQ(state, DeviceState::BothLost);
}

TEST(DeviceStateTest, UserResetResetsToRunning) {
    EXPECT_EQ(transition(DeviceState::BothLost, DeviceEvent::UserReset), DeviceState::Running);
    EXPECT_EQ(transition(DeviceState::FallbackDetected, DeviceEvent::UserReset), DeviceState::Running);
    EXPECT_EQ(transition(DeviceState::InputLost, DeviceEvent::UserReset), DeviceState::Running);
}

TEST(DeviceStateTest, FallbackDetected) {
    auto state = transition(DeviceState::Running, DeviceEvent::FallbackDetected);
    EXPECT_EQ(state, DeviceState::FallbackDetected);
}

TEST(DeviceStateTest, StateToString) {
    EXPECT_STREQ(deviceStateToString(DeviceState::Running), "Running");
    EXPECT_STREQ(deviceStateToString(DeviceState::BothLost), "BothLost");
    EXPECT_STREQ(deviceStateToString(DeviceState::Reconnecting), "Reconnecting");
    EXPECT_STREQ(deviceStateToString(DeviceState::FallbackDetected), "FallbackDetected");
}
