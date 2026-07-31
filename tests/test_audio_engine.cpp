// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "Audio/AudioEngine.h"

#include "Audio/AudioRingBuffer.h"
#include "Audio/DeviceState.h"
#include "Audio/MonitorDriftPolicy.h"
#include "Platform/EndpointChangeWatcher.h"
#include "UI/AudioSettingsPolicy.h"

#if JUCE_WINDOWS
#include <windows.h>
#endif

using namespace directpipe;

namespace directpipe::audio_device_recovery_detail {
juce::String initialiseWithDefaultDeviceFallbacks(
    juce::AudioDeviceManager& deviceManager,
    const juce::String& logContext);
juce::String forceReopenAudioDevice(
    juce::AudioDeviceManager& deviceManager,
    const juce::AudioDeviceManager::AudioDeviceSetup& setup);
bool monitorDeviceConflictsWithExclusiveMainOutput(
    const juce::String& deviceType,
    const juce::String& mainOutputDevice,
    const juce::String& monitorDevice);
bool endpointEventIsSuppressed(double nowMs, double suppressedUntilMs) noexcept;
int reconnectCooldownAfterRecovery(bool recovered) noexcept;
bool savedTargetMismatchesActualDevice(const juce::String& desiredDevice,
                                       const juce::String& actualDevice,
                                       bool targetDisabled) noexcept;
bool restoredDeviceTargetsMatch(const juce::String& actualInput,
                                const juce::String& actualOutput,
                                const juce::String& desiredInput,
                                const juce::String& desiredOutput,
                                bool outputDisabled) noexcept;
bool shouldSuspendMonitorBeforeExclusiveOpen(
    const juce::String& deviceType,
    const juce::String& targetOutputDevice,
    const juce::String& monitorDevice,
    bool suspendForAnyExclusiveTypeSwitch);
bool prepareOutputDeviceChangeChannels(
    juce::AudioDeviceManager::AudioDeviceSetup& setup);
bool seedCompatibleWindowsDriverSnapshot(
    const juce::String& currentType,
    const juce::String& targetType,
    bool targetHasSnapshot,
    const DriverTypeSnapshot& currentSnapshot,
    DriverTypeSnapshot& seededSnapshot);
bool snapshotEndpointsAvailable(
    const DriverTypeSnapshot& snapshot,
    const juce::StringArray& availableInputs,
    const juce::StringArray& availableOutputs);
juce::String restoreDriverSnapshot(
    juce::AudioDeviceManager& deviceManager,
    const DriverTypeSnapshot& snapshot);
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
    juce::StringArray openedDeviceNames;
    juce::StringArray openRequestLog;
    juce::String typeName { "Recovery Test" };
    juce::String deviceName { "Recovery Device" };
    juce::StringArray deviceNames;
    juce::String failDeviceName;
    juce::String noActiveChannelsDeviceName;
    juce::String partialInputChannelsDeviceName;
    juce::String partialOutputChannelsDeviceName;
    juce::String zeroSampleRateDeviceName;
    juce::String zeroBufferSizeDeviceName;
    bool failMultiChannelOpen = false;
    bool failEveryOpen = false;
    bool reportNoActiveChannels = false;
    double failSampleRate = 0.0;
    int failBufferSize = 0;
    int failInputFirstChannel = -1;
    int failOutputFirstChannel = -1;
    double reportedSampleRate = 0.0;
    int reportedBufferSize = 0;
    int reportedInputLatency = 0;
    int reportedOutputLatency = 0;
    int availableInputChannels = 2;
    int availableOutputChannels = 2;
    bool separateInputsAndOutputs = false;
};

class ManagedFakeAudioIODevice final : public juce::AudioIODevice {
public:
    ManagedFakeAudioIODevice(std::shared_ptr<ManagedFakeDeviceStats> stats,
                             const juce::String& outputDeviceName,
                             const juce::String& inputDeviceName)
        : juce::AudioIODevice(
              outputDeviceName.isNotEmpty() ? outputDeviceName : inputDeviceName,
              stats->typeName),
          outputDeviceName_(outputDeviceName),
          inputDeviceName_(inputDeviceName),
          stats_(std::move(stats))
    {
    }

    juce::StringArray getOutputChannelNames() override
    {
        juce::StringArray names;
        for (int i = 0; i < stats_->availableOutputChannels; ++i)
            names.add("Out " + juce::String(i + 1));
        return names;
    }

    juce::StringArray getInputChannelNames() override
    {
        juce::StringArray names;
        for (int i = 0; i < stats_->availableInputChannels; ++i)
            names.add("In " + juce::String(i + 1));
        return names;
    }
    juce::Array<double> getAvailableSampleRates() override { return { 44100.0, 48000.0 }; }
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
        stats_->openedDeviceNames.add(
            outputDeviceName_.isNotEmpty() ? outputDeviceName_ : inputDeviceName_);
        stats_->openRequestLog.add(
            "inFirst=" + juce::String(inputChannels.findNextSetBit(0))
            + " outFirst=" + juce::String(outputChannels.findNextSetBit(0))
            + " sr=" + juce::String(sampleRate)
            + " bs=" + juce::String(bufferSizeSamples));
        if (inputDeviceName_ == stats_->failDeviceName
            || outputDeviceName_ == stats_->failDeviceName)
            return "test selected device refuses open";
        if (stats_->failEveryOpen)
            return "test device refuses every open";
        if (stats_->failSampleRate > 0.0
            && std::abs(sampleRate - stats_->failSampleRate) < 1.0)
            return "test device refuses requested sample rate";
        if (stats_->failBufferSize > 0
            && bufferSizeSamples == stats_->failBufferSize)
            return "test device refuses requested buffer size";
        if (stats_->failInputFirstChannel >= 0
            && inputChannels.findNextSetBit(0) == stats_->failInputFirstChannel)
            return "test device refuses requested input channel";
        if (stats_->failOutputFirstChannel >= 0
            && outputChannels.findNextSetBit(0) == stats_->failOutputFirstChannel)
            return "test device refuses requested output channel";
        if (inputChannels.getHighestBit() >= stats_->availableInputChannels)
            return "test device input channel unavailable";
        if (outputChannels.getHighestBit() >= stats_->availableOutputChannels)
            return "test device output channel unavailable";
        if (stats_->failMultiChannelOpen && (inputCount > 1 || outputCount > 1))
            return "test device requires mono input/output";

        const bool reportNoActiveChannels =
            stats_->reportNoActiveChannels
            || inputDeviceName_ == stats_->noActiveChannelsDeviceName
            || outputDeviceName_ == stats_->noActiveChannelsDeviceName;
        activeInput_ = reportNoActiveChannels ? juce::BigInteger() : inputChannels;
        activeOutput_ = reportNoActiveChannels ? juce::BigInteger() : outputChannels;
        if (inputDeviceName_ == stats_->partialInputChannelsDeviceName
            || outputDeviceName_ == stats_->partialInputChannelsDeviceName)
            activeInput_.clearBit(1);
        if (inputDeviceName_ == stats_->partialOutputChannelsDeviceName
            || outputDeviceName_ == stats_->partialOutputChannelsDeviceName)
            activeOutput_.clearBit(1);
        const bool reportZeroSampleRate =
            inputDeviceName_ == stats_->zeroSampleRateDeviceName
            || outputDeviceName_ == stats_->zeroSampleRateDeviceName;
        const bool reportZeroBufferSize =
            inputDeviceName_ == stats_->zeroBufferSizeDeviceName
            || outputDeviceName_ == stats_->zeroBufferSizeDeviceName;
        sampleRate_ = reportZeroSampleRate
            ? 0.0
            : stats_->reportedSampleRate > 0.0
            ? stats_->reportedSampleRate
            : sampleRate;
        bufferSize_ = reportZeroBufferSize
            ? 0
            : stats_->reportedBufferSize > 0
            ? stats_->reportedBufferSize
            : bufferSizeSamples;
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
    int getOutputLatencyInSamples() override { return stats_->reportedOutputLatency; }
    int getInputLatencyInSamples() override { return stats_->reportedInputLatency; }

private:
    juce::String outputDeviceName_;
    juce::String inputDeviceName_;
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
        : juce::AudioIODeviceType(stats->typeName),
          stats_(std::move(stats))
    {
    }

    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool) const override
    {
        return stats_->deviceNames.isEmpty()
            ? juce::StringArray { stats_->deviceName }
            : stats_->deviceNames;
    }
    int getDefaultDeviceIndex(bool) const override { return 0; }
    int getIndexOfDevice(juce::AudioIODevice* device, bool) const override
    {
        return device ? getDeviceNames(false).indexOf(device->getName()) : -1;
    }
    bool hasSeparateInputsAndOutputs() const override
    {
        return stats_->separateInputsAndOutputs;
    }
    juce::AudioIODevice* createDevice(const juce::String& outputDeviceName,
                                      const juce::String& inputDeviceName) override
    {
        const auto names = getDeviceNames(false);
        const bool outputValid =
            outputDeviceName.isEmpty() || names.contains(outputDeviceName);
        const bool inputValid =
            inputDeviceName.isEmpty() || names.contains(inputDeviceName);
        if (outputValid && inputValid
            && (outputDeviceName.isNotEmpty() || inputDeviceName.isNotEmpty())) {
            return new ManagedFakeAudioIODevice(
                stats_, outputDeviceName, inputDeviceName);
        }
        return nullptr;
    }

private:
    std::shared_ptr<ManagedFakeDeviceStats> stats_;
};

void addManagedFakeDeviceType(juce::AudioDeviceManager& manager,
                              const std::shared_ptr<ManagedFakeDeviceStats>& stats,
                              bool makeCurrent = true)
{
    manager.addAudioDeviceType(std::make_unique<ManagedFakeAudioIODeviceType>(stats));
    if (makeCurrent)
        manager.setCurrentAudioDeviceType(stats->typeName, true);
}

juce::AudioDeviceManager::AudioDeviceSetup managedDuplexSetup(
    const juce::String& deviceName,
    double sampleRate = 48000.0,
    int bufferSize = 128)
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = deviceName;
    setup.outputDeviceName = deviceName;
    setup.sampleRate = sampleRate;
    setup.bufferSize = bufferSize;
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.setRange(0, 2, true);
    setup.outputChannels.setRange(0, 2, true);
    return setup;
}

