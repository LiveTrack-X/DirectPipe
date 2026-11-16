// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "../host/Source/Audio/BuiltinNoiseRemovalDSP.h"
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

using directpipe::BuiltinNoiseRemovalDSP;

namespace {
void render(BuiltinNoiseRemovalDSP& processor, int samples, float left = 0.02f, float right = 0.01f)
{
    juce::AudioBuffer<float> block(2, samples);
    for (int i = 0; i < samples; ++i) {
        block.setSample(0, i, left);
        block.setSample(1, i, right);
    }
    juce::MidiBuffer midi;
    processor.processBlock(block, midi);
}

juce::var stateOf(BuiltinNoiseRemovalDSP& processor)
{
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    return juce::JSON::parse(juce::String::fromUTF8(static_cast<const char*>(state.getData()),
                                                 static_cast<int>(state.getSize())));
}

void restore(BuiltinNoiseRemovalDSP& processor, const juce::var& state)
{
    const auto text = juce::JSON::toString(state);
    processor.setStateInformation(text.toRawUTF8(), static_cast<int>(text.getNumBytesAsUTF8()));
}

juce::String profileText(BuiltinNoiseRemovalDSP& processor)
{
    return juce::JSON::toString(stateOf(processor)["profile"]);
}
}

TEST(BuiltinNoiseRemovalDSPTest, DefaultsAndNativeRatesHaveExactDelayedUnityInBothModes)
{
    constexpr std::array<double, 8> rates{8000, 16000, 32000, 44100, 48000, 88200, 96000, 192000};
    constexpr std::array<int, 8> sizes{64, 128, 256, 256, 256, 512, 512, 1024};
    for (size_t rateIndex = 0; rateIndex < rates.size(); ++rateIndex) {
        for (const auto mode : {BuiltinNoiseRemovalDSP::Mode::Learn, BuiltinNoiseRemovalDSP::Mode::Realtime}) {
            BuiltinNoiseRemovalDSP processor;
            EXPECT_EQ(processor.getMode(), BuiltinNoiseRemovalDSP::Mode::Learn);
            EXPECT_FLOAT_EQ(processor.getReductionDb(), 12.0f);
            processor.setMode(mode);
            processor.setReductionDb(0);
            processor.prepareToPlay(rates[rateIndex], 17);
            EXPECT_EQ(processor.getLatencySamples(), sizes[rateIndex]);
            EXPECT_TRUE(processor.isActive());
            juce::MidiBuffer midi;
            constexpr int total = 8192;
            std::vector<float> input(total), output(total);
            for (int i = 0; i < total - 2048; ++i)
                input[static_cast<size_t>(i)] = 0.19f * std::sin(i * 0.127f) + (i == 0 ? 0.4f : 0);
            int offset = 0;
            for (const int hint : {0, 1, 17, 63, 513, 4097, 3501}) {
                const int count = (std::min)(hint, total - offset);
                juce::AudioBuffer<float> block(2, count);
                block.clear();
                for (int i = 0; i < count; ++i) block.setSample(0, i, input[static_cast<size_t>(offset + i)]);
                processor.processBlock(block, midi);
                for (int i = 0; i < count; ++i) {
                    output[static_cast<size_t>(offset + i)] = block.getSample(0, i);
                    ASSERT_FLOAT_EQ(block.getSample(1, i), 0.0f);
                }
                offset += count;
            }
            ASSERT_EQ(offset, total);
            for (int i = 0; i < total; ++i)
                ASSERT_NEAR(output[static_cast<size_t>(i)], i >= sizes[rateIndex]
                    ? input[static_cast<size_t>(i - sizes[rateIndex])] : 0, 2.0e-6f)
                    << "rate=" << rates[rateIndex] << " sample=" << i;
        }
    }
}

TEST(BuiltinNoiseRemovalDSPTest, MissingManualProfileUsesDelayedUnityAtDefaultReduction)
{
    BuiltinNoiseRemovalDSP processor;
    processor.prepareToPlay(48000, 256);
    EXPECT_FALSE(processor.hasLearnedProfile());
    EXPECT_TRUE(processor.getStatusText().contains("Learn required"));
    juce::AudioBuffer<float> block(1, 2048);
    block.clear();
    block.setSample(0, 7, 0.8f);
    juce::MidiBuffer midi;
    processor.processBlock(block, midi);
    for (int i = 0; i < 2048; ++i)
        EXPECT_NEAR(block.getSample(0, i), i == 263 ? 0.8f : 0, 2.0e-6f);
}

