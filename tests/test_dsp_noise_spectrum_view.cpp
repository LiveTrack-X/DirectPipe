// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "../host/Source/UI/DspNoiseSpectrumView.h"
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

using directpipe::BuiltinNoiseRemovalDSP;
using directpipe::DspNoiseSpectrumView;

namespace {
BuiltinNoiseRemovalDSP::SpectrumSnapshot exampleSnapshot()
{
    BuiltinNoiseRemovalDSP::SpectrumSnapshot snapshot;
    snapshot.active = true;
    snapshot.sampleRate = 48000;
    snapshot.fftSize = 256;
    snapshot.bins = 129;
    snapshot.profileReady = {true, false};
    snapshot.generation = 1;
    for (int channel = 0; channel < 2; ++channel)
        for (int bin = 0; bin < snapshot.bins; ++bin) {
            snapshot.inputPower[channel][bin] = channel == 0 ? 0.01f : 0.00001f;
            snapshot.noisePower[channel][bin] = 0.0001f;
            snapshot.gains[channel][bin] = channel == 0 ? 0.25f : 1.0f;
        }
    return snapshot;
}
}

TEST(DspNoiseSpectrumViewTest, ActiveAnalysisIsDisplayedAndInactiveSnapshotClearsImmediately)
{
    juce::ScopedJuceInitialiser_GUI gui;
    DspNoiseSpectrumView view;
    auto snapshot = exampleSnapshot();
    view.updateSnapshot(&snapshot, 1000);
    EXPECT_TRUE(view.hasVisibleData());
    EXPECT_TRUE(view.getProfileCaption().contains("Frozen"));
    snapshot.active = false;
    view.updateSnapshot(&snapshot, 1010);
    EXPECT_FALSE(view.hasVisibleData());
}

TEST(DspNoiseSpectrumViewTest, FrozenMailboxAndContendedReadsExpireThenRecover)
{
    juce::ScopedJuceInitialiser_GUI gui;
    DspNoiseSpectrumView view;
    auto snapshot = exampleSnapshot();
    view.updateSnapshot(&snapshot, 1000);
    view.updateSnapshot(nullptr, 1200);
    EXPECT_TRUE(view.hasVisibleData());
    // Reading the same successful mailbox cannot masquerade as fresh audio.
    view.updateSnapshot(&snapshot, 2000);
    EXPECT_FALSE(view.hasVisibleData());
    ++snapshot.generation;
    view.updateSnapshot(&snapshot, 2010);
    EXPECT_TRUE(view.hasVisibleData());
    view.updateSnapshot(nullptr, 3010);
    EXPECT_FALSE(view.hasVisibleData());
}

TEST(DspNoiseSpectrumViewTest, ChannelSelectionAndCaptureCaptionsDescribeActualProfileState)
{
    juce::ScopedJuceInitialiser_GUI gui;
    DspNoiseSpectrumView view;
    auto snapshot = exampleSnapshot();
    view.updateSnapshot(&snapshot, 0);
    EXPECT_EQ(view.getSelectedChannel(), 0);
    EXPECT_TRUE(view.getProfileCaption().contains("Frozen"));
    view.setSelectedChannel(1);
    EXPECT_EQ(view.getSelectedChannel(), 1);
    EXPECT_TRUE(view.getProfileCaption().contains("No profile"));
    snapshot.learning = true;
    ++snapshot.generation;
    view.updateSnapshot(&snapshot, 50);
    EXPECT_TRUE(view.getProfileCaption().contains("Capturing"));
    snapshot.learning = false;
    snapshot.realtime = true;
    snapshot.profileReady[1] = true;
    ++snapshot.generation;
    view.updateSnapshot(&snapshot, 100);
    EXPECT_TRUE(view.getProfileCaption().contains("Adaptive"));
}