#if JUCE_WINDOWS
bool pumpMessagesUntil(const std::atomic<bool>& completed,
                       std::chrono::milliseconds timeout = std::chrono::seconds(2))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!completed.load(std::memory_order_acquire)
           && std::chrono::steady_clock::now() < deadline) {
        MSG message;
        bool dispatched = false;
        while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessage(&message);
            dispatched = true;
        }
        if (!dispatched)
            std::this_thread::yield();
    }
    return completed.load(std::memory_order_acquire);
}
#endif

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

TEST(LatencyMonitorTest, UsesReportedDeviceLatencyAndFallsBackPerDirection)
{
    LatencyMonitor monitor;

    monitor.reset(48000.0, 256, 480, 960);
    EXPECT_EQ(monitor.getInputLatencySamples(), 480);
    EXPECT_EQ(monitor.getOutputLatencySamples(), 960);
    EXPECT_NEAR(monitor.getInputLatencyMs(), 10.0, 0.0001);
    EXPECT_NEAR(monitor.getOutputLatencyMs(), 20.0, 0.0001);
    EXPECT_NEAR(monitor.getTotalLatencyVirtualMicMs(), 30.0, 0.0001);

    // Invalid reports fall back independently, not as an all-or-nothing pair.
    monitor.reset(48000.0, 256, 480, 0);
    EXPECT_EQ(monitor.getInputLatencySamples(), 480);
    EXPECT_EQ(monitor.getOutputLatencySamples(), 256);
    EXPECT_NEAR(monitor.getTotalLatencyVirtualMicMs(),
                static_cast<double>(736) / 48000.0 * 1000.0, 0.0001);

    monitor.reset(48000.0, 256, -1, -1);
    EXPECT_EQ(monitor.getInputLatencySamples(), 256);
    EXPECT_EQ(monitor.getOutputLatencySamples(), 256);
}

TEST(LatencyMonitorTest, CallbackExecutionRemainsDiagnosticOnly)
{
    LatencyMonitor monitor;
    monitor.reset(48000.0, 256, 480, 960);
    const double devicePathBefore = monitor.getTotalLatencyVirtualMicMs();

    monitor.markCallbackStart();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    monitor.markCallbackEnd();

    EXPECT_GT(monitor.getProcessingTimeMs(), 0.0);
    EXPECT_DOUBLE_EQ(monitor.getTotalLatencyVirtualMicMs(), devicePathBefore);
}

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

TEST_F(AudioEngineTest, DeviceStartPublishesReportedLatencyToMonitor)
{
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->reportedInputLatency = 384;
    stats->reportedOutputLatency = 768;
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);

    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName, 48000.0, 128), true).isEmpty());
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceAboutToStart(manager.getCurrentAudioDevice());

    auto& latency = engine_->getLatencyMonitor();
    EXPECT_EQ(latency.getInputLatencySamples(), 384);
    EXPECT_EQ(latency.getOutputLatencySamples(), 768);
    EXPECT_NEAR(latency.getTotalLatencyVirtualMicMs(), 24.0, 0.0001);
}

TEST_F(AudioEngineTest, MonoCallbackAveragesSelectedPairAndDuplicatesToBothOutputs)
{
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName, 48000.0, 128), true).isEmpty());
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);

    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceAboutToStart(manager.getCurrentAudioDevice());
    engine_->setChannelMode(1);
    engine_->getSafetyLimiter().setEnabled(false);
    engine_->setSafetyHeadroomEnabled(false);

    std::vector<float> left { 0.1f, 0.2f, 0.3f, 0.4f };
    std::vector<float> right { 0.5f, 0.6f, 0.7f, 0.8f };
    const float* inputs[] = { left.data(), right.data() };
    std::vector<float> outputLeft(left.size(), 0.0f);
    std::vector<float> outputRight(left.size(), 0.0f);
    float* outputs[] = { outputLeft.data(), outputRight.data() };
    juce::AudioIODeviceCallbackContext context;

    std::thread audioThread([&] {
        callback.audioDeviceIOCallbackWithContext(
            inputs, 2, outputs, 2, static_cast<int>(left.size()), context);
    });
    audioThread.join();

    for (size_t i = 0; i < left.size(); ++i) {
        const float expected = (left[i] + right[i]) * 0.5f;
        EXPECT_NEAR(outputLeft[i], expected, 0.000001f);
        EXPECT_NEAR(outputRight[i], expected, 0.000001f);
    }
}

TEST_F(AudioEngineTest, SparseSelectedPairMapsToInternalStereoAndBackToSelectedOutputs)
{
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName, 48000.0, 128), true).isEmpty());
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);

    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceAboutToStart(manager.getCurrentAudioDevice());
    engine_->setChannelMode(2);
    engine_->getSafetyLimiter().setEnabled(false);
    engine_->setSafetyHeadroomEnabled(false);

    std::vector<float> left { 0.1f, 0.2f, 0.3f, 0.4f };
    std::vector<float> right { 0.5f, 0.6f, 0.7f, 0.8f };
    const float* inputs[] = { nullptr, nullptr, left.data(), right.data() };
    std::vector<float> outputLeft(left.size(), 0.0f);
    std::vector<float> outputRight(left.size(), 0.0f);
    float* outputs[] = { nullptr, nullptr, outputLeft.data(), outputRight.data() };
    juce::AudioIODeviceCallbackContext context;

    std::thread audioThread([&] {
        callback.audioDeviceIOCallbackWithContext(
            inputs, 4, outputs, 4, static_cast<int>(left.size()), context);
    });
    audioThread.join();

    EXPECT_EQ(outputLeft, left);
    EXPECT_EQ(outputRight, right);
}

TEST_F(AudioEngineTest, SparseSelectedPairUsesInputFrontDualMono)
{
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName, 48000.0, 128), true).isEmpty());
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);

    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceAboutToStart(manager.getCurrentAudioDevice());
    engine_->setChannelMode(1);
    engine_->getSafetyLimiter().setEnabled(false);
    engine_->setSafetyHeadroomEnabled(false);

    std::vector<float> left { 0.1f, 0.2f, 0.3f, 0.4f };
    std::vector<float> right { 0.5f, 0.6f, 0.7f, 0.8f };
    const float* inputs[] = { nullptr, nullptr, left.data(), right.data() };
    std::vector<float> outputLeft(left.size(), 0.0f);
    std::vector<float> outputRight(left.size(), 0.0f);
    float* outputs[] = { nullptr, nullptr, outputLeft.data(), outputRight.data() };
    juce::AudioIODeviceCallbackContext context;

    std::thread audioThread([&] {
        callback.audioDeviceIOCallbackWithContext(
            inputs, 4, outputs, 4, static_cast<int>(left.size()), context);
    });
    audioThread.join();

    for (size_t i = 0; i < left.size(); ++i) {
        const float expected = (left[i] + right[i]) * 0.5f;
        EXPECT_NEAR(outputLeft[i], expected, 0.000001f);
        EXPECT_NEAR(outputRight[i], expected, 0.000001f);
    }
}

TEST_F(AudioEngineTest, MonoCallbackIgnoresInputsBeyondSelectedPair)
{
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName, 48000.0, 128), true).isEmpty());
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);

    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceAboutToStart(manager.getCurrentAudioDevice());
    engine_->setChannelMode(1);
    engine_->getSafetyLimiter().setEnabled(false);
    engine_->setSafetyHeadroomEnabled(false);

    std::vector<float> left { 0.1f, 0.2f, 0.3f, 0.4f };
    std::vector<float> right { 0.5f, 0.6f, 0.7f, 0.8f };
    std::vector<float> unrelated { 1.0f, 1.0f, 1.0f, 1.0f };
    const float* inputs[] = { left.data(), right.data(), unrelated.data() };
    std::vector<float> outputLeft(left.size(), 0.0f);
    std::vector<float> outputRight(left.size(), 0.0f);
    float* outputs[] = { outputLeft.data(), outputRight.data() };
    juce::AudioIODeviceCallbackContext context;

    std::thread audioThread([&] {
        callback.audioDeviceIOCallbackWithContext(
            inputs, 3, outputs, 2, static_cast<int>(left.size()), context);
    });
    audioThread.join();

    for (size_t i = 0; i < left.size(); ++i) {
        const float expected = (left[i] + right[i]) * 0.5f;
        EXPECT_NEAR(outputLeft[i], expected, 0.000001f);
        EXPECT_NEAR(outputRight[i], expected, 0.000001f);
    }
}

TEST_F(AudioEngineTest, ExclusiveMainOutputRejectsTheSameMonitorDevice) {
    EXPECT_TRUE(audio_device_recovery_detail::monitorDeviceConflictsWithExclusiveMainOutput(
        "Windows Audio (Exclusive Mode)", "Line(3- AG06/AG03)", "Line(3- AG06/AG03)"));
    EXPECT_TRUE(audio_device_recovery_detail::monitorDeviceConflictsWithExclusiveMainOutput(
        "ASIO", "TOPPING Pro USB Audio Device", "TOPPING Pro USB Audio Device"));
    EXPECT_FALSE(audio_device_recovery_detail::monitorDeviceConflictsWithExclusiveMainOutput(
        "Windows Audio", "Line(3- AG06/AG03)", "Line(3- AG06/AG03)"));
    EXPECT_FALSE(audio_device_recovery_detail::monitorDeviceConflictsWithExclusiveMainOutput(
        "Windows Audio (Exclusive Mode)", "CABLE Input", "Speakers"));
}

TEST_F(AudioEngineTest, OutputChangePreservesDriverDefaultInputPolicy)
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = "Native Mono Input";
    setup.outputDeviceName = "Replacement Output";
    setup.useDefaultInputChannels = true;
    setup.inputChannels.clear();
    setup.useDefaultOutputChannels = true;
    setup.outputChannels.clear();

    const bool synthesizedInputPair =
        audio_device_recovery_detail::prepareOutputDeviceChangeChannels(setup);

    EXPECT_FALSE(synthesizedInputPair);
    EXPECT_TRUE(setup.useDefaultInputChannels);
    EXPECT_TRUE(setup.inputChannels.isZero());
    EXPECT_FALSE(setup.useDefaultOutputChannels);
    EXPECT_EQ(setup.outputChannels.countNumberOfSetBits(), 2);
}