TEST(BuiltinNoiseRemovalDSPTest, SpectrumTelemetryExposesNormalizedActualAudioAndMask)
{
    BuiltinNoiseRemovalDSP processor;
    processor.setReductionDb(0);
    processor.prepareToPlay(48000, 128);
    BuiltinNoiseRemovalDSP::SpectrumSnapshot snapshot;
    juce::AudioBuffer<float> tone(2, 4096);
    for (int i = 0; i < tone.getNumSamples(); ++i) {
        tone.setSample(0, i, 0.5f * std::sin(juce::MathConstants<double>::twoPi * 1500 * i / 48000));
        tone.setSample(1, i, 0.25f * std::sin(juce::MathConstants<double>::twoPi * 3000 * i / 48000));
    }
    juce::MidiBuffer midi;
    processor.processBlock(tone, midi);
    ASSERT_TRUE(processor.getSpectrumSnapshot(snapshot));
    ASSERT_TRUE(snapshot.active);
    ASSERT_EQ(snapshot.bins, 129);
    EXPECT_EQ(snapshot.fftSize, 256);
    EXPECT_DOUBLE_EQ(snapshot.sampleRate, 48000);
    EXPECT_FALSE(snapshot.profileReady[0]);
    EXPECT_EQ(processor.getLatencySamples(), 256);
    for (int channel = 0; channel < 2; ++channel) {
        double sum = 0;
        int peak = 0;
        for (int bin = 0; bin < snapshot.bins; ++bin) {
            sum += snapshot.inputPower[channel][bin];
            if (snapshot.inputPower[channel][bin] > snapshot.inputPower[channel][peak]) peak = bin;
            EXPECT_FLOAT_EQ(snapshot.gains[channel][bin], 1);
        }
        EXPECT_NEAR(sum, channel == 0 ? 0.125 : 0.03125, 1e-5);
        EXPECT_EQ(peak, channel == 0 ? 8 : 16);
    }
    const auto generation = snapshot.generation;
    ASSERT_TRUE(processor.getSpectrumSnapshot(snapshot));
    EXPECT_EQ(snapshot.generation, generation); // Re-reading cannot fake fresh audio.
    processor.releaseResources();
    ASSERT_TRUE(processor.getSpectrumSnapshot(snapshot));
    EXPECT_FALSE(snapshot.active);
    EXPECT_EQ(snapshot.bins, 0);
}

TEST(BuiltinNoiseRemovalDSPTest, SpectrumShowsFrozenNoiseAgainstChangingInputAndActualZeroMask)
{
    BuiltinNoiseRemovalDSP processor;
    processor.prepareToPlay(8000, 128);
    processor.startLearning();
    render(processor, 16000, 0.02f, 0.01f);
    BuiltinNoiseRemovalDSP::SpectrumSnapshot before, after;
    ASSERT_TRUE(processor.getSpectrumSnapshot(before));
    ASSERT_TRUE(before.active);
    ASSERT_TRUE(before.profileReady[0]);
    ASSERT_TRUE(before.profileReady[1]);
    render(processor, 4096, 0.2f, 0.03f);
    ASSERT_TRUE(processor.getSpectrumSnapshot(after));
    EXPECT_GT(after.generation, before.generation);
    EXPECT_EQ(after.noisePower, before.noisePower);
    EXPECT_NE(after.inputPower, before.inputPower);
    EXPECT_GT(after.inputPower[0][0], before.inputPower[0][0]);
    for (int channel = 0; channel < 2; ++channel)
        for (int bin = 0; bin < after.bins; ++bin) {
            EXPECT_GE(after.gains[channel][bin], std::pow(10.0f, -12.0f / 20.0f) - 1e-5f);
            EXPECT_LE(after.gains[channel][bin], 1);
        }
    processor.setReductionDb(0);
    render(processor, 4096, 0.2f, 0.03f);
    ASSERT_TRUE(processor.getSpectrumSnapshot(after));
    for (int channel = 0; channel < 2; ++channel)
        for (int bin = 0; bin < after.bins; ++bin) EXPECT_FLOAT_EQ(after.gains[channel][bin], 1);
    EXPECT_EQ(processor.getLatencySamples(), 64);
}