TEST(DspNoiseSpectrumViewTest, InvalidGeometryClearsDataInsteadOfReusingPriorRate)
{
    juce::ScopedJuceInitialiser_GUI gui;
    DspNoiseSpectrumView view;
    auto snapshot = exampleSnapshot();
    view.updateSnapshot(&snapshot, 0);
    ASSERT_TRUE(view.hasVisibleData());
    snapshot.sampleRate = std::numeric_limits<double>::quiet_NaN();
    ++snapshot.generation;
    view.updateSnapshot(&snapshot, 50);
    EXPECT_FALSE(view.hasVisibleData());
    snapshot = exampleSnapshot();
    snapshot.bins = BuiltinNoiseRemovalDSP::kSpectrumMaxBins + 1;
    view.updateSnapshot(&snapshot, 100);
    EXPECT_FALSE(view.hasVisibleData());
    snapshot = exampleSnapshot();
    snapshot.bins = 0;
    view.updateSnapshot(&snapshot, 150);
    EXPECT_FALSE(view.hasVisibleData());
}

namespace {
int pixelsOfColour(const juce::Image& image, juce::Rectangle<int> region, juce::Colour colour)
{
    int count = 0;
    for (int y = region.getY(); y < region.getBottom(); ++y)
        for (int x = region.getX(); x < region.getRight(); ++x)
            if (image.getPixelAt(x, y) == colour) ++count;
    return count;
}
}

TEST(DspNoiseSpectrumViewTest, PaintedCurvesUseIndependentChannelsAndNoInventedLowFrequencyBins)
{
    juce::ScopedJuceInitialiser_GUI gui;
    DspNoiseSpectrumView view;
    view.setSize(640, 350);
    auto snapshot = exampleSnapshot();
    view.updateSnapshot(&snapshot, 0);
    auto left = view.createComponentSnapshot(view.getLocalBounds());
    // Central chart-only regions exclude captions/legends and axes. With real
    // bin spacing 187.5 Hz the entire region below 100 Hz must contain no curve.
    const juce::Rectangle<int> upperChart(66, 74, 550, 114);
    const juce::Rectangle<int> below100Hz(66, 74, 120, 114);
    EXPECT_GT(pixelsOfColour(left, upperChart, juce::Colour(0xff72c7ff)), 200);
    EXPECT_GT(pixelsOfColour(left, upperChart, juce::Colour(0xffffbd66)), 200);
    // 0.01 power is -20 dBFS/bin; a 0.25 amplitude gain is about 12 dB.
    // These narrow bands catch using 20log10 for power or 10log10 for gain.
    EXPECT_GT(pixelsOfColour(left, {300, 91, 200, 7}, juce::Colour(0xff72c7ff)), 100);
    EXPECT_GT(pixelsOfColour(left, {300, 270, 200, 6}, juce::Colour(0xffba9bff)), 100);
    const juce::Rectangle<int> belowUnityReduction(66, 243, 550, 68);
    EXPECT_GT(pixelsOfColour(left, belowUnityReduction, juce::Colour(0xffba9bff)), 200);
    EXPECT_EQ(pixelsOfColour(left, below100Hz, juce::Colour(0xff72c7ff)), 0);
    EXPECT_EQ(pixelsOfColour(left, below100Hz, juce::Colour(0xffffbd66)), 0);
    view.setSelectedChannel(1);
    auto right = view.createComponentSnapshot(view.getLocalBounds());
    EXPECT_GT(pixelsOfColour(right, upperChart, juce::Colour(0xff72c7ff)), 200);
    EXPECT_EQ(pixelsOfColour(right, upperChart, juce::Colour(0xffffbd66)), 0);
    EXPECT_EQ(pixelsOfColour(right, belowUnityReduction, juce::Colour(0xffba9bff)), 0);
    EXPECT_GT(pixelsOfColour(right, {300, 233, 200, 6}, juce::Colour(0xffba9bff)), 100);
    EXPECT_NE(left.getPixelAt(400, 95), right.getPixelAt(400, 95));
}