TEST_F(AudioEngineTest, GenuineMonoInputSurvivesOutputDeviceChange)
{
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    stats->deviceNames = {
        "Native Mono Input",
        "Initial Output",
        "Replacement Output",
    };
    stats->availableInputChannels = 1;
    stats->availableOutputChannels = 2;

    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(audio_device_recovery_detail::initialiseWithDefaultDeviceFallbacks(
        manager, "test mono input").isEmpty());
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);

    juce::AudioDeviceManager::AudioDeviceSetup before;
    manager.getAudioDeviceSetup(before);
    ASSERT_TRUE(before.useDefaultInputChannels);
    ASSERT_EQ(before.inputChannels.countNumberOfSetBits(), 1);

    const auto result = engine_->setOutputDevice("Replacement Output");

    ASSERT_TRUE(result) << result.message.toStdString();
    juce::AudioDeviceManager::AudioDeviceSetup after;
    manager.getAudioDeviceSetup(after);
    EXPECT_EQ(after.outputDeviceName, "Replacement Output");
    EXPECT_TRUE(after.useDefaultInputChannels);
    EXPECT_EQ(after.inputChannels.countNumberOfSetBits(), 1);
    EXPECT_TRUE(after.inputChannels[0]);
    ASSERT_FALSE(stats->openAttempts.empty());
    EXPECT_EQ(stats->openAttempts.back(), std::make_pair(1, 2));
}

TEST_F(AudioEngineTest, SelfReopenEndpointEventsHaveABoundedSuppressionWindow) {
    EXPECT_TRUE(audio_device_recovery_detail::endpointEventIsSuppressed(1000.0, 2000.0));
    EXPECT_FALSE(audio_device_recovery_detail::endpointEventIsSuppressed(2000.0, 2000.0));
    EXPECT_FALSE(audio_device_recovery_detail::endpointEventIsSuppressed(3000.0, 0.0));
}

TEST_F(AudioEngineTest, ExclusiveOpenSuspendsOnlyTheMonitorThatCanBlockIt) {
    EXPECT_TRUE(audio_device_recovery_detail::shouldSuspendMonitorBeforeExclusiveOpen(
        "ASIO", {}, "TOPPING Pro USB Audio Device", true));
    EXPECT_TRUE(audio_device_recovery_detail::shouldSuspendMonitorBeforeExclusiveOpen(
        "Windows Audio (Exclusive Mode)", "Speakers", "Speakers", false));
    EXPECT_FALSE(audio_device_recovery_detail::shouldSuspendMonitorBeforeExclusiveOpen(
        "Windows Audio (Exclusive Mode)", "Speakers", "Headphones", false));
    EXPECT_FALSE(audio_device_recovery_detail::shouldSuspendMonitorBeforeExclusiveOpen(
        "Windows Audio", "Speakers", "Speakers", true));
}

TEST_F(AudioEngineTest, AvailableInputRecoveryUsesDistinctPlaceholderSelection) {
    const juce::StringArray outputs {
        "Speakers",
        "CABLE Input(VB-Audio Virtual Cable)"
    };

    const auto selection = audio_settings_detail::makeRecoverySelection(
        "CABLE Input(VB-Audio Virtual Cable)", outputs, 1);

    EXPECT_TRUE(selection.desiredDeviceAvailable);
    EXPECT_EQ(selection.desiredDeviceId, 2);
    EXPECT_EQ(selection.placeholderId, 3);
    EXPECT_NE(selection.placeholderId, selection.desiredDeviceId);
    EXPECT_EQ(selection.placeholderText,
              "CABLE Input(VB-Audio Virtual Cable) (Reconnect)");
}

TEST_F(AudioEngineTest, AvailableOutputRecoveryUsesDistinctPlaceholderSelection) {
    const juce::StringArray outputs {
        "Speakers",
        "CABLE Input(VB-Audio Virtual Cable)"
    };

    const auto selection = audio_settings_detail::makeRecoverySelection(
        "CABLE Input(VB-Audio Virtual Cable)", outputs, 2);

    EXPECT_TRUE(selection.desiredDeviceAvailable);
    EXPECT_EQ(selection.desiredDeviceId, 3);
    EXPECT_EQ(selection.placeholderId, 4);
    EXPECT_NE(selection.placeholderId, selection.desiredDeviceId);
    EXPECT_EQ(selection.placeholderText,
              "CABLE Input(VB-Audio Virtual Cable) (Reconnect)");
}

TEST_F(AudioEngineTest, MissingRecoveryDeviceUsesDisconnectedPlaceholder) {
    const juce::StringArray devices { "Speakers" };

    const auto selection = audio_settings_detail::makeRecoverySelection(
        "Missing Device", devices, 2);

    EXPECT_FALSE(selection.desiredDeviceAvailable);
    EXPECT_EQ(selection.desiredDeviceId, 0);
    EXPECT_EQ(selection.placeholderId, 3);
    EXPECT_EQ(selection.placeholderText, "Missing Device (Disconnected)");
    EXPECT_TRUE(audio_settings_detail::isRecoveryPlaceholderSelection(
        selection.placeholderId, selection.placeholderId));
    EXPECT_FALSE(audio_settings_detail::isRecoveryPlaceholderSelection(
        1, selection.placeholderId));
}

TEST_F(AudioEngineTest, RecoveryPlaceholderDetectionDoesNotRejectRealSuffixDeviceNames) {
    const juce::StringArray devices {
        "Studio Device (Reconnect)",
        "Backup Device (Disconnected)"
    };
    const auto selection = audio_settings_detail::makeRecoverySelection(
        devices[0], devices, 1);

    ASSERT_TRUE(selection.desiredDeviceAvailable);
    EXPECT_EQ(selection.desiredDeviceId, 1);
    EXPECT_EQ(selection.placeholderId, 3);
    EXPECT_FALSE(audio_settings_detail::isRecoveryPlaceholderSelection(
        selection.desiredDeviceId, selection.placeholderId));
    EXPECT_TRUE(audio_settings_detail::isRecoveryPlaceholderSelection(
        selection.placeholderId, selection.placeholderId));
}

TEST_F(AudioEngineTest, SelectedChannelPairUsesDualMonoAndSingleChannelFallback) {
    EXPECT_EQ(audio_settings_detail::channelCountForSelectedPair(0, 2), 2);
    EXPECT_EQ(audio_settings_detail::channelCountForSelectedPair(2, 4), 2);
    EXPECT_EQ(audio_settings_detail::channelCountForSelectedPair(0, 1), 1);
    EXPECT_EQ(audio_settings_detail::channelCountForSelectedPair(2, 3), 0);
    EXPECT_EQ(audio_settings_detail::channelCountForSelectedPair(4, 4), 0);
    EXPECT_EQ(audio_settings_detail::channelCountForSelectedPair(0, 0), 2);
}

TEST_F(AudioEngineTest, AsioDuplexLossShowsRecoveryForBothDeviceCombos) {
    using audio_settings_detail::DeviceDirection;

    EXPECT_TRUE(audio_settings_detail::needsRecoveryPlaceholder(
        DeviceDirection::Input, true, true, false));
    EXPECT_TRUE(audio_settings_detail::needsRecoveryPlaceholder(
        DeviceDirection::Output, true, true, false));
    EXPECT_TRUE(audio_settings_detail::needsRecoveryPlaceholder(
        DeviceDirection::Input, true, false, true));
    EXPECT_TRUE(audio_settings_detail::needsRecoveryPlaceholder(
        DeviceDirection::Output, true, false, true));
}

TEST_F(AudioEngineTest, WindowsAudioLossOnlyMarksAffectedDirection) {
    using audio_settings_detail::DeviceDirection;

    EXPECT_TRUE(audio_settings_detail::needsRecoveryPlaceholder(
        DeviceDirection::Input, false, true, false));
    EXPECT_FALSE(audio_settings_detail::needsRecoveryPlaceholder(
        DeviceDirection::Output, false, true, false));
    EXPECT_FALSE(audio_settings_detail::needsRecoveryPlaceholder(
        DeviceDirection::Input, false, false, true));
    EXPECT_TRUE(audio_settings_detail::needsRecoveryPlaceholder(
        DeviceDirection::Output, false, false, true));
}

TEST_F(AudioEngineTest, FirstWindowsExclusiveSwitchKeepsTheCurrentDeviceSelection) {
    DriverTypeSnapshot current;
    current.inputDevice = "Microphone(Yeti Stereo Microphone)";
    current.outputDevice = "CABLE Input(VB-Audio Virtual Cable)";
    current.sampleRate = 48000.0;
    current.bufferSize = 512;
    current.inputChannels.setRange(0, 2, true);
    current.outputChannels.setRange(0, 2, true);
    current.outputNone = true;

    DriverTypeSnapshot seeded;
    ASSERT_TRUE(audio_device_recovery_detail::seedCompatibleWindowsDriverSnapshot(
        "Windows Audio", "Windows Audio (Exclusive Mode)", false, current, seeded));
    EXPECT_EQ(seeded.inputDevice, current.inputDevice);
    EXPECT_EQ(seeded.outputDevice, current.outputDevice);
    EXPECT_EQ(seeded.sampleRate, current.sampleRate);
    EXPECT_EQ(seeded.bufferSize, current.bufferSize);
    EXPECT_EQ(seeded.inputChannels, current.inputChannels);
    EXPECT_EQ(seeded.outputChannels, current.outputChannels);
    EXPECT_FALSE(seeded.outputNone)
        << "a first switch carries routing, not the source driver's None state";

    EXPECT_FALSE(audio_device_recovery_detail::seedCompatibleWindowsDriverSnapshot(
        "Windows Audio", "Windows Audio (Exclusive Mode)", true, current, seeded));
    EXPECT_FALSE(audio_device_recovery_detail::seedCompatibleWindowsDriverSnapshot(
        "Windows Audio", "ASIO", false, current, seeded));

    EXPECT_TRUE(audio_device_recovery_detail::snapshotEndpointsAvailable(
        seeded,
        { "Microphone(Yeti Stereo Microphone)", "Microphone(FHD60F)" },
        { "CABLE Input(VB-Audio Virtual Cable)", "Speakers(Yeti Stereo Microphone)" }));
    EXPECT_FALSE(audio_device_recovery_detail::snapshotEndpointsAvailable(
        seeded,
        { "Microphone(FHD60F)" },
        { "CABLE Input(VB-Audio Virtual Cable)", "Speakers(Yeti Stereo Microphone)" }));
    EXPECT_FALSE(audio_device_recovery_detail::snapshotEndpointsAvailable(
        seeded,
        { "Microphone(Yeti Stereo Microphone)" },
        { "Speakers(Yeti Stereo Microphone)" }));
}