TEST(BuiltinNoiseRemovalDSPTest, SpectrumReadersStayCoherentWhileRendering)
{
    BuiltinNoiseRemovalDSP processor;
    processor.prepareToPlay(48000, 73);
    std::thread audio([&] { for (int i = 0; i < 1000; ++i) render(processor, 73); });
    BuiltinNoiseRemovalDSP::SpectrumSnapshot snapshot;
    for (int i = 0; i < 1000; ++i) {
        if (!processor.getSpectrumSnapshot(snapshot) || !snapshot.active) continue;
        EXPECT_EQ(snapshot.fftSize, 256);
        EXPECT_EQ(snapshot.bins, 129);
        EXPECT_DOUBLE_EQ(snapshot.sampleRate, 48000);
        for (int bin = 0; bin < snapshot.bins; ++bin) {
            EXPECT_TRUE(std::isfinite(snapshot.inputPower[0][bin]));
            EXPECT_GE(snapshot.inputPower[0][bin], 0);
            EXPECT_GE(snapshot.gains[0][bin], 0);
            EXPECT_LE(snapshot.gains[0][bin], 1);
        }
    }
    audio.join();
    ASSERT_TRUE(processor.getSpectrumSnapshot(snapshot));
    EXPECT_TRUE(snapshot.active);
    EXPECT_EQ(processor.getLatencySamples(), 256);
}

TEST(BuiltinNoiseRemovalDSPTest, CaptureEndsAtExactlyTwoSecondsAndExcludesPreLearnWindows)
{
    BuiltinNoiseRemovalDSP processor;
    processor.prepareToPlay(8000, 128);
    render(processor, 127, 100.0f, 100.0f);
    processor.startLearning();
    EXPECT_TRUE(processor.isLearning());
    render(processor, 15999, 0.01f, 0.02f);
    EXPECT_TRUE(processor.isLearning());
    EXPECT_FALSE(processor.hasLearnedProfile());
    EXPECT_NEAR(processor.getLearnProgress(), 15999.0f / 16000.0f, 1.0e-6f);
    // Only the first sample belongs to Learn. Loud trailing audio must not be
    // accidentally included when a callback crosses the exact capture end.
    juce::AudioBuffer<float> crossing(2, 500);
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < 500; ++i) crossing.setSample(ch, i, 100.0f);
        crossing.setSample(ch, 0, ch == 0 ? 0.01f : 0.02f);
    }
    juce::MidiBuffer midi;
    processor.processBlock(crossing, midi);
    EXPECT_FALSE(processor.isLearning());
    EXPECT_TRUE(processor.hasLearnedProfile());
    EXPECT_FLOAT_EQ(processor.getLearnProgress(), 1.0f);
    const auto profile = stateOf(processor)["profile"];
    ASSERT_TRUE(profile.isObject());
    const auto* channels = profile["channels"].getArray();
    ASSERT_NE(channels, nullptr);
    ASSERT_EQ(channels->size(), 2);
    for (int ch = 0; ch < 2; ++ch) {
        const auto* bins = channels->getReference(ch).getArray();
        ASSERT_NE(bins, nullptr);
        ASSERT_EQ(bins->size(), 33);
        for (const auto& bin : *bins) EXPECT_LT(static_cast<double>(bin), 1.0);
    }
    const auto frozen = profileText(processor);
    render(processor, 32000, 0.8f, 0.5f);
    EXPECT_EQ(profileText(processor), frozen);
}

