// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#include <gtest/gtest.h>
#include "../host/Source/Audio/BuiltinNoiseRemoval.h"
#include <array>
#include <cmath>
#include <vector>
#include <limits>
#include <thread>
#include <chrono>

using namespace directpipe;

namespace directpipe {
struct NoiseRemovalTestAccess {
    static void seedNearCounterWrap(BuiltinNoiseRemoval& processor)
    {
        // The freshly prepared output queue contains only priming zeros.
        const uint32_t start = UINT32_MAX - 240;
        processor.outputFifoReadL_ = start;
        processor.outputFifoWriteL_ = start + 480u;
    }
};
}

// Helper: generate mono or stereo sine wave at given frequency
static void fillSine(juce::AudioBuffer<float>& buffer, float freq, double sampleRate) {
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(ch, i, std::sin(2.0f * juce::MathConstants<float>::pi * freq * i / static_cast<float>(sampleRate)));
}

class BuiltinNoiseRemovalTest : public ::testing::Test {
protected:
    BuiltinNoiseRemoval nr;
    static constexpr double kSampleRate = 48000.0;
    static constexpr int kBlockSize = 512;

    void SetUp() override {
        nr.prepareToPlay(kSampleRate, kBlockSize);
    }
};

// 1. DefaultState: After prepareToPlay(48000, 512), verify defaults
TEST_F(BuiltinNoiseRemovalTest, DefaultState) {
    EXPECT_EQ(nr.getLatencySamples(), 1440);
    EXPECT_FALSE(nr.needsResampling());
    EXPECT_EQ(nr.getStrength(), 1);  // Standard
}

// Non-native rates run the 48 kHz processor through streaming conversion.
TEST_F(BuiltinNoiseRemovalTest, Non48kProcessesAudio) {
    BuiltinNoiseRemoval nr44;
    nr44.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buf(2, 512);
    fillSine(buf, 1000.0f, 44100.0);

    // Copy original for comparison
    juce::AudioBuffer<float> original(2, 512);
    for (int ch = 0; ch < 2; ++ch)
        original.copyFrom(ch, 0, buf, ch, 0, 512);

    juce::MidiBuffer midi;
    nr44.processBlock(buf, midi);

    EXPECT_TRUE(nr44.isActive());
    EXPECT_EQ(nr44.getMode(), BuiltinNoiseRemoval::Mode::Resampled);
    EXPECT_GT(nr44.getLatencySamples(), 441);
    // Initial output is buffered processing, not unchanged dry input.
    bool changed = false;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            changed |= buf.getSample(ch, i) != original.getSample(ch, i);
    EXPECT_TRUE(changed);
}

// 3. VADThresholds: setStrength maps to correct VAD thresholds
TEST_F(BuiltinNoiseRemovalTest, VADThresholds) {
    // Light
    nr.setStrength(0);
    EXPECT_EQ(nr.getStrength(), 0);
    EXPECT_FLOAT_EQ(nr.getVadThreshold(), 0.50f);

    // Standard
    nr.setStrength(1);
    EXPECT_EQ(nr.getStrength(), 1);
    EXPECT_FLOAT_EQ(nr.getVadThreshold(), 0.70f);

    // Aggressive
    nr.setStrength(2);
    EXPECT_EQ(nr.getStrength(), 2);
    EXPECT_FLOAT_EQ(nr.getVadThreshold(), 0.90f);

    // Verify via getStateInformation JSON
    nr.setStrength(2);
    juce::MemoryBlock state;
    nr.getStateInformation(state);
    auto json = juce::String::fromUTF8(static_cast<const char*>(state.getData()),
                                        static_cast<int>(state.getSize()));
    auto parsed = juce::JSON::parse(json);
    auto* obj = parsed.getDynamicObject();
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(static_cast<int>(obj->getProperty("strength")), 2);
    EXPECT_NEAR(static_cast<double>(obj->getProperty("vadThreshold")), 0.90, 0.01);
}