TEST_F(AudioEngineTest, FailedDriverSwitchRestoresTheExactPreviousSnapshot) {
    juce::AudioDeviceManager manager;
    const auto stats = std::make_shared<ManagedFakeDeviceStats>();
    addManagedFakeDeviceType(manager, stats);

    DriverTypeSnapshot snapshot;
    snapshot.inputDevice = "Recovery Device";
    snapshot.outputDevice = "Recovery Device";
    snapshot.sampleRate = 48000.0;
    snapshot.bufferSize = 256;
    snapshot.inputChannels.setRange(0, 2, true);
    snapshot.outputChannels.setRange(0, 2, true);

    manager.closeAudioDevice();
    ASSERT_TRUE(audio_device_recovery_detail::restoreDriverSnapshot(
        manager, snapshot).isEmpty());

    juce::AudioDeviceManager::AudioDeviceSetup restored;
    manager.getAudioDeviceSetup(restored);
    EXPECT_EQ(restored.inputDeviceName, snapshot.inputDevice);
    EXPECT_EQ(restored.outputDeviceName, snapshot.outputDevice);
    EXPECT_EQ(restored.sampleRate, snapshot.sampleRate);
    EXPECT_EQ(restored.bufferSize, snapshot.bufferSize);
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_TRUE(manager.getCurrentAudioDevice()->isOpen());
}

TEST_F(AudioEngineTest, FailedOutputSelectionRestoresThePreviousDuplexDevice) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);

    auto initial = managedDuplexSetup("Recovery Device");
    ASSERT_TRUE(manager.setAudioDeviceSetup(initial, true).isEmpty());

    // Reject a stale UI selection before JUCE can close/recreate the current
    // duplex stream.
    const auto openAttemptsBefore = stats->openAttempts.size();
    const auto result = engine_->setOutputDevice("Missing Output");
    SCOPED_TRACE(result.message.toStdString());
    SCOPED_TRACE("open attempts=" + std::to_string(stats->openAttempts.size()));
    EXPECT_FALSE(result);

    juce::AudioDeviceManager::AudioDeviceSetup restored;
    manager.getAudioDeviceSetup(restored);
    EXPECT_EQ(restored.inputDeviceName, initial.inputDeviceName);
    EXPECT_EQ(restored.outputDeviceName, initial.outputDeviceName);
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_TRUE(manager.getCurrentAudioDevice()->isOpen());
    EXPECT_EQ(stats->openAttempts.size(), openAttemptsBefore);
}

TEST_F(AudioEngineTest, FailedInputSelectionRestoresThePreviousDuplexDevice) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);

    auto initial = managedDuplexSetup("Recovery Device");
    ASSERT_TRUE(manager.setAudioDeviceSetup(initial, true).isEmpty());

    const auto openAttemptsBefore = stats->openAttempts.size();
    const auto result = engine_->setInputDevice("Missing Input");
    SCOPED_TRACE(result.message.toStdString());
    EXPECT_FALSE(result);

    juce::AudioDeviceManager::AudioDeviceSetup restored;
    manager.getAudioDeviceSetup(restored);
    EXPECT_EQ(restored.inputDeviceName, initial.inputDeviceName);
    EXPECT_EQ(restored.outputDeviceName, initial.outputDeviceName);
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_TRUE(manager.getCurrentAudioDevice()->isOpen());
    EXPECT_EQ(stats->openAttempts.size(), openAttemptsBefore);
}

TEST_F(AudioEngineTest, FailedOutputSelectionAndRollbackEnterDeviceRecovery) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    stats->deviceNames = { "Working Device", "Failing Device" };
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);

    auto initial = managedDuplexSetup("Working Device");
    ASSERT_TRUE(manager.setAudioDeviceSetup(initial, true).isEmpty());

    stats->failEveryOpen = true;
    engine_->forceReconnectCooldownForTest(37);
    const auto result = engine_->setOutputDevice("Failing Device");

    EXPECT_FALSE(result);
    EXPECT_TRUE(result.message.contains("previous setup restore failed"));
    EXPECT_EQ(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());
    EXPECT_EQ(engine_->getReconnectCooldownForTest(), 90);
}

TEST_F(AudioEngineTest, FailedInputSelectionAndRollbackEnterDeviceRecovery) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    stats->deviceNames = { "Working Device", "Failing Device" };
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);

    auto initial = managedDuplexSetup("Working Device");
    ASSERT_TRUE(manager.setAudioDeviceSetup(initial, true).isEmpty());

    stats->failEveryOpen = true;
    engine_->forceReconnectCooldownForTest(37);
    const auto result = engine_->setInputDevice("Failing Device");

    EXPECT_FALSE(result);
    EXPECT_TRUE(result.message.contains("previous setup restore failed"));
    EXPECT_EQ(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());
    EXPECT_EQ(engine_->getReconnectCooldownForTest(), 90);
}

TEST_F(AudioEngineTest, UnusableDriverSwitchRestoresThePreviousDriverSnapshot) {
    auto goodStats = std::make_shared<ManagedFakeDeviceStats>();
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, goodStats);

    juce::AudioDeviceManager::AudioDeviceSetup initial;
    initial.inputDeviceName = goodStats->deviceName;
    initial.outputDeviceName = goodStats->deviceName;
    initial.sampleRate = 48000.0;
    initial.bufferSize = 128;
    initial.useDefaultInputChannels = true;
    initial.useDefaultOutputChannels = true;
    ASSERT_TRUE(manager.setAudioDeviceSetup(initial, true).isEmpty());
    juce::AudioDeviceManager::AudioDeviceSetup expectedRestored;
    manager.getAudioDeviceSetup(expectedRestored);
    engine_->syncDesiredFromDevice();

    auto brokenStats = std::make_shared<ManagedFakeDeviceStats>();
    brokenStats->typeName = "Broken Recovery Test";
    brokenStats->deviceName = "Broken Recovery Device";
    brokenStats->reportNoActiveChannels = true;
    brokenStats->reportedSampleRate = 44100.0;
    brokenStats->reportedBufferSize = 256;
    addManagedFakeDeviceType(manager, brokenStats, false);

    const auto result = engine_->setAudioDeviceType(brokenStats->typeName);

    EXPECT_FALSE(result);
    EXPECT_TRUE(result.message.contains("audio device is not ready"));
    EXPECT_EQ(engine_->getCurrentDeviceType(), goodStats->typeName);
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_TRUE(manager.getCurrentAudioDevice()->isOpen());

    juce::AudioDeviceManager::AudioDeviceSetup restored;
    manager.getAudioDeviceSetup(restored);
    EXPECT_EQ(restored.inputDeviceName, expectedRestored.inputDeviceName);
    EXPECT_EQ(restored.outputDeviceName, expectedRestored.outputDeviceName);
    EXPECT_EQ(restored.sampleRate, expectedRestored.sampleRate);
    EXPECT_EQ(restored.bufferSize, expectedRestored.bufferSize);
    EXPECT_TRUE(engine_->hasUsableActiveChannelsForTest(
        restored, manager.getCurrentAudioDevice()));
    EXPECT_EQ(engine_->getDesiredDeviceType(), goodStats->typeName);
    EXPECT_EQ(engine_->getCurrentSampleRateForTest(), expectedRestored.sampleRate);
    EXPECT_EQ(engine_->getCurrentBufferSizeForTest(), expectedRestored.bufferSize);
    EXPECT_EQ(engine_->getDesiredSampleRate(), expectedRestored.sampleRate);
    EXPECT_EQ(engine_->getDesiredBufferSize(), expectedRestored.bufferSize);
    EXPECT_FALSE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isInputDeviceLost());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
}

TEST_F(AudioEngineTest, AutomaticAsioSwitchSkipsUnusableCandidate) {
    auto windowsStats = std::make_shared<ManagedFakeDeviceStats>();
    windowsStats->typeName = "Automatic Candidate Windows";
    windowsStats->deviceName = "Windows Device";
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, windowsStats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(windowsStats->deviceName), true).isEmpty());
    engine_->syncDesiredFromDevice();

    auto asioStats = std::make_shared<ManagedFakeDeviceStats>();
    asioStats->typeName = "Automatic Candidate ASIO";
    asioStats->deviceNames = { "Unusable ASIO", "Working ASIO" };
    asioStats->noActiveChannelsDeviceName = "Unusable ASIO";
    addManagedFakeDeviceType(manager, asioStats, false);

    const auto result = engine_->setAudioDeviceType(asioStats->typeName);

    ASSERT_TRUE(result) << result.message.toStdString();
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_EQ(manager.getCurrentAudioDevice()->getName(), "Working ASIO");
    EXPECT_TRUE(asioStats->openedDeviceNames.contains("Unusable ASIO"));
    EXPECT_TRUE(asioStats->openedDeviceNames.contains("Working ASIO"));
    juce::AudioDeviceManager::AudioDeviceSetup applied;
    manager.getAudioDeviceSetup(applied);
    EXPECT_TRUE(engine_->hasUsableActiveChannelsForTest(
        applied, manager.getCurrentAudioDevice()));
}

TEST_F(AudioEngineTest, AutomaticAsioSwitchSkipsPartialAndInvalidRuntimeCandidates) {
    auto windowsStats = std::make_shared<ManagedFakeDeviceStats>();
    windowsStats->typeName = "Automatic Runtime Candidate Windows";
    windowsStats->deviceName = "Windows Device";
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, windowsStats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(windowsStats->deviceName), true).isEmpty());
    engine_->syncDesiredFromDevice();

    auto asioStats = std::make_shared<ManagedFakeDeviceStats>();
    asioStats->typeName = "Automatic Runtime Candidate ASIO";
    asioStats->deviceNames = {
        "Partial Input ASIO",
        "Partial Output ASIO",
        "Zero Sample Rate ASIO",
        "Zero Buffer Size ASIO",
        "Working ASIO",
    };
    asioStats->partialInputChannelsDeviceName = "Partial Input ASIO";
    asioStats->partialOutputChannelsDeviceName = "Partial Output ASIO";
    asioStats->zeroSampleRateDeviceName = "Zero Sample Rate ASIO";
    asioStats->zeroBufferSizeDeviceName = "Zero Buffer Size ASIO";
    addManagedFakeDeviceType(manager, asioStats, false);

    const auto result = engine_->setAudioDeviceType(asioStats->typeName);

    ASSERT_TRUE(result) << result.message.toStdString();
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_EQ(manager.getCurrentAudioDevice()->getName(), "Working ASIO");
    for (const auto& candidate : asioStats->deviceNames)
        EXPECT_TRUE(asioStats->openedDeviceNames.contains(candidate))
            << candidate.toStdString();
}