TEST(BuiltinNoiseRemovalDSPTest, ManualProfileSurvivesRealtimeAndInterruptedRelearnAndRoundtrip)
{
    BuiltinNoiseRemovalDSP processor;
    processor.prepareToPlay(8000, 128);
    processor.startLearning();
    render(processor, 16000);
    const auto frozen = profileText(processor);
    processor.startLearning();
    render(processor, 7000, 0.4f, 0.3f);
    EXPECT_TRUE(processor.isLearning());
    EXPECT_EQ(profileText(processor), frozen);
    processor.setMode(BuiltinNoiseRemovalDSP::Mode::Realtime);
    render(processor, 32000, 0.5f, 0.4f);
    EXPECT_FALSE(processor.isLearning());
    EXPECT_EQ(profileText(processor), frozen);
    processor.setReductionDb(19);
    auto state = stateOf(processor);
    BuiltinNoiseRemovalDSP restored;
    restore(restored, state);
    EXPECT_EQ(restored.getMode(), BuiltinNoiseRemovalDSP::Mode::Realtime);
    EXPECT_FLOAT_EQ(restored.getReductionDb(), 19);
    EXPECT_EQ(profileText(restored), frozen);
    restored.prepareToPlay(8000, 128);
    render(restored, 1);
    EXPECT_TRUE(restored.hasLearnedProfile());
    restored.setMode(BuiltinNoiseRemovalDSP::Mode::Learn);
    render(restored, 1);
    EXPECT_TRUE(restored.hasLearnedProfile());
    EXPECT_EQ(profileText(restored), frozen);
}

TEST(BuiltinNoiseRemovalDSPTest, UnsupportedReleasedAndMismatchedRateRequireLearn)
{
    BuiltinNoiseRemovalDSP processor;
    processor.prepareToPlay(8000, 128);
    processor.startLearning();
    render(processor, 16000);
    ASSERT_TRUE(processor.hasLearnedProfile());
    processor.prepareToPlay(48000, 128);
    EXPECT_FALSE(processor.hasLearnedProfile());
    EXPECT_TRUE(processor.getStatusText().contains("Learn required"));
    for (double rate : {0.0, 7999.0, 192001.0, std::numeric_limits<double>::quiet_NaN()}) {
        processor.prepareToPlay(rate, 128);
        EXPECT_FALSE(processor.isActive());
        EXPECT_EQ(processor.getLatencySamples(), 0);
        EXPECT_TRUE(processor.getStatusText().contains("Inactive"));
        juce::AudioBuffer<float> block(1, 1);
        block.setSample(0, 0, 0.4f);
        juce::MidiBuffer midi;
        processor.processBlock(block, midi);
        EXPECT_FLOAT_EQ(block.getSample(0, 0), 0.4f);
    }
    processor.prepareToPlay(8000, 128);
    EXPECT_TRUE(processor.hasLearnedProfile());
    processor.releaseResources();
    EXPECT_EQ(processor.getLatencySamples(), 0);
    EXPECT_FALSE(processor.isActive());
}

TEST(BuiltinNoiseRemovalDSPTest, CorruptProfileAndNonfiniteControlsCannotBecomeLearned)
{
    BuiltinNoiseRemovalDSP processor;
    processor.prepareToPlay(8000, 128);
    processor.startLearning();
    render(processor, 16000);
    auto good = stateOf(processor);
    for (const juce::String field : {"rate", "fftSize", "channels"}) {
        auto bad = good.clone();
        bad["profile"].getDynamicObject()->setProperty(field, -1);
        restore(processor, bad);
        render(processor, 1);
        EXPECT_FALSE(processor.hasLearnedProfile());
        EXPECT_TRUE(processor.getStatusText().contains("Learn required"));
    }
    auto negative = good.clone();
    negative["profile"]["channels"].getArray()->getReference(0).getArray()->set(0, -0.1);
    restore(processor, negative);
    render(processor, 1);
    EXPECT_FALSE(processor.hasLearnedProfile());
    processor.setReductionDb(std::numeric_limits<float>::quiet_NaN());
    EXPECT_TRUE(std::isfinite(processor.getReductionDb()));
    processor.setReductionDb(100);
    EXPECT_FLOAT_EQ(processor.getReductionDb(), 24);
    processor.setReductionDb(-10);
    EXPECT_FLOAT_EQ(processor.getReductionDb(), 0);
}

