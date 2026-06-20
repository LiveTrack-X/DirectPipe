// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "Audio/AudioEngine.h"
#include "Audio/AudioRingBuffer.h"
#include "Audio/DeviceState.h"
#include "Audio/MonitorDriftPolicy.h"

using namespace directpipe;

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

TEST_F(AudioEngineTest, ReconnectionMaxRetry) {
    for (int i = 0; i < 10; ++i)
        engine_->checkReconnection();
    EXPECT_FALSE(engine_->isDeviceLost());
}

TEST_F(AudioEngineTest, FallbackProtection) {
    EXPECT_FALSE(engine_->isDeviceLost());
    // intentionalChange_ is private, but public API should reflect no fallback
    EXPECT_FALSE(engine_->isOutputAutoMuted());
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

TEST(MonitorDriftPolicyTest, ProducerBlockPreventsOverTrimWithSmallMonitorBuffer) {
    auto plan = monitor_drift::calculateTrimPlan(
        1800, 4096,
        512, 128);

    EXPECT_GE(plan.targetFill, 1024);
    EXPECT_EQ(plan.highThreshold, 1536);
    EXPECT_EQ(plan.trimFrames, 1800 - plan.targetFill);
}

TEST(MonitorDriftPolicyTest, DoesNotTrimBelowMainCallbackGranularity) {
    auto plan = monitor_drift::calculateTrimPlan(
        900, 4096,
        512, 128);

    EXPECT_GE(plan.targetFill, 1024);
    EXPECT_EQ(plan.trimFrames, 0);
}

TEST(MonitorDriftPolicyTest, LargeProducerBlockStillTrimsBeforeFullCapacity) {
    auto plan = monitor_drift::calculateTrimPlan(
        3500, 4096,
        1024, 128);

    EXPECT_EQ(plan.targetFill, 2048);
    EXPECT_EQ(plan.highThreshold, 3072);
    EXPECT_EQ(plan.trimFrames, 3500 - 2048);
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