TEST_F(AudioEngineTest, UnusablePreferredAsioDoesNotOpenReplacementCandidate) {
    auto windowsStats = std::make_shared<ManagedFakeDeviceStats>();
    windowsStats->typeName = "Strict Candidate Windows";
    windowsStats->deviceName = "Windows Device";
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, windowsStats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(windowsStats->deviceName), true).isEmpty());
    engine_->syncDesiredFromDevice();

    auto asioStats = std::make_shared<ManagedFakeDeviceStats>();
    asioStats->typeName = "Strict Candidate ASIO";
    asioStats->deviceNames = { "Preferred ASIO", "Replacement ASIO" };
    asioStats->noActiveChannelsDeviceName = "Preferred ASIO";
    addManagedFakeDeviceType(manager, asioStats, false);

    const auto result = engine_->setAudioDeviceType(
        asioStats->typeName, "Preferred ASIO");

    EXPECT_FALSE(result);
    EXPECT_TRUE(asioStats->openedDeviceNames.contains("Preferred ASIO"));
    EXPECT_FALSE(asioStats->openedDeviceNames.contains("Replacement ASIO"));
    EXPECT_EQ(engine_->getCurrentDeviceType(), windowsStats->typeName);
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_EQ(manager.getCurrentAudioDevice()->getName(), windowsStats->deviceName);
}

TEST_F(AudioEngineTest, UnavailableDriverTypeLeavesCurrentStreamUntouched) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);

    juce::AudioDeviceManager::AudioDeviceSetup initial;
    initial.inputDeviceName = stats->deviceName;
    initial.outputDeviceName = stats->deviceName;
    initial.sampleRate = 48000.0;
    initial.bufferSize = 128;
    initial.useDefaultInputChannels = true;
    initial.useDefaultOutputChannels = true;
    ASSERT_TRUE(manager.setAudioDeviceSetup(initial, true).isEmpty());
    engine_->syncDesiredFromDevice();

    auto* originalDevice = manager.getCurrentAudioDevice();
    ASSERT_NE(originalDevice, nullptr);
    const auto openAttemptsBefore = stats->openAttempts.size();
    engine_->forceSameDeviceReopenPendingForTest(true);
    engine_->forceReconnectCooldownForTest(37);

    const auto result = engine_->setAudioDeviceType("Missing Driver Type");

    EXPECT_FALSE(result);
    EXPECT_TRUE(result.message.contains("not available"));
    EXPECT_EQ(engine_->getCurrentDeviceType(), stats->typeName);
    EXPECT_EQ(manager.getCurrentAudioDevice(), originalDevice);
    EXPECT_TRUE(manager.getCurrentAudioDevice()->isOpen());
    EXPECT_EQ(stats->openAttempts.size(), openAttemptsBefore);
    EXPECT_EQ(engine_->getDesiredDeviceType(), stats->typeName);
    EXPECT_TRUE(engine_->isSameDeviceReopenPendingForTest());
    EXPECT_EQ(engine_->getReconnectCooldownForTest(), 37);
}

TEST_F(AudioEngineTest, UnavailableDriverTypeIsRejectedWithoutAnOpenStream) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    manager.closeAudioDevice();

    const auto result = engine_->setAudioDeviceType("Missing Driver Type");

    EXPECT_FALSE(result);
    EXPECT_TRUE(result.message.contains("not available"));
    EXPECT_EQ(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_EQ(engine_->getDesiredDeviceType(), stats->typeName);
}

TEST_F(AudioEngineTest, FailedAsioDeviceSelectionRestoresPreviousDuplexStream) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->typeName = "Recovery ASIO";
    stats->deviceNames = { "Working ASIO", "Failing ASIO" };
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);

    auto initial = managedDuplexSetup("Working ASIO");
    ASSERT_TRUE(manager.setAudioDeviceSetup(initial, true).isEmpty());
    engine_->rememberRestoredDeviceTargets(
        stats->typeName, "Working ASIO", "Working ASIO");
    engine_->syncDesiredFromDevice();

    stats->failDeviceName = "Failing ASIO";
    const auto result = engine_->setAsioDevice("Failing ASIO");

    EXPECT_FALSE(result);
    EXPECT_TRUE(result.message.contains("Failed to set ASIO device"));
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_TRUE(manager.getCurrentAudioDevice()->isOpen());
    EXPECT_EQ(manager.getCurrentAudioDevice()->getName(), "Working ASIO");

    juce::AudioDeviceManager::AudioDeviceSetup restored;
    manager.getAudioDeviceSetup(restored);
    EXPECT_EQ(restored.inputDeviceName, "Working ASIO");
    EXPECT_EQ(restored.outputDeviceName, "Working ASIO");
    EXPECT_EQ(engine_->getDesiredInputDevice(), "Working ASIO");
    EXPECT_EQ(engine_->getDesiredOutputDevice(), "Working ASIO");
    EXPECT_FALSE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isInputDeviceLost());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
}

TEST_F(AudioEngineTest, SuccessfulAsioDeviceSelectionPreservesRequestedSampleRateAndAdoptsActualBuffer) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->typeName = "Recovery ASIO";
    stats->deviceNames = { "Working ASIO", "New ASIO" };
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);

    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup("Working ASIO", 48000.0, 128), true).isEmpty());
    engine_->presetAudioParams(48000.0, 128);

    stats->reportedSampleRate = 44100.0;
    stats->reportedBufferSize = 256;
    const auto result = engine_->setAsioDevice("New ASIO");

    ASSERT_TRUE(result) << result.message.toStdString();
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_EQ(manager.getCurrentAudioDevice()->getName(), "New ASIO");
    EXPECT_DOUBLE_EQ(engine_->getCurrentSampleRateForTest(), 44100.0);
    EXPECT_EQ(engine_->getCurrentBufferSizeForTest(), 256);
    EXPECT_DOUBLE_EQ(engine_->getDesiredSampleRate(), 48000.0);
    EXPECT_EQ(engine_->getDesiredBufferSize(), 256);
}

TEST_F(AudioEngineTest, SavedDriverSnapshotDoesNotUseAReplacementEndpoint) {
    auto firstStats = std::make_shared<ManagedFakeDeviceStats>();
    firstStats->typeName = "First Recovery Type";
    firstStats->deviceName = "First Device";
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, firstStats);

    juce::AudioDeviceManager::AudioDeviceSetup firstSetup;
    firstSetup.inputDeviceName = firstStats->deviceName;
    firstSetup.outputDeviceName = firstStats->deviceName;
    firstSetup.sampleRate = 48000.0;
    firstSetup.bufferSize = 128;
    firstSetup.useDefaultInputChannels = true;
    firstSetup.useDefaultOutputChannels = true;
    ASSERT_TRUE(manager.setAudioDeviceSetup(firstSetup, true).isEmpty());
    engine_->syncDesiredFromDevice();

    auto secondStats = std::make_shared<ManagedFakeDeviceStats>();
    secondStats->typeName = "Second Recovery Type";
    secondStats->deviceName = "Saved Second Device";
    addManagedFakeDeviceType(manager, secondStats, false);

    ASSERT_TRUE(engine_->setAudioDeviceType(secondStats->typeName));
    ASSERT_TRUE(engine_->setAudioDeviceType(firstStats->typeName));
    secondStats->deviceName = "Replacement Second Device";

    const auto result = engine_->setAudioDeviceType(secondStats->typeName);

    EXPECT_FALSE(result);
    EXPECT_TRUE(result.message.contains("unavailable"));
    EXPECT_EQ(engine_->getCurrentDeviceType(), firstStats->typeName);
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_EQ(manager.getCurrentAudioDevice()->getName(), firstStats->deviceName);
    EXPECT_EQ(engine_->getDesiredDeviceType(), firstStats->typeName);
}

TEST_F(AudioEngineTest, MissingSavedAsioDeviceIsRejectedBeforeOpeningAReplacement) {
    auto firstStats = std::make_shared<ManagedFakeDeviceStats>();
    firstStats->typeName = "Windows Recovery Type";
    firstStats->deviceName = "Windows Recovery Device";
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, firstStats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(firstStats->deviceName), true).isEmpty());
    engine_->syncDesiredFromDevice();

    auto asioStats = std::make_shared<ManagedFakeDeviceStats>();
    asioStats->typeName = "Saved Recovery ASIO";
    asioStats->deviceNames = { "Saved ASIO Device" };
    addManagedFakeDeviceType(manager, asioStats, false);

    ASSERT_TRUE(engine_->setAudioDeviceType(
        asioStats->typeName, "Saved ASIO Device"));
    ASSERT_TRUE(engine_->setAudioDeviceType(firstStats->typeName));
    const auto targetOpenAttemptsBefore = asioStats->openAttempts.size();
    asioStats->deviceNames = { "Replacement ASIO Device" };

    const auto result = engine_->setAudioDeviceType(asioStats->typeName);

    EXPECT_FALSE(result);
    EXPECT_TRUE(result.message.contains("unavailable"));
    EXPECT_EQ(asioStats->openAttempts.size(), targetOpenAttemptsBefore);
    EXPECT_EQ(engine_->getCurrentDeviceType(), firstStats->typeName);
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_EQ(manager.getCurrentAudioDevice()->getName(), firstStats->deviceName);
}