TEST(BuiltinNoiseRemovalDSPTest, ModeTransitionsAndLearnDoNotResetAudioTimeline)
{
    BuiltinNoiseRemovalDSP processor;
    processor.prepareToPlay(8000, 17);
    processor.setReductionDb(0);
    juce::MidiBuffer midi;
    int position = 0;
    for (int blockIndex = 0; blockIndex < 1200; ++blockIndex) {
        if (blockIndex == 3 || blockIndex == 800) processor.startLearning();
        if (blockIndex == 17) processor.setMode(BuiltinNoiseRemovalDSP::Mode::Realtime);
        if (blockIndex == 107) processor.setMode(BuiltinNoiseRemovalDSP::Mode::Learn);
        juce::AudioBuffer<float> block(1, 17);
        for (int i = 0; i < 17; ++i) block.setSample(0, i, 0.2f * std::sin((position + i) * 0.1f));
        processor.processBlock(block, midi);
        for (int i = 0; i < 17; ++i)
            ASSERT_NEAR(block.getSample(0, i), position + i < 64 ? 0 :
                0.2f * std::sin((position + i - 64) * 0.1f), 1.0e-6f);
        position += 17;
    }
}

TEST(BuiltinNoiseRemovalDSPTest, ControlStateSnapshotsRemainCoherentDuringRendering)
{
    BuiltinNoiseRemovalDSP processor;
    processor.prepareToPlay(8000, 128);
    processor.startLearning();
    render(processor, 16000);
    const auto good = stateOf(processor);
    const auto expected = juce::JSON::toString(good["profile"]);
    std::thread audio([&] {
        for (int i = 0; i < 1000; ++i) render(processor, 73);
    });
    for (int i = 0; i < 100; ++i) {
        processor.setMode(i % 2 ? BuiltinNoiseRemovalDSP::Mode::Learn : BuiltinNoiseRemovalDSP::Mode::Realtime);
        processor.setReductionDb(static_cast<float>(i % 25));
        restore(processor, good);
        EXPECT_EQ(profileText(processor), expected);
    }
    audio.join();
}

TEST(BuiltinNoiseRemovalDSPTest, CompletedLearnPublicationCanBeSavedConcurrently)
{
    BuiltinNoiseRemovalDSP processor, reference;
    for (auto* dsp : {&processor, &reference}) {
        dsp->prepareToPlay(8000, 128);
        dsp->startLearning();
        render(*dsp, 16000, 0.01f, 0.02f);
    }
    const auto before = profileText(processor);
    reference.startLearning();
    for (int offset = 0; offset < 16000; offset += 73)
        render(reference, (std::min)(73, 16000 - offset), 0.04f, 0.08f);
    const auto after = profileText(reference);
    ASSERT_NE(before, after);

    processor.startLearning();
    std::atomic<bool> readerReady{false}, finished{false};
    std::thread audio([&] {
        while (!readerReady.load(std::memory_order_acquire)) std::this_thread::yield();
        for (int offset = 0; offset < 16000; offset += 73)
            render(processor, (std::min)(73, 16000 - offset), 0.04f, 0.08f);
        finished.store(true, std::memory_order_release);
    });
    readerReady.store(true, std::memory_order_release);
    do {
        const auto snapshot = profileText(processor);
        EXPECT_TRUE(snapshot == before || snapshot == after)
            << "Saved state must contain a complete old or new profile";
    } while (!finished.load(std::memory_order_acquire));
    audio.join();
    EXPECT_FALSE(processor.isLearning());
    EXPECT_EQ(profileText(processor), after);
}

