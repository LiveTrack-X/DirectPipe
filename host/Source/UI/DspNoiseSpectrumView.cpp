// SPDX-License-Identifier: GPL-3.0-or-later
#include "DspNoiseSpectrumView.h"
#include <algorithm>
#include <cmath>

namespace directpipe {
namespace {
constexpr double staleAfterMs = 750.0;
const juce::Colour inputColour(0xff72c7ff), noiseColour(0xffffbd66), maskColour(0xffba9bff);
const juce::Colour textColour(0xffe0e0e0), mutedColour(0xffa6a6bd), gridColour(0xff373748);

bool validGeometry(const BuiltinNoiseRemovalDSP::SpectrumSnapshot& snapshot)
{
    return std::isfinite(snapshot.sampleRate) && snapshot.sampleRate >= 8000 && snapshot.sampleRate <= 192000
        && snapshot.fftSize >= 64 && snapshot.fftSize <= 1024
        && (snapshot.fftSize & (snapshot.fftSize - 1)) == 0
        && snapshot.bins == snapshot.fftSize / 2 + 1
        && snapshot.bins <= BuiltinNoiseRemovalDSP::kSpectrumMaxBins;
}

juce::String frequencyText(double frequency)
{
    return frequency >= 1000 ? juce::String(frequency / 1000.0, frequency < 10000 ? 1 : 0) + "k"
                             : juce::String(frequency, 0);
}

struct Plots {
    explicit Plots(juce::Rectangle<int> bounds)
    {
        const float height = static_cast<float>((std::max)(80, bounds.getHeight() - 154));
        const float width = static_cast<float>((std::max)(1, bounds.getWidth() - 84));
        upper = {64.0f, 70.0f, width, height * 0.62f};
        lower = {64.0f, upper.getBottom() + 44.0f, width, height * 0.38f};
    }
    juce::Rectangle<float> upper, lower;
};
}

DspNoiseSpectrumView::DspNoiseSpectrumView()
{
    setName("Input spectrum, estimated noise profile and applied reduction");
    channelCombo_.setName("Spectrum display channel");
    channelCombo_.addItem("L / Mono", 1);
    channelCombo_.addItem("R", 2);
    channelCombo_.setSelectedId(1, juce::dontSendNotification);
    channelCombo_.setTooltip("Display one DSP channel without combining left and right spectra or masks.");
    channelCombo_.onChange = [this] { repaint(); };
    addAndMakeVisible(channelCombo_);
}

void DspNoiseSpectrumView::updateSnapshot(const BuiltinNoiseRemovalDSP::SpectrumSnapshot* incoming, double nowMs)
{
    if (incoming != nullptr) {
        if (!incoming->active || !validGeometry(*incoming)) {
            snapshot_ = *incoming;
            visible_ = stale_ = haveGeneration_ = false;
            repaint();
            return;
        }
        if (!haveGeneration_ || incoming->generation != snapshot_.generation) {
            snapshot_ = *incoming;
            lastFreshMs_ = nowMs;
            haveGeneration_ = true;
        }
    }
    visible_ = haveGeneration_ && nowMs >= lastFreshMs_ && nowMs - lastFreshMs_ < staleAfterMs;
    stale_ = haveGeneration_ && !visible_;
    repaint();
}

void DspNoiseSpectrumView::setSelectedChannel(int channel)
{
    channelCombo_.setSelectedId(channel == 1 ? 2 : 1, juce::dontSendNotification);
    repaint();
}

juce::String DspNoiseSpectrumView::getProfileCaption() const
{
    if (!visible_) return stale_ ? "Waiting for audio" : "No current spectrum";
    if (snapshot_.learning) return "Capturing noise estimate";
    if (!snapshot_.profileReady[static_cast<size_t>(getSelectedChannel())])
        return snapshot_.realtime ? "Adaptive estimate settling" : "No profile - Learn required";
    return snapshot_.realtime ? "Adaptive noise estimate" : "Frozen noise estimate";
}

void DspNoiseSpectrumView::resized()
{
    channelCombo_.setBounds(getWidth() - 112, 0, 108, 25);
}

void DspNoiseSpectrumView::paint(juce::Graphics& g)
{
    const Plots plots(getLocalBounds());
    g.setColour(juce::Colour(0xff191925));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
    g.setColour(textColour);
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.drawText("SPECTRUM / NOISE ESTIMATE", 10, 0, getWidth() - 132, 25, juce::Justification::centredLeft);
    g.setFont(juce::Font(12.0f));
    const auto legend = [&](juce::Colour colour, const juce::String& text, int x, int y, int width) {
        g.setColour(colour);
        g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y + 7), 14.0f, 3.0f, 1.0f);
        g.drawText(text, x + 21, y, width - 21, 19, juce::Justification::centredLeft);
    };
    legend(inputColour, "Input (includes voice)", 12, 29, 210);
    legend(noiseColour, getProfileCaption(), 228, 29, getWidth() - 238);
    g.setColour(mutedColour);
    g.setFont(juce::Font(11.0f));
    g.drawText("Power / FFT bin (dBFS)", 64, 50, 240, 18, juce::Justification::centredLeft);
    const int maskTitleY = static_cast<int>(plots.upper.getBottom()) + 12;
    legend(maskColour, "Applied reduction (dB) - actual DSP gain", 64, maskTitleY, getWidth() - 78);

    for (const auto plot : {plots.upper, plots.lower}) {
        g.setColour(juce::Colour(0xff12121c));
        g.fillRect(plot);
    }
    const auto horizontalGrid = [&](juce::Rectangle<float> plot, int maximum, int step, bool negative) {
        g.setFont(juce::Font(11.0f));
        for (int value = 0; value <= maximum; value += step) {
            const float y = plot.getY() + plot.getHeight() * static_cast<float>(value) / maximum;
            g.setColour(gridColour);
            g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
            g.setColour(mutedColour);
            g.drawText(juce::String(negative ? -value : value), 8, juce::roundToInt(y) - 7, 48, 14,
                       juce::Justification::centredRight);
        }
    };
    horizontalGrid(plots.upper, 100, 20, true);
    horizontalGrid(plots.lower, 24, 6, false);

    const bool geometryAvailable = validGeometry(snapshot_);
    const double maximumHz = geometryAvailable ? snapshot_.sampleRate * 0.5 : 24000.0;
    const double logRange = std::log(maximumHz / 20.0);
    const auto frequencyX = [&](double hz) {
        return plots.upper.getX() + plots.upper.getWidth() * static_cast<float>(std::log(hz / 20.0) / logRange);
    };
    float previousTick = plots.upper.getX() - 100;
    for (const double hz : {20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0,
                            10000.0, 20000.0, 50000.0}) {
        if (hz > maximumHz || (maximumHz - hz) / maximumHz < 0.08) continue;
        const float x = frequencyX(hz);
        g.setColour(gridColour);
        for (const auto plot : {plots.upper, plots.lower})
            g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
        if (x - previousTick >= 43 && x < plots.lower.getRight() - 40) {
            g.setColour(mutedColour);
            g.drawText(frequencyText(hz), juce::roundToInt(x) - 21,
                       juce::roundToInt(plots.lower.getBottom()) + 3, 42, 16, juce::Justification::centred);
            previousTick = x;
        }
    }
    g.setColour(mutedColour);
    g.drawText(frequencyText(maximumHz), juce::roundToInt(plots.lower.getRight()) - 39,
               juce::roundToInt(plots.lower.getBottom()) + 3, 45, 16, juce::Justification::centredRight);

    if (visible_) {
        const double binHz = snapshot_.sampleRate / snapshot_.fftSize;
        const float firstX = frequencyX(binHz);
        for (const auto plot : {plots.upper, plots.lower}) {
            g.setColour(juce::Colour(0xff242430));
            g.fillRect(juce::Rectangle<float>(plot.getX(), plot.getY(), firstX - plot.getX(), plot.getHeight()));
            g.setColour(mutedColour.withAlpha(0.65f));
            g.setFont(juce::Font(10.0f));
            g.drawText("No FFT bins", juce::Rectangle<float>(plot.getX() + 2, plot.getY() + 6,
                firstX - plot.getX() - 4, 16), juce::Justification::centred);
        }
        const size_t channel = static_cast<size_t>(getSelectedChannel());
        const auto curve = [&](const auto& values, juce::Rectangle<float> plot, juce::Colour colour, bool reduction) {
            juce::Path path;
            bool started = false;
            for (int bin = 1; bin < snapshot_.bins; ++bin) {
                const float value = values[static_cast<size_t>(bin)];
                if (!std::isfinite(value) || value < 0) { started = false; continue; }
                const float db = reduction ? -20.0f * std::log10((std::max)(value, 1.0e-6f))
                                           : 10.0f * std::log10((std::max)(value, 1.0e-10f));
                const float fraction = reduction ? juce::jlimit(0.0f, 24.0f, db) / 24.0f
                                                  : -juce::jlimit(-100.0f, 0.0f, db) / 100.0f;
                const float x = frequencyX(bin * binHz), y = plot.getY() + plot.getHeight() * fraction;
                if (!started) path.startNewSubPath(x, y); else path.lineTo(x, y);
                started = true;
            }
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(plot.toNearestInt().expanded(0, 1));
            g.setColour(colour);
            g.strokePath(path, juce::PathStrokeType(1.8f));
        };
        // Capturing may expose a partial estimate before profileReady becomes true.
        if (snapshot_.learning || snapshot_.profileReady[channel])
            curve(snapshot_.noisePower[channel], plots.upper, noiseColour, false);
        curve(snapshot_.inputPower[channel], plots.upper, inputColour, false);
        curve(snapshot_.gains[channel], plots.lower, maskColour, true);
        g.setFont(juce::Font(11.0f));
        g.setColour(mutedColour);
        g.drawText("Frequency (Hz, log)  |  " + juce::String(binHz, 1) + " Hz/bin  |  FFT "
            + juce::String(snapshot_.fftSize) + "  |  " + juce::String(snapshot_.sampleRate / 1000, 1)
            + " kHz  |  DC omitted", 8, getHeight() - 20, getWidth() - 16, 18, juce::Justification::centred);
    } else {
        g.setFont(juce::Font(13.0f));
        g.setColour(textColour);
        const auto message = stale_ ? "Waiting for audio - spectrum expired"
            : snapshot_.active ? "Waiting for a complete DSP analysis frame" : "DSP inactive - no current spectrum";
        g.drawText(message, plots.upper.reduced(8).toNearestInt(), juce::Justification::centred);
        g.setFont(juce::Font(11.0f));
        g.setColour(mutedColour);
        g.drawText("Frequency (Hz, log)  |  No current FFT data", 8, getHeight() - 20,
                   getWidth() - 16, 18, juce::Justification::centred);
    }
}
}