TEST_F(AudioEngineTest, FailedDesiredAsioRecoveryRestoresActualWindowsFallback) {
    auto windowsStats = std::make_shared<ManagedFakeDeviceStats>();
    windowsStats->typeName = "Windows Fallback Recovery";
    windowsStats->deviceName = "Fallback Windows Device";
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, windowsStats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(windowsStats->deviceName), true).isEmpty());
    engine_->syncDesiredFromDevice();

    auto asioStats = std::make_shared<ManagedFakeDeviceStats>();
    asioStats->typeName = "Wanted Recovery ASIO";
    asioStats->deviceName = "Wanted ASIO Device";
    asioStats->failEveryOpen = true;
    addManagedFakeDeviceType(manager, asioStats, false);
    engine_->rememberRestoredDeviceTargets(
        asioStats->typeName, asioStats->deviceName, asioStats->deviceName);

    const auto result = engine_->setAudioDeviceType(
        asioStats->typeName, asioStats->deviceName);

    EXPECT_FALSE(result);
    EXPECT_EQ(engine_->getCurrentDeviceType(), windowsStats->typeName);
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_TRUE(manager.getCurrentAudioDevice()->isOpen());
    EXPECT_EQ(manager.getCurrentAudioDevice()->getName(), windowsStats->deviceName);
    juce::AudioDeviceManager::AudioDeviceSetup restored;
    manager.getAudioDeviceSetup(restored);
    EXPECT_EQ(restored.inputDeviceName, windowsStats->deviceName);
    EXPECT_EQ(restored.outputDeviceName, windowsStats->deviceName);
    EXPECT_EQ(engine_->getDesiredDeviceType(), asioStats->typeName);
    EXPECT_EQ(engine_->getDesiredInputDevice(), asioStats->deviceName);
    EXPECT_EQ(engine_->getDesiredOutputDevice(), asioStats->deviceName);
    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());
}

TEST_F(AudioEngineTest, FailedBufferSizeChangeRestoresPreviousReadyStream) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName), true).isEmpty());
    engine_->syncDesiredFromDevice();
    stats->failBufferSize = 256;

    const auto result = engine_->setBufferSize(256);

    EXPECT_FALSE(result);
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr)
        << stats->openRequestLog.joinIntoString(" | ").toStdString();
    EXPECT_TRUE(manager.getCurrentAudioDevice()->isOpen());
    EXPECT_EQ(manager.getCurrentAudioDevice()->getCurrentBufferSizeSamples(), 128);
    EXPECT_EQ(engine_->getCurrentBufferSizeForTest(), 128);
    EXPECT_EQ(engine_->getDesiredBufferSize(), 128);
}

TEST_F(AudioEngineTest, FailedBufferChangePreservesExistingOutputRecoveryState) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName), true).isEmpty());
    engine_->syncDesiredFromDevice();
    engine_->markOutputDeviceLostForTest("Missing Speakers");
    ASSERT_TRUE(engine_->isDeviceLost());
    ASSERT_TRUE(engine_->isOutputAutoMuted());
    ASSERT_FALSE(engine_->isStartupRestorePendingForTest());
    stats->failBufferSize = 256;

    const auto result = engine_->setBufferSize(256);

    EXPECT_FALSE(result);
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_EQ(manager.getCurrentAudioDevice()->getCurrentBufferSizeSamples(), 128);
    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());
    EXPECT_TRUE(engine_->isOutputMuted());
    EXPECT_FALSE(engine_->isStartupRestorePendingForTest());
    EXPECT_EQ(engine_->getDesiredOutputDevice(), "Missing Speakers");
}

TEST_F(AudioEngineTest, FailedSampleRateChangeRestoresPreviousReadyStream) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName), true).isEmpty());
    engine_->syncDesiredFromDevice();
    stats->failSampleRate = 44100.0;

    const auto result = engine_->setSampleRate(44100.0);

    EXPECT_FALSE(result);
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr)
        << stats->openRequestLog.joinIntoString(" | ").toStdString();
    EXPECT_TRUE(manager.getCurrentAudioDevice()->isOpen());
    EXPECT_DOUBLE_EQ(manager.getCurrentAudioDevice()->getCurrentSampleRate(), 48000.0);
    EXPECT_DOUBLE_EQ(engine_->getCurrentSampleRateForTest(), 48000.0);
    EXPECT_DOUBLE_EQ(engine_->getDesiredSampleRate(), 48000.0);
}

TEST_F(AudioEngineTest, FailedExplicitInputChannelChangeRestoresPreviousRouting) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName), true).isEmpty());
    stats->failInputFirstChannel = 1;

    const auto result = engine_->setActiveInputChannels(1, 1);

    EXPECT_FALSE(result);
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_TRUE(manager.getCurrentAudioDevice()->isOpen());
    juce::AudioDeviceManager::AudioDeviceSetup restored;
    manager.getAudioDeviceSetup(restored);
    EXPECT_NE(restored.inputChannels.findNextSetBit(0), 1);
}

TEST_F(AudioEngineTest, FailedExplicitOutputChannelChangeRestoresPreviousRouting) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName), true).isEmpty());
    stats->failOutputFirstChannel = 1;

    const auto result = engine_->setActiveOutputChannels(1, 1);

    EXPECT_FALSE(result);
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr)
        << stats->openRequestLog.joinIntoString(" | ").toStdString();
    EXPECT_TRUE(manager.getCurrentAudioDevice()->isOpen());
    juce::AudioDeviceManager::AudioDeviceSetup restored;
    manager.getAudioDeviceSetup(restored);
    EXPECT_NE(restored.outputChannels.findNextSetBit(0), 1);
}

TEST_F(AudioEngineTest, FailedFullSetupApplyRestoresPreviousReadyStream) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName), true).isEmpty());
    stats->failBufferSize = 256;
    auto target = managedDuplexSetup(stats->deviceName, 48000.0, 256);

    const auto result = engine_->applyAudioDeviceSetup(target, "test setup");

    EXPECT_FALSE(result);
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr)
        << stats->openRequestLog.joinIntoString(" | ").toStdString();
    EXPECT_TRUE(manager.getCurrentAudioDevice()->isOpen());
    EXPECT_EQ(manager.getCurrentAudioDevice()->getCurrentBufferSizeSamples(), 128);
}

TEST_F(AudioEngineTest, ZeroActiveOutputSelectionRestoresPreviousDevice) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    stats->deviceNames = { "Working Device", "Silent Device" };
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup("Working Device"), true).isEmpty());
    engine_->rememberRestoredDeviceTargets(
        stats->typeName, "Working Device", "Working Device");
    stats->noActiveChannelsDeviceName = "Silent Device";

    const auto result = engine_->setOutputDevice("Silent Device");

    EXPECT_FALSE(result);
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    juce::AudioDeviceManager::AudioDeviceSetup restored;
    manager.getAudioDeviceSetup(restored);
    EXPECT_EQ(restored.inputDeviceName, "Working Device");
    EXPECT_EQ(restored.outputDeviceName, "Working Device");
    EXPECT_EQ(engine_->getDesiredOutputDevice(), "Working Device");
    EXPECT_FALSE(engine_->isActiveChannelRecoveryPendingForTest());
}

TEST_F(AudioEngineTest, ZeroActiveInputSelectionRestoresPreviousDevice) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    stats->deviceNames = { "Working Device", "Silent Device" };
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup("Working Device"), true).isEmpty());
    engine_->rememberRestoredDeviceTargets(
        stats->typeName, "Working Device", "Working Device");
    stats->noActiveChannelsDeviceName = "Silent Device";

    const auto result = engine_->setInputDevice("Silent Device");

    EXPECT_FALSE(result);
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    juce::AudioDeviceManager::AudioDeviceSetup restored;
    manager.getAudioDeviceSetup(restored);
    EXPECT_EQ(restored.inputDeviceName, "Working Device");
    EXPECT_EQ(restored.outputDeviceName, "Working Device");
    EXPECT_EQ(engine_->getDesiredInputDevice(), "Working Device");
}

TEST_F(AudioEngineTest, ZeroActiveRollbackEntersFailClosedDeviceRecovery) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    stats->deviceNames = { "Working Device", "Failing Device" };
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup("Working Device"), true).isEmpty());
    engine_->rememberRestoredDeviceTargets(
        stats->typeName, "Working Device", "Working Device");
    stats->failDeviceName = "Failing Device";
    stats->reportNoActiveChannels = true;

    const auto result = engine_->setInputDevice("Failing Device");

    EXPECT_FALSE(result);
    EXPECT_EQ(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());
    EXPECT_EQ(engine_->getReconnectCooldownForTest(), 90);
}

TEST_F(AudioEngineTest, ReselectingCurrentOutputClearsOutputRecoveryMute) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName), true).isEmpty());
    engine_->syncDesiredFromDevice();
    engine_->markOutputDeviceLostForTest("Missing Speakers");
    ASSERT_TRUE(engine_->isDeviceLost());
    ASSERT_TRUE(engine_->isOutputAutoMuted());

    const auto result = engine_->setOutputDevice(stats->deviceName);

    EXPECT_TRUE(result);
    EXPECT_FALSE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
    EXPECT_EQ(engine_->getDesiredOutputDevice(), stats->deviceName);
}

TEST_F(AudioEngineTest, ReselectingCurrentInputClearsInputRecoveryMute) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName), true).isEmpty());
    engine_->syncDesiredFromDevice();
    engine_->markInputDeviceLostForTest("Missing Microphone");
    ASSERT_TRUE(engine_->isDeviceLost());
    ASSERT_TRUE(engine_->isInputDeviceLost());

    const auto result = engine_->setInputDevice(stats->deviceName);

    EXPECT_TRUE(result);
    EXPECT_FALSE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isInputDeviceLost());
    EXPECT_EQ(engine_->getDesiredInputDevice(), stats->deviceName);
}

TEST_F(AudioEngineTest, FailedActiveChannelRecoveryRearmsThreeSecondCooldown) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    manager.closeAudioDevice();
    stats->failEveryOpen = true;

    engine_->forceReconnectCooldownForTest(0);
    EXPECT_FALSE(engine_->recoverActiveChannelsWithDriverDefaultsForTest(
        "test forced open failure"));
    EXPECT_EQ(engine_->getReconnectCooldownForTest(), 90);
    EXPECT_EQ(audio_device_recovery_detail::reconnectCooldownAfterRecovery(false), 90);
    EXPECT_EQ(audio_device_recovery_detail::reconnectCooldownAfterRecovery(true), 0);
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