TEST(DspNoiseSpectrumViewTest, ActualProcessorDrivenEditorFixtures)
{
    juce::ScopedJuceInitialiser_GUI gui;
    BuiltinNoiseRemovalDSP processor;
    processor.prepareToPlay(48000, 256);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    DspNoiseSpectrumView* spectrum = nullptr;
    juce::Slider* reduction = nullptr;
    for (auto* child : editor->getChildren()) {
        if (auto* view = dynamic_cast<DspNoiseSpectrumView*>(child)) spectrum = view;
        if (auto* slider = dynamic_cast<juce::Slider*>(child)) reduction = slider;
    }
    ASSERT_NE(spectrum, nullptr);
    ASSERT_NE(reduction, nullptr);
    int64_t sample = 0;
    const auto render = [&](int count, bool voice) {
        juce::AudioBuffer<float> buffer(2, 256);
        juce::MidiBuffer midi;
        for (int offset = 0; offset < count; offset += 256) {
            const int blockSize = (std::min)(256, count - offset);
            buffer.setSize(2, blockSize, false, false, true);
            for (int i = 0; i < blockSize; ++i, ++sample) {
                const double phase = juce::MathConstants<double>::twoPi * sample / 48000.0;
                buffer.setSample(0, i, static_cast<float>(0.025 * std::sin(375 * phase)
                    + 0.008 * std::sin(3750 * phase) + (voice ? 0.18 * std::sin(1500 * phase) : 0)));
                buffer.setSample(1, i, static_cast<float>(0.012 * std::sin(750 * phase)
                    + 0.004 * std::sin(6000 * phase) + (voice ? 0.1 * std::sin(2250 * phase) : 0)));
            }
            processor.processBlock(buffer, midi);
        }
    };
    const auto capture = [&](const juce::String& name, const juce::String& expectedCaption, bool expectVisible = true) {
        SCOPED_TRACE(name.toStdString());
        // TimerThread startup is asynchronous, including in a software-only
        // editor. Wait for observed UI state rather than assuming one sleep
        // made a timer due. Matching reduction also ensures the zero-dB image
        // contains the new snapshot rather than the preceding adaptive mask.
        const auto ready = [&] {
            return spectrum->hasVisibleData() == expectVisible
                && (!expectVisible || spectrum->getProfileCaption().contains(expectedCaption))
                && std::abs(reduction->getValue() - processor.getReductionDb()) < 0.01;
        };
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        do {
            juce::Timer::callPendingTimersSynchronously();
            if (ready()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
        ASSERT_TRUE(ready()) << "Editor did not observe the processor state before the deadline";
        const auto directoryName = juce::SystemStats::getEnvironmentVariable("DIRECTPIPE_DSP_EVIDENCE_DIR", {});
        if (directoryName.isEmpty()) return;
        const juce::File directory(directoryName);
        ASSERT_TRUE(directory.createDirectory().wasOk());
        auto stream = directory.getChildFile(name + ".png").createOutputStream();
        ASSERT_NE(stream, nullptr);
        ASSERT_TRUE(stream->setPosition(0));
        ASSERT_TRUE(stream->truncate().wasOk());
        juce::PNGImageFormat png;
        EXPECT_TRUE(png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), *stream));
    };
    render(12000, true);
    capture("spectrum-learn-required", "No profile");
    EXPECT_TRUE(spectrum->hasVisibleData());
    EXPECT_TRUE(spectrum->getProfileCaption().contains("No profile"));
    processor.startLearning();
    render(24000, false);
    capture("spectrum-capturing", "Capturing");
    EXPECT_TRUE(spectrum->getProfileCaption().contains("Capturing"));
    render(80000, false);
    render(12000, true);
    capture("spectrum-frozen-left", "Frozen");
    EXPECT_TRUE(spectrum->getProfileCaption().contains("Frozen"));
    spectrum->setSelectedChannel(1);
    capture("spectrum-frozen-right", "Frozen");
    processor.setMode(BuiltinNoiseRemovalDSP::Mode::Realtime);
    render(48000, true);
    capture("spectrum-realtime", "Adaptive");
    EXPECT_TRUE(spectrum->getProfileCaption().contains("Adaptive"));
    processor.setReductionDb(0);
    render(12000, true);
    capture("spectrum-zero-reduction", "Adaptive");
    processor.releaseResources();
    capture("spectrum-inactive", {}, false);
    EXPECT_FALSE(spectrum->hasVisibleData());
}