// 4. StateRoundtrip: save/restore strength via state serialization
TEST_F(BuiltinNoiseRemovalTest, StateRoundtrip) {
    nr.setStrength(2);  // Aggressive

    // Save state
    juce::MemoryBlock state;
    nr.getStateInformation(state);

    // Create new instance, restore
    BuiltinNoiseRemoval restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    EXPECT_EQ(restored.getStrength(), 2);
    EXPECT_FLOAT_EQ(restored.getVadThreshold(), 0.90f);
}

// FIFO + vendored RNNoise delay, plus conversion at non-native rates.
TEST_F(BuiltinNoiseRemovalTest, LatencyReport) {
    // 48kHz — active processing, FIFO latency
    EXPECT_EQ(nr.getLatencySamples(), 1440);

    // Non-48kHz — FIFO plus input/output conversion
    BuiltinNoiseRemoval nr44;
    nr44.prepareToPlay(44100.0, 512);
    EXPECT_EQ(nr44.getLatencySamples(), 1448);
}

// 6. MonoBuffer: processBlock with 1-channel buffer should not crash
TEST_F(BuiltinNoiseRemovalTest, MonoBuffer) {
    juce::AudioBuffer<float> buf(1, 1024);
    fillSine(buf, 440.0f, kSampleRate);

    juce::MidiBuffer midi;
    // Should not crash — processes channel 0 only
    nr.processBlock(buf, midi);
    SUCCEED();
}

// 7. StereoBuffer: processBlock with 2-channel buffer should not crash (dual mono)
TEST_F(BuiltinNoiseRemovalTest, StereoBuffer) {
    juce::AudioBuffer<float> buf(2, 1024);
    fillSine(buf, 440.0f, kSampleRate);

    juce::MidiBuffer midi;
    // Should not crash — dual-mono processing on both channels
    nr.processBlock(buf, midi);
    SUCCEED();
}

// 8. FifoCounterOverflowDoesNotSilence: uint32_t wraparound must not cause permanent silence
TEST(BuiltinNoiseRemovalFifoTest, FifoCounterOverflowDoesNotSilence)
{
    // Simulate uint32_t near-overflow: counters wrap past UINT32_MAX
    constexpr uint32_t kFifoCapacity = 960;
    std::vector<float> fifo(kFifoCapacity, 0.0f);

    uint32_t readPos  = UINT32_MAX - 5;
    uint32_t writePos = UINT32_MAX - 5;

    // Write 480 samples (one RNNoise frame)
    for (int j = 0; j < 480; ++j) {
        fifo[static_cast<size_t>(writePos % kFifoCapacity)] = 0.5f;
        ++writePos;
    }
    // writePos has wrapped past UINT32_MAX

    // Drain using FIXED comparison: (write - read) > 0u
    int samplesRead = 0;
    for (int i = 0; i < 480; ++i) {
        if ((writePos - readPos) > 0u) {
            float val = fifo[static_cast<size_t>(readPos % kFifoCapacity)];
            EXPECT_NEAR(val, 0.5f, 1e-6f);
            ++readPos;
            ++samplesRead;
        }
    }
    EXPECT_EQ(samplesRead, 480);

    // Verify the OLD comparison would have failed
    uint32_t oldRead  = UINT32_MAX - 5;
    uint32_t oldWrite = writePos;
    EXPECT_FALSE(oldRead < oldWrite);  // BUG: false even though data exists
}

namespace {

std::vector<float> processNoiseRemovalSequence(const std::vector<float>& input,
                                               int blockSize)
{
    EXPECT_EQ(input.size() % static_cast<size_t>(blockSize), 0u);

    BuiltinNoiseRemoval processor;
    processor.prepareToPlay(48000.0, blockSize);

    std::vector<float> output(input.size(), 0.0f);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block(1, blockSize);

    for (size_t offset = 0; offset < input.size(); offset += static_cast<size_t>(blockSize)) {
        for (int i = 0; i < blockSize; ++i)
            block.setSample(0, i, input[offset + static_cast<size_t>(i)]);

        processor.processBlock(block, midi);

        for (int i = 0; i < blockSize; ++i)
            output[offset + static_cast<size_t>(i)] = block.getSample(0, i);
    }

    return output;
}

} // namespace