#if JUCE_WINDOWS
    // The recovery request above is asynchronous. Destroying an engine that
    // was never initialized must still invalidate that queued callback.
    engine_.reset();
    std::atomic<bool> queueDrained{false};
    ASSERT_TRUE(juce::MessageManager::callAsync([&queueDrained] {
        queueDrained.store(true, std::memory_order_release);
    }));
    ASSERT_TRUE(pumpMessagesUntil(queueDrained));
#endif
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

TEST_F(AudioEngineTest, EndpointChangesCoalesceWhileSameDeviceReopenIsPending) {
    engine_->forceSameDeviceReopenPendingForTest(true);

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = "Microphone(Yeti Stereo Microphone)";
    setup.outputDeviceName = "CABLE Input(VB-Audio Virtual Cable)";

    EXPECT_FALSE(engine_->markInputEndpointRestartPendingForTest(
        setup, "capture endpoint property changed"));
    EXPECT_FALSE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isInputDeviceLost());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
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

TEST_F(AudioEngineTest, DeferredInitialDeviceSnapshotCannotOverwriteLastSelectedDevices) {
#if !JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    ASSERT_NE(juce::MessageManager::getInstance(), nullptr);

    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->deviceName = "System Default";
    stats->separateInputsAndOutputs = true;
    stats->deviceNames = {
        "System Default", "User Selected Input", "User Selected Output"
    };
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName), true).isEmpty());
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);

    // AudioDeviceManager::addAudioCallback performs this callback before the
    // application's saved settings are restored. The device callback defers
    // its desired-device update to the message queue.
    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceAboutToStart(manager.getCurrentAudioDevice());

    // Simulate settings restore in the same message turn, before the deferred
    // default-device snapshot is allowed to run.
    engine_->rememberRestoredDeviceTargets(
        stats->typeName, "User Selected Input", "User Selected Output");

    std::atomic<bool> queueDrained{false};
    ASSERT_TRUE(juce::MessageManager::callAsync([&queueDrained] {
        queueDrained.store(true, std::memory_order_release);
    }));
    ASSERT_TRUE(pumpMessagesUntil(queueDrained));

    EXPECT_EQ(engine_->getDesiredInputDevice(), "User Selected Input");
    EXPECT_EQ(engine_->getDesiredOutputDevice(), "User Selected Output");
    EXPECT_TRUE(engine_->isStartupRestorePendingForTest());
#endif
}

TEST_F(AudioEngineTest, FirstLaunchDeferredSnapshotSeedsCurrentDevices) {
#if !JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    ASSERT_NE(juce::MessageManager::getInstance(), nullptr);

    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->deviceName = "System Default";
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName), true).isEmpty());

    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceAboutToStart(manager.getCurrentAudioDevice());

    std::atomic<bool> queueDrained{false};
    ASSERT_TRUE(juce::MessageManager::callAsync([&queueDrained] {
        queueDrained.store(true, std::memory_order_release);
    }));
    ASSERT_TRUE(pumpMessagesUntil(queueDrained));

    EXPECT_EQ(engine_->getDesiredInputDevice(), "System Default");
    EXPECT_EQ(engine_->getDesiredOutputDevice(), "System Default");
#endif
}

TEST_F(AudioEngineTest, SilentDeviceTypeFallbackRestoresSavedDriverAndEndpoints) {
#if !JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    ASSERT_NE(juce::MessageManager::getInstance(), nullptr);

    auto actualStats = std::make_shared<ManagedFakeDeviceStats>();
    actualStats->typeName = "Unexpected Driver";
    actualStats->deviceName = "Shared Endpoint Name";
    auto desiredStats = std::make_shared<ManagedFakeDeviceStats>();
    desiredStats->typeName = "Saved Driver";
    desiredStats->deviceName = "Shared Endpoint Name";
    desiredStats->deviceNames = { "First Enumerated Default", "Shared Endpoint Name" };

    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, actualStats);
    addManagedFakeDeviceType(manager, desiredStats, false);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup("Shared Endpoint Name"), true).isEmpty());

    engine_->rememberRestoredDeviceTargets(
        desiredStats->typeName,
        "Shared Endpoint Name",
        "Shared Endpoint Name");
    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());

    // The endpoint display names happen to match, but the active driver does
    // not. This must remain a pending restore rather than being accepted as a
    // new preference merely because Windows opened it without an error.
    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceAboutToStart(manager.getCurrentAudioDevice());

    std::atomic<bool> queueDrained{false};
    ASSERT_TRUE(juce::MessageManager::callAsync([&queueDrained] {
        queueDrained.store(true, std::memory_order_release);
    }));
    ASSERT_TRUE(pumpMessagesUntil(queueDrained));

    EXPECT_EQ(engine_->getCurrentDeviceType(), "Unexpected Driver");
    EXPECT_EQ(engine_->getDesiredDeviceType(), "Saved Driver");
    EXPECT_TRUE(engine_->isStartupRestorePendingForTest());
    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());

    engine_->attemptImmediateReconnectionForTest();

    juce::AudioDeviceManager::AudioDeviceSetup restored;
    manager.getAudioDeviceSetup(restored);
    EXPECT_EQ(engine_->getCurrentDeviceType(), "Saved Driver");
    EXPECT_EQ(engine_->getDesiredDeviceType(), "Saved Driver");
    EXPECT_EQ(restored.inputDeviceName, "Shared Endpoint Name");
    EXPECT_EQ(restored.outputDeviceName, "Shared Endpoint Name");
    EXPECT_FALSE(engine_->isStartupRestorePendingForTest());
    EXPECT_FALSE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isInputDeviceLost());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
#endif
}

TEST_F(AudioEngineTest, ErrorlessOutputFallbackPreservesLastSelectedOutput) {
#if !JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    ASSERT_NE(juce::MessageManager::getInstance(), nullptr);

    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    stats->deviceNames = { "System Default", "User Selected Output" };
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);

    auto selected = managedDuplexSetup("System Default");
    selected.outputDeviceName = "User Selected Output";
    ASSERT_TRUE(manager.setAudioDeviceSetup(selected, true).isEmpty());
    engine_->rememberRestoredDeviceTargets(
        stats->typeName, "System Default", "User Selected Output");
    ASSERT_FALSE(engine_->isDeviceLost());

    auto fallback = selected;
    fallback.outputDeviceName = "System Default";
    ASSERT_TRUE(manager.setAudioDeviceSetup(fallback, true).isEmpty());

    // Some Windows/JUCE restarts report the fallback start without first
    // delivering an error callback. A different actual output must still be
    // treated as fallback, not as a new user preference.
    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceAboutToStart(manager.getCurrentAudioDevice());

    std::atomic<bool> queueDrained{false};
    ASSERT_TRUE(juce::MessageManager::callAsync([&queueDrained] {
        queueDrained.store(true, std::memory_order_release);
    }));
    ASSERT_TRUE(pumpMessagesUntil(queueDrained));

    EXPECT_EQ(engine_->getDesiredInputDevice(), "System Default");
    EXPECT_EQ(engine_->getDesiredOutputDevice(), "User Selected Output");
    EXPECT_FALSE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
    EXPECT_FALSE(engine_->isStartupRestorePendingForTest());

    juce::AudioDeviceManager::AudioDeviceSetup restored;
    manager.getAudioDeviceSetup(restored);
    EXPECT_EQ(restored.outputDeviceName, "User Selected Output");
#endif
}

TEST_F(AudioEngineTest, DirectInvalidDeviceStartFailsClosedInBothDirections) {
    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->deviceName = "Invalid Runtime Device";
    stats->zeroSampleRateDeviceName = stats->deviceName;
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName), true).isEmpty());
    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);

    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceAboutToStart(manager.getCurrentAudioDevice());

    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());
    EXPECT_TRUE(engine_->isOutputMuted());
}

TEST_F(AudioEngineTest, InvalidRuntimeAfterFallbackRestoreRemainsPending) {
#if !JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    ASSERT_NE(juce::MessageManager::getInstance(), nullptr);

    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    stats->deviceNames = { "System Default", "Saved Output" };
    stats->zeroSampleRateDeviceName = "Saved Output";
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup("System Default"), true).isEmpty());
    engine_->rememberRestoredDeviceTargets(
        stats->typeName, "System Default", "Saved Output");

    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceAboutToStart(manager.getCurrentAudioDevice());

    std::atomic<bool> queueDrained{false};
    ASSERT_TRUE(juce::MessageManager::callAsync([&queueDrained] {
        queueDrained.store(true, std::memory_order_release);
    }));
    ASSERT_TRUE(pumpMessagesUntil(queueDrained));

    EXPECT_EQ(engine_->getDesiredOutputDevice(), "Saved Output");
    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());
#endif
}

TEST_F(AudioEngineTest, StoppedOutputFallbackClearsStaleInputLossAfterRestore) {
#if !JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    ASSERT_NE(juce::MessageManager::getInstance(), nullptr);

    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    stats->deviceNames = { "System Default", "Saved Output" };
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);

    auto selected = managedDuplexSetup("System Default");
    selected.outputDeviceName = "Saved Output";
    ASSERT_TRUE(manager.setAudioDeviceSetup(selected, true).isEmpty());
    engine_->rememberRestoredDeviceTargets(
        stats->typeName, "System Default", "Saved Output");

    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceStopped();
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());

    auto fallback = selected;
    fallback.outputDeviceName = "System Default";
    ASSERT_TRUE(manager.setAudioDeviceSetup(fallback, true).isEmpty());
    callback.audioDeviceAboutToStart(manager.getCurrentAudioDevice());

    std::atomic<bool> queueDrained{false};
    ASSERT_TRUE(juce::MessageManager::callAsync([&queueDrained] {
        queueDrained.store(true, std::memory_order_release);
    }));
    ASSERT_TRUE(pumpMessagesUntil(queueDrained));

    juce::AudioDeviceManager::AudioDeviceSetup restored;
    manager.getAudioDeviceSetup(restored);
    EXPECT_EQ(restored.outputDeviceName, "Saved Output");
    EXPECT_EQ(engine_->getDesiredOutputDevice(), "Saved Output");
    EXPECT_FALSE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isInputDeviceLost());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
#endif
}

