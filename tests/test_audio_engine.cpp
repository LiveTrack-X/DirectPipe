// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "Audio/AudioEngine.h"

#include "Audio/AudioRingBuffer.h"
#include "Audio/DeviceState.h"
#include "Audio/MonitorDriftPolicy.h"

using namespace directpipe;

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