TEST(BuiltinNoiseRemovalFifoTest, FixedLatencyAndOrderingAreBlockSizeInvariantThrough4096)
{
    // 61,440 is divisible by every callback size below and by RNNoise's
    // 480-sample frame, so all processors receive the exact same sample stream.
    constexpr size_t totalSamples = 61440;
    constexpr std::array<int, 7> blockSizes{128, 256, 480, 512, 1024, 2048, 4096};

    std::vector<float> input(totalSamples);
    for (size_t i = 0; i < input.size(); ++i) {
        const auto phase = static_cast<float>(i);
        input[i] = 0.22f * std::sin(phase * 0.031f)
                 + 0.07f * std::sin(phase * 0.113f);
    }

    const auto reference = processNoiseRemovalSequence(input, 480);
    ASSERT_EQ(reference.size(), input.size());

    // FIFO priming guarantees one exact zero frame. The model's additional
    // nominal group delay can include filter pre-ringing; signal alignment is
    // verified separately by the speech/noise runtime probe.
    for (int i = 0; i < 480; ++i)
        ASSERT_FLOAT_EQ(reference[static_cast<size_t>(i)], 0.0f);

    bool observedProcessedAudio = false;
    for (size_t i = 480; i < reference.size(); ++i)
        observedProcessedAudio = observedProcessedAudio || std::abs(reference[i]) > 1.0e-7f;
    ASSERT_TRUE(observedProcessedAudio);

    for (const int blockSize : blockSizes) {
        const auto actual = processNoiseRemovalSequence(input, blockSize);
        ASSERT_EQ(actual.size(), reference.size());
        for (size_t i = 0; i < reference.size(); ++i) {
            ASSERT_NEAR(actual[i], reference[i], 1.0e-7f)
                << "blockSize=" << blockSize << " sample=" << i;
        }
    }
}

TEST(BuiltinNoiseRemovalConversionTest, RatesBlocksAndChannelsAreIndependent)
{
    // The same stream must produce the same samples regardless of host callback
    // partitioning, including callbacks larger than the prepare hint.
    for (const double rate : {8000.0, 16000.0, 32000.0, 44100.0, 88200.0, 96000.0, 192000.0}) {
        constexpr int total = 8192;
        auto run = [&](int blockSize) {
            BuiltinNoiseRemoval processor;
            processor.prepareToPlay(rate, 128);
            processor.setVADThreshold(0.0f);
            EXPECT_TRUE(processor.isActive());
            std::vector<float> result(total);
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> block(2, blockSize);
            for (int offset = 0; offset < total; offset += blockSize) {
                for (int i = 0; i < blockSize; ++i) {
                    block.setSample(0, i, 0.3f * std::sin(static_cast<float>((offset + i) * 700.0 * 6.283185307179586 / rate)));
                    block.setSample(1, i, 0.0f);
                }
                processor.processBlock(block, midi);
                for (int i = 0; i < blockSize; ++i) {
                    result[static_cast<size_t>(offset + i)] = block.getSample(0, i);
                    EXPECT_TRUE(std::isfinite(block.getSample(0, i)));
                    EXPECT_FLOAT_EQ(block.getSample(1, i), 0.0f) << "rate=" << rate;
                }
            }
            return result;
        };
        const auto reference = run(128);
        bool audible = false;
        for (const auto value : reference) audible |= std::abs(value) > 1.0e-6f;
        EXPECT_TRUE(audible) << "rate=" << rate;
        for (const int blockSize : {1, 512, 4096}) {
            const auto actual = run(blockSize);
            for (int i = 0; i < total; ++i)
                ASSERT_FLOAT_EQ(actual[static_cast<size_t>(i)], reference[static_cast<size_t>(i)])
                    << "rate=" << rate << " block=" << blockSize << " sample=" << i;
        }
    }
}