TEST(BuiltinNoiseRemovalDSPTest, MonoLearningAndFractionalRateCaptureRemainValid)
{
    BuiltinNoiseRemovalDSP processor;
    const auto mono = juce::AudioChannelSet::mono();
    ASSERT_TRUE(processor.setBusesLayout({{mono}, {mono}}));
    processor.prepareToPlay(8000.25, 128);
    processor.startLearning();
    juce::AudioBuffer<float> block(1, 16000);
    for (int i = 0; i < block.getNumSamples(); ++i) block.setSample(0, i, 0.01f);
    juce::MidiBuffer midi;
    processor.processBlock(block, midi);
    EXPECT_TRUE(processor.isLearning());
    EXPECT_FALSE(processor.hasLearnedProfile());
    juce::AudioBuffer<float> last(1, 1);
    last.setSample(0, 0, 0.01f);
    processor.processBlock(last, midi);
    ASSERT_TRUE(processor.hasLearnedProfile());
    EXPECT_FALSE(processor.isLearning());
    const auto saved = stateOf(processor);
    ASSERT_TRUE(saved["profile"].isObject());
    EXPECT_DOUBLE_EQ(static_cast<double>(saved["profile"]["rate"]), 8000.25);
    const auto* channels = saved["profile"]["channels"].getArray();
    ASSERT_NE(channels, nullptr);
    ASSERT_EQ(channels->size(), 2);
    for (const auto& bin : *channels->getReference(1).getArray())
        EXPECT_DOUBLE_EQ(static_cast<double>(bin), 0);
    BuiltinNoiseRemovalDSP restored;
    restore(restored, saved);
    ASSERT_TRUE(restored.setBusesLayout({{mono}, {mono}}));
    restored.prepareToPlay(8000.25, 128);
    EXPECT_TRUE(restored.hasLearnedProfile());
}

TEST(BuiltinNoiseRemovalDSPTest, EditorControlsSelectModesAndShowExactLatency)
{
    juce::ScopedJuceInitialiser_GUI initialiser;
    BuiltinNoiseRemovalDSP processor;
    processor.prepareToPlay(48000, 128);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    juce::ComboBox* mode = nullptr;
    juce::TextButton* learn = nullptr;
    juce::Slider* reduction = nullptr;
    bool latencyVisible = false;
    for (auto* child : editor->getChildren()) {
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child)) mode = combo;
        if (auto* button = dynamic_cast<juce::TextButton*>(child)) learn = button;
        if (auto* slider = dynamic_cast<juce::Slider*>(child)) reduction = slider;
        if (auto* label = dynamic_cast<juce::Label*>(child))
            latencyVisible |= label->getText().contains("256 samples") && label->getText().contains("5.333 ms");
    }
    ASSERT_NE(mode, nullptr);
    ASSERT_NE(learn, nullptr);
    ASSERT_NE(reduction, nullptr);
    EXPECT_TRUE(latencyVisible);
    const auto exportImage = [&](const juce::String& name) {
        const auto path = juce::SystemStats::getEnvironmentVariable("DIRECTPIPE_DSP_EVIDENCE_DIR", {});
        if (path.isEmpty()) return;
        const juce::File directory(path);
        ASSERT_TRUE(directory.createDirectory().wasOk());
        auto stream = directory.getChildFile(name + ".png").createOutputStream();
        ASSERT_NE(stream, nullptr);
        ASSERT_TRUE(stream->setPosition(0));
        ASSERT_TRUE(stream->truncate().wasOk());
        juce::PNGImageFormat png;
        ASSERT_TRUE(png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), *stream));
    };
    exportImage("dsp-learn-required");
    mode->setSelectedId(2, juce::sendNotificationSync);
    EXPECT_EQ(processor.getMode(), BuiltinNoiseRemovalDSP::Mode::Realtime);
    EXPECT_FALSE(processor.getStatusText().contains("Learn required"));
    EXPECT_FALSE(learn->isEnabled());
    exportImage("dsp-realtime");
    mode->setSelectedId(1, juce::sendNotificationSync);
    EXPECT_TRUE(learn->isEnabled());
    learn->onClick();
    EXPECT_TRUE(processor.isLearning());
    reduction->setValue(7, juce::sendNotificationSync);
    EXPECT_FLOAT_EQ(processor.getReductionDb(), 7);
    render(processor, 48000);
    // Force the existing timer once so snapshots include the current progress.
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    juce::Timer::callPendingTimersSynchronously();
    exportImage("dsp-learning");
    render(processor, 48000);
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    juce::Timer::callPendingTimersSynchronously();
    exportImage("dsp-frozen");
    EXPECT_TRUE(learn->isEnabled());
    EXPECT_EQ(learn->getButtonText(), "Relearn");
    processor.releaseResources();
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    juce::Timer::callPendingTimersSynchronously();
    exportImage("dsp-inactive");
    EXPECT_FALSE(learn->isEnabled());
}