TEST_F(AudioEngineTest, StoppedInputFallbackClearsStaleOutputMuteAfterRestore) {
#if !JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    ASSERT_NE(juce::MessageManager::getInstance(), nullptr);

    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    stats->deviceNames = { "System Default", "Saved Input" };
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);

    auto selected = managedDuplexSetup("System Default");
    selected.inputDeviceName = "Saved Input";
    ASSERT_TRUE(manager.setAudioDeviceSetup(selected, true).isEmpty());
    engine_->rememberRestoredDeviceTargets(
        stats->typeName, "Saved Input", "System Default");

    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceStopped();
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());

    auto fallback = selected;
    fallback.inputDeviceName = "System Default";
    ASSERT_TRUE(manager.setAudioDeviceSetup(fallback, true).isEmpty());
    callback.audioDeviceAboutToStart(manager.getCurrentAudioDevice());

    std::atomic<bool> queueDrained{false};
    ASSERT_TRUE(juce::MessageManager::callAsync([&queueDrained] {
        queueDrained.store(true, std::memory_order_release);
    }));
    ASSERT_TRUE(pumpMessagesUntil(queueDrained));

    juce::AudioDeviceManager::AudioDeviceSetup restored;
    manager.getAudioDeviceSetup(restored);
    EXPECT_EQ(restored.inputDeviceName, "Saved Input");
    EXPECT_EQ(engine_->getDesiredInputDevice(), "Saved Input");
    EXPECT_FALSE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isInputDeviceLost());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
#endif
}

TEST_F(AudioEngineTest, QueuedFallbackRestoreCannotOverrideNewManualOutput) {
#if !JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    ASSERT_NE(juce::MessageManager::getInstance(), nullptr);

    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->separateInputsAndOutputs = true;
    stats->deviceNames = {
        "System Default", "Saved Output", "New Manual Output"
    };
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);

    auto selected = managedDuplexSetup("System Default");
    selected.outputDeviceName = "Saved Output";
    ASSERT_TRUE(manager.setAudioDeviceSetup(selected, true).isEmpty());
    engine_->rememberRestoredDeviceTargets(
        stats->typeName, "System Default", "Saved Output");

    auto fallback = selected;
    fallback.outputDeviceName = "System Default";
    ASSERT_TRUE(manager.setAudioDeviceSetup(fallback, true).isEmpty());

    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceAboutToStart(manager.getCurrentAudioDevice());

    // The fallback restore is queued for Saved Output. A later successful
    // manual choice must invalidate it even though both devices are available.
    const auto selection = engine_->setOutputDevice("New Manual Output");

    std::atomic<bool> queueDrained{false};
    const bool markerPosted = juce::MessageManager::callAsync([&queueDrained] {
        queueDrained.store(true, std::memory_order_release);
    });
    const bool drained = markerPosted && pumpMessagesUntil(queueDrained);

    ASSERT_TRUE(selection);
    ASSERT_TRUE(markerPosted);
    ASSERT_TRUE(drained);
    EXPECT_EQ(engine_->getDesiredOutputDevice(), "New Manual Output");
    EXPECT_FALSE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isOutputAutoMuted());

    juce::AudioDeviceManager::AudioDeviceSetup actual;
    manager.getAudioDeviceSetup(actual);
    EXPECT_EQ(actual.outputDeviceName, "New Manual Output");
#endif
}

TEST(AudioDeviceRecoveryPolicyTest, EmptyActualDeviceMismatchesSavedTarget) {
    using directpipe::audio_device_recovery_detail::savedTargetMismatchesActualDevice;

    EXPECT_TRUE(savedTargetMismatchesActualDevice(
        "User Selected Output", {}, false));
    EXPECT_TRUE(savedTargetMismatchesActualDevice(
        "User Selected Output", "System Default", false));
    EXPECT_FALSE(savedTargetMismatchesActualDevice(
        "User Selected Output", "User Selected Output", false));
    EXPECT_FALSE(savedTargetMismatchesActualDevice(
        {}, "System Default", false));
    EXPECT_FALSE(savedTargetMismatchesActualDevice(
        "User Selected Output", {}, true));
}

TEST(AudioDeviceRecoveryPolicyTest, RestoredEndpointIdentityMustMatchSavedTargets) {
    using directpipe::audio_device_recovery_detail::restoredDeviceTargetsMatch;

    EXPECT_TRUE(restoredDeviceTargetsMatch(
        "Saved Input", "Saved Output",
        "Saved Input", "Saved Output", false));
    EXPECT_FALSE(restoredDeviceTargetsMatch(
        "First Default", "Saved Output",
        "Saved Input", "Saved Output", false));
    EXPECT_FALSE(restoredDeviceTargetsMatch(
        "Saved Input", "First Default",
        "Saved Input", "Saved Output", false));
    EXPECT_TRUE(restoredDeviceTargetsMatch(
        "Saved Input", "First Default",
        "Saved Input", "Saved Output", true));
    EXPECT_TRUE(restoredDeviceTargetsMatch(
        "Any Input", "Any Output", {}, {}, false));
}

TEST_F(AudioEngineTest, OutputNoneRejectsQueuedOutputSnapshot) {
#if !JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    ASSERT_NE(juce::MessageManager::getInstance(), nullptr);

    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->deviceName = "System Default";
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName), true).isEmpty());

    juce::AudioIODeviceCallback& callback = *engine_;
    callback.audioDeviceAboutToStart(manager.getCurrentAudioDevice());
    engine_->setOutputNone(true);

    std::atomic<bool> queueDrained{false};
    ASSERT_TRUE(juce::MessageManager::callAsync([&queueDrained] {
        queueDrained.store(true, std::memory_order_release);
    }));
    ASSERT_TRUE(pumpMessagesUntil(queueDrained));

    EXPECT_TRUE(engine_->isOutputNone());
    EXPECT_TRUE(engine_->getDesiredOutputDevice().isEmpty());
    EXPECT_EQ(engine_->getDesiredInputDevice(), "System Default");
#endif
}

TEST_F(AudioEngineTest, IntentionalAsioRestartInvalidatesQueuedRuntimeSnapshot) {
#if !JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    ASSERT_NE(juce::MessageManager::getInstance(), nullptr);

    auto stats = std::make_shared<ManagedFakeDeviceStats>();
    stats->typeName = "ASIO Test";
    stats->deviceName = "ASIO Device";
    auto& manager = engine_->getAudioDeviceManagerForTest();
    addManagedFakeDeviceType(manager, stats);
    ASSERT_TRUE(manager.setAudioDeviceSetup(
        managedDuplexSetup(stats->deviceName, 48000.0, 128), true).isEmpty());

    // addAudioCallback queues the 128-sample non-intentional snapshot. The
    // setter then performs an intentional restart of the same ASIO endpoint.
    manager.addAudioCallback(engine_.get());
    const auto bufferResult = engine_->setBufferSize(256);

    std::atomic<bool> queueDrained{false};
    const bool markerPosted = juce::MessageManager::callAsync([&queueDrained] {
        queueDrained.store(true, std::memory_order_release);
    });
    const bool drained = markerPosted && pumpMessagesUntil(queueDrained);

    const auto actualBuffer = manager.getCurrentAudioDevice()
        ? manager.getCurrentAudioDevice()->getCurrentBufferSizeSamples()
        : 0;
    const auto runtimeBuffer = engine_->getCurrentBufferSizeForTest();
    const auto desiredBuffer = engine_->getDesiredBufferSize();
    manager.removeAudioCallback(engine_.get());

    ASSERT_TRUE(bufferResult);
    ASSERT_TRUE(markerPosted);
    ASSERT_TRUE(drained);
    EXPECT_EQ(actualBuffer, 256);
    EXPECT_EQ(runtimeBuffer, 256);
    EXPECT_EQ(desiredBuffer, 256);
#endif
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

TEST_F(AudioEngineTest, ReadyFallbackReconcilesBothSavedDeviceDirections) {
    engine_->rememberRestoredDeviceTargets(
        {}, "Saved Input", "Saved Output");

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = "Unexpected Input";
    setup.outputDeviceName = "Saved Output";

    EXPECT_FALSE(engine_->clearDeviceLossAfterReadyForTest(setup));
    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_TRUE(engine_->isInputDeviceLost());
    EXPECT_FALSE(engine_->isOutputAutoMuted());

    setup.inputDeviceName = "Saved Input";
    setup.outputDeviceName = "Unexpected Output";

    EXPECT_FALSE(engine_->clearDeviceLossAfterReadyForTest(setup));
    EXPECT_TRUE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isInputDeviceLost());
    EXPECT_TRUE(engine_->isOutputAutoMuted());

    setup.outputDeviceName = "Saved Output";

    EXPECT_TRUE(engine_->clearDeviceLossAfterReadyForTest(setup));
    EXPECT_FALSE(engine_->isDeviceLost());
    EXPECT_FALSE(engine_->isInputDeviceLost());
    EXPECT_FALSE(engine_->isOutputAutoMuted());
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

TEST_F(AudioEngineTest, ExplicitChannelMaskMustFullyMatchTheRequiredPair) {
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

    EXPECT_FALSE(engine_->hasUsableActiveChannelsForTest(setup, &device));

    setup.inputChannels.setBit(1);
    setup.outputChannels.setBit(1);
    juce::BigInteger activePair;
    activePair.setBit(0);
    activePair.setBit(1);
    FakeAudioIODevice pairDevice(
        "Fake Duplex", inputs, outputs, activePair, activePair);
    EXPECT_TRUE(engine_->hasUsableActiveChannelsForTest(setup, &pairDevice));

    juce::StringArray monoInputs { "Mic" };
    juce::StringArray monoOutputs { "Speaker" };
    juce::BigInteger monoActive;
    monoActive.setBit(0);
    juce::AudioDeviceManager::AudioDeviceSetup monoSetup;
    monoSetup.inputDeviceName = "Fake Mono";
    monoSetup.outputDeviceName = "Fake Mono";
    monoSetup.useDefaultInputChannels = false;
    monoSetup.useDefaultOutputChannels = false;
    monoSetup.inputChannels = monoActive;
    monoSetup.outputChannels = monoActive;
    FakeAudioIODevice monoDevice(
        "Fake Mono", monoInputs, monoOutputs, monoActive, monoActive);
    EXPECT_TRUE(engine_->hasUsableActiveChannelsForTest(monoSetup, &monoDevice));
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