TEST(BuiltinNoiseRemovalFifoTest, ActualProcessorPreservesOrderedAudioAcrossCounterWrap)
{
    BuiltinNoiseRemoval normal, wrapped;
    normal.prepareToPlay(48000, 128);
    wrapped.prepareToPlay(48000, 128);
    normal.setVADThreshold(0);
    wrapped.setVADThreshold(0);
    NoiseRemovalTestAccess::seedNearCounterWrap(wrapped);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> a(1, 128), b(1, 128);
    for (int offset = 0; offset < 16384; offset += 128) {
        for (int i = 0; i < 128; ++i) {
            const float value = 0.23f * std::sin((offset + i) * 0.037f)
                              + 0.08f * std::cos((offset + i) * 0.119f);
            a.setSample(0, i, value);
            b.setSample(0, i, value);
        }
        normal.processBlock(a, midi);
        wrapped.processBlock(b, midi);
        for (int i = 0; i < 128; ++i)
            ASSERT_FLOAT_EQ(a.getSample(0, i), b.getSample(0, i)) << offset + i;
    }
}

TEST(BuiltinNoiseRemovalConversionTest, UnsupportedReleaseAndReprepareHaveTruthfulStatus)
{
    BuiltinNoiseRemoval processor;
    EXPECT_FALSE(processor.isActive());
    for (const double rate : {0.0, -1.0, 384000.0, std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity()}) {
        processor.prepareToPlay(rate, 128);
        EXPECT_FALSE(processor.isActive());
        EXPECT_EQ(processor.getLatencySamples(), 0);
        juce::AudioBuffer<float> block(1, 4);
        for (int i = 0; i < 4; ++i) block.setSample(0, i, 0.2f);
        juce::MidiBuffer midi;
        processor.processBlock(block, midi);
        EXPECT_FLOAT_EQ(block.getSample(0, 0), 0.2f);
    }
    for (const double rate : {44100.0, 48000.0, 96000.0, 48000.0}) {
        processor.prepareToPlay(rate, 128);
        EXPECT_TRUE(processor.isActive());
        EXPECT_EQ(processor.needsResampling(), rate != 48000.0);
        processor.releaseResources();
        EXPECT_FALSE(processor.isActive());
        EXPECT_EQ(processor.getLatencySamples(), 0);
    }
}

TEST(StreamingSincResamplerTest, AntiAliasingRejectsAboveNyquistWithoutChangingPassbandGain)
{
    auto rms = [](double frequency) {
        StreamingSincResampler converter;
        converter.prepare(96000.0, 48000.0);
        double power = 0.0;
        int measured = 0, count = 0;
        for (int i = 0; i < 96000; ++i) {
            converter.push(static_cast<float>(std::sin(6.283185307179586 * frequency * i / 96000.0)));
            while (converter.canRead()) {
                const auto value = converter.read();
                if (++count > 1000) { power += value * value; ++measured; }
            }
        }
        return std::sqrt(power / measured);
    };
    EXPECT_NEAR(rms(1000.0), std::sqrt(0.5), 0.001);
    EXPECT_LT(rms(30000.0), 0.001);
}

TEST(StreamingSincResamplerTest, LongFractionalClockDoesNotLoseSamples)
{
    StreamingSincResampler converter;
    converter.prepare(44100.0, 48000.0);
    constexpr int inputSamples = 441000;
    int outputSamples = 0;
    for (int i = 0; i < inputSamples; ++i) {
        converter.push(0.25f);
        while (converter.canRead()) {
            const float value = converter.read();
            if (i > 256) ASSERT_NEAR(value, 0.25f, 0.00001f);
            ++outputSamples;
        }
    }
    const auto expected = static_cast<int>(std::floor(
        (inputSamples - 1.0 - StreamingSincResampler::kHalfLength) * 48000.0 / 44100.0)) + 1;
    EXPECT_EQ(outputSamples, expected);
}

