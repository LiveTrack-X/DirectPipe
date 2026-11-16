// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include "StreamingStftDenoiser.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace directpipe {

// prepare/release require rendering to be excluded, as for other JUCE processors.
// The rendering thread, or lifecycle methods with rendering excluded, own cores
// and manual_. Control/state methods use atomic mailboxes; neither mailbox reader
// nor writer locks RT.
class BuiltinNoiseRemovalDSP final : public juce::AudioProcessor {
public:
    enum class Mode { Learn = 0, Realtime = 1 };
    static constexpr int kSpectrumMaxBins = 513;
    struct SpectrumSnapshot {
        uint64_t generation = 0;
        double sampleRate = 0;
        int fftSize = 0, bins = 0;
        bool active = false, learning = false, realtime = false;
        std::array<bool, 2> profileReady{};
        std::array<std::array<float, kSpectrumMaxBins>, 2> inputPower{}, noisePower{}, gains{};
    };
    // UI/control reader: one bounded coherent attempt. False means contention.
    bool getSpectrumSnapshot(SpectrumSnapshot&) const noexcept;
    BuiltinNoiseRemovalDSP();
    ~BuiltinNoiseRemovalDSP() override = default;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void setMode(Mode mode);
    Mode getMode() const { return requestedMode_.load(std::memory_order_relaxed); }
    void startLearning();
    bool isLearning() const;
    float getLearnProgress() const { return progress_.load(std::memory_order_relaxed); }
    bool hasLearnedProfile() const { return compatibleProfile_.load(std::memory_order_acquire); }
    void setReductionDb(float reduction);
    float getReductionDb() const { return reduction_.load(std::memory_order_relaxed); }
    bool isActive() const { return active_.load(std::memory_order_acquire); }
    juce::String getStatusText() const;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    const juce::String getName() const override { return "NoiseRemover (DSP)"; }
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        const auto in = layouts.getMainInputChannelSet();
        return in == layouts.getMainOutputChannelSet()
            && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

private:
    struct SpectrumMailbox {
        SpectrumMailbox() noexcept;
        void publish(const SpectrumSnapshot&) noexcept;
        std::atomic<uint64_t> sequence{0}, generation{0};
        std::atomic<double> sampleRate{0};
        std::atomic<int> fftSize{0}, bins{0}, flags{0};
        using ChannelData = std::array<std::array<std::atomic<float>, kSpectrumMaxBins>, 2>;
        ChannelData inputPower, noisePower, gains;
    };
    void publishSpectrum(bool includeAnalysis) noexcept;
    SpectrumMailbox spectrum_;
    uint64_t spectrumGeneration_ = 0; // Single RT/lifecycle writer; rendering excluded in lifecycle.
    int spectrumCountdown_ = 0;
    static constexpr int kMaxBins = 513;
    struct Profile {
        uint64_t generation = 0, cancelLearnThrough = 0;
        double rate = 0;
        int fftSize = 0, bins = 0;
        std::array<std::array<float, kMaxBins>, 2> spectra{};
    };
    // Two separate single-writer atomic seqlocks: restore writer is serialized
    // on the control side; completed writer is exclusively RT (or lifecycle with
    // rendering excluded). Atomic payloads prevent C++ data races on retries.
    struct ProfileMailbox {
        ProfileMailbox() noexcept;
        void publish(const Profile&) noexcept;
        bool read(Profile&, uint64_t& sequence) const noexcept;
        std::atomic<uint64_t> sequence{0}, generation{0}, cancelLearnThrough{0};
        std::atomic<double> rate{0};
        std::atomic<int> fftSize{0}, bins{0};
        std::array<std::array<std::atomic<float>, kMaxBins>, 2> spectra;
    };
    static int fftSizeForRate(double) noexcept;
    static bool validProfile(const Profile&) noexcept;
    bool compatible(const Profile&) const noexcept;
    Profile snapshotProfile() const;
    void loadManualIntoCores() noexcept;
    void cancelLearning() noexcept;
    void processSegment(juce::AudioBuffer<float>&, int offset, int count, bool learn) noexcept;
    void completeLearning() noexcept;

    std::array<StreamingStftDenoiser, 2> cores_;
    Profile manual_;
    ProfileMailbox pendingRestore_, completed_;
    mutable std::mutex stateMutex_; // Never used by processBlock.
    uint64_t nextRestoreGeneration_ = 0; // Protected by stateMutex_.
    uint64_t seenRestoreSequence_ = 0, lastLearnRequest_ = 0;
    double preparedRate_ = 0;
    int preparedFft_ = 0, captureTarget_ = 0, captured_ = 0;
    float appliedReduction_ = 12;
    bool capturing_ = false;
    Mode renderMode_ = Mode::Learn;
    std::atomic<Mode> requestedMode_{Mode::Learn};
    std::atomic<float> reduction_{12}, progress_{0};
    std::atomic<bool> active_{false}, unsupported_{false}, learning_{false}, compatibleProfile_{false};
    std::atomic<double> statusRate_{0};
    std::atomic<int> statusLatency_{0};
    std::atomic<uint64_t> learnRequest_{0}, handledLearnRequest_{0};
};
}