TEST(BuiltinNoiseRemovalConversionTest, OpenEditorRefreshesStatusAfterRateChange)
{
    juce::ScopedJuceInitialiser_GUI initialiser;
    BuiltinNoiseRemoval processor;
    processor.prepareToPlay(48000.0, 128);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto visibleStatus = [&] {
        for (auto* child : editor->getChildren())
            if (auto* label = dynamic_cast<juce::Label*>(child))
                if (label->getText().startsWith("Active:") || label->getText().startsWith("Inactive:"))
                    return label->getText();
        return juce::String();
    };
    EXPECT_TRUE(visibleStatus().contains("native 48"));
    auto awaitStatus = [&](const juce::String& expected) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        do {
            juce::Timer::callPendingTimersSynchronously();
            if (visibleStatus().contains(expected)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    };
    processor.prepareToPlay(44100.0, 128);
    EXPECT_TRUE(awaitStatus("44.1 -> 48 -> 44.1"));
    processor.releaseResources();
    EXPECT_TRUE(awaitStatus("Inactive:"));
}

// Opt-in evidence run: local fixture WAV -> real RNNoise -> WAV and timing JSON.
// This is offline DSP execution, not a physical-device acceptance test.
TEST(BuiltinNoiseRemovalRuntimeEvidence, RenderSpeechAndNoiseAtEightRates)
{
    const auto location = juce::SystemStats::getEnvironmentVariable("DIRECTPIPE_NOISE_EVIDENCE_DIR", {});
    if (location.isEmpty()) GTEST_SKIP() << "Set DIRECTPIPE_NOISE_EVIDENCE_DIR to prepared local fixtures.";
    const juce::File directory(location);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    juce::Array<juce::var> results;
    for (const int rate : {8000, 16000, 32000, 44100, 48000, 88200, 96000, 192000}) {
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(
            directory.getChildFile("input-" + juce::String(rate) + ".wav")));
        ASSERT_NE(reader, nullptr);
        ASSERT_EQ(reader->sampleRate, static_cast<double>(rate));
        const int samples = static_cast<int>(reader->lengthInSamples);
        constexpr int blockSize = 128;
        const int padded = ((samples + blockSize - 1) / blockSize + 16) * blockSize;
        juce::AudioBuffer<float> input(1, padded), output(1, padded), block(1, blockSize);
        input.clear();
        ASSERT_TRUE(reader->read(&input, 0, samples, 0, true, false));
        BuiltinNoiseRemoval processor;
        processor.prepareToPlay(rate, blockSize);
        ASSERT_TRUE(processor.isActive());
        juce::MidiBuffer midi;
        double totalSeconds = 0.0, maxSeconds = 0.0;
        for (int offset = 0; offset < padded; offset += blockSize) {
            block.copyFrom(0, 0, input, 0, offset, blockSize);
            const auto before = std::chrono::steady_clock::now();
            processor.processBlock(block, midi);
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - before).count();
            totalSeconds += seconds;
            maxSeconds = (std::max)(maxSeconds, seconds);
            output.copyFrom(0, offset, block, 0, 0, blockSize);
        }
        juce::WavAudioFormat wav;
        auto stream = directory.getChildFile("output-" + juce::String(rate) + ".wav").createOutputStream();
        ASSERT_NE(stream, nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.release(), rate, 1, 24, {}, 0));
        ASSERT_NE(writer, nullptr);
        ASSERT_TRUE(writer->writeFromAudioSampleBuffer(output, 0, padded));
        writer.reset();
        auto row = std::make_unique<juce::DynamicObject>();
        row->setProperty("rate", rate);
        row->setProperty("samples", samples);
        row->setProperty("reported_latency_samples", processor.getLatencySamples());
        row->setProperty("status", processor.getStatusText());
        row->setProperty("processing_seconds", totalSeconds);
        row->setProperty("audio_seconds", static_cast<double>(padded) / rate);
        row->setProperty("max_block_ms", maxSeconds * 1000.0);
        results.add(juce::var(row.release()));
    }
    ASSERT_TRUE(directory.getChildFile("processing.json").replaceWithText(juce::JSON::toString(results)));
}
