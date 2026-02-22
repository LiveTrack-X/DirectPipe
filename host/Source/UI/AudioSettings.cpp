/**
 * @file AudioSettings.cpp
 * @brief Audio configuration panel implementation
 */

#include "AudioSettings.h"

namespace directpipe {

// ─── Lookup tables ──────────────────────────────────────────────────────────

static constexpr double kSampleRates[] = {44100.0, 48000.0, 96000.0};
static constexpr int    kBufferSizes[] = {64, 128, 256, 480, 512};

// ─── Construction / Destruction ─────────────────────────────────────────────

AudioSettings::AudioSettings(AudioEngine& engine)
    : engine_(engine)
{
    // ── Title ──
    titleLabel_.setFont(juce::Font(16.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(titleLabel_);

    // ── Sample rate ──
    sampleRateLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(sampleRateLabel_);

    sampleRateCombo_.addItem("44100 Hz", 1);
    sampleRateCombo_.addItem("48000 Hz", 2);
    sampleRateCombo_.addItem("96000 Hz", 3);
    sampleRateCombo_.setSelectedId(2, juce::dontSendNotification); // default 48 kHz
    sampleRateCombo_.onChange = [this] { onSampleRateChanged(); };
    addAndMakeVisible(sampleRateCombo_);

    // ── Buffer size ──
    bufferSizeLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(bufferSizeLabel_);

    bufferSizeCombo_.addItem("64 samples",  1);
    bufferSizeCombo_.addItem("128 samples", 2);
    bufferSizeCombo_.addItem("256 samples", 3);
    bufferSizeCombo_.addItem("480 samples", 4);
    bufferSizeCombo_.addItem("512 samples", 5);
    bufferSizeCombo_.setSelectedId(4, juce::dontSendNotification); // default 480
    bufferSizeCombo_.onChange = [this] { onBufferSizeChanged(); };
    addAndMakeVisible(bufferSizeCombo_);

    // ── Channel mode (radio group) ──
    channelModeLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(channelModeLabel_);

    monoButton_.setRadioGroupId(1);
    stereoButton_.setRadioGroupId(1);
    monoButton_.setToggleState(true, juce::dontSendNotification); // default Mono
    monoButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(kTextColour));
    monoButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(kAccentColour));
    stereoButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(kTextColour));
    stereoButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(kAccentColour));

    monoButton_.onClick   = [this] { onChannelModeChanged(); };
    stereoButton_.onClick = [this] { onChannelModeChanged(); };

    addAndMakeVisible(monoButton_);
    addAndMakeVisible(stereoButton_);

    // ── Latency display ──
    latencyTitleLabel_.setColour(juce::Label::textColourId, juce::Colour(kDimTextColour));
    latencyTitleLabel_.setFont(juce::Font(13.0f));
    addAndMakeVisible(latencyTitleLabel_);

    latencyValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kAccentColour));
    latencyValueLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    addAndMakeVisible(latencyValueLabel_);

    // Synchronise the UI with current engine state
    refreshFromEngine();
}

AudioSettings::~AudioSettings() = default;

// ─── Paint ──────────────────────────────────────────────────────────────────

void AudioSettings::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBgColour));

    // Draw a rounded-rect surface behind the controls
    auto area = getLocalBounds().reduced(4);
    g.setColour(juce::Colour(kSurfaceColour));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);
}

// ─── Layout ─────────────────────────────────────────────────────────────────

void AudioSettings::resized()
{
    auto bounds = getLocalBounds().reduced(12);
    constexpr int rowH = 28;
    constexpr int gap  = 8;
    constexpr int labelW = 120;

    int y = bounds.getY();

    // Title
    titleLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), rowH);
    y += rowH + gap;

    // Sample Rate
    sampleRateLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    sampleRateCombo_.setBounds(bounds.getX() + labelW + gap, y,
                               bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // Buffer Size
    bufferSizeLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    bufferSizeCombo_.setBounds(bounds.getX() + labelW + gap, y,
                               bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // Channel Mode
    channelModeLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    int radioW = (bounds.getWidth() - labelW - gap) / 2;
    monoButton_.setBounds(bounds.getX() + labelW + gap, y, radioW, rowH);
    stereoButton_.setBounds(bounds.getX() + labelW + gap + radioW, y, radioW, rowH);
    y += rowH + gap + 4;

    // Latency display
    latencyTitleLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    latencyValueLabel_.setBounds(bounds.getX() + labelW + gap, y,
                                 bounds.getWidth() - labelW - gap, rowH);
}

// ─── Refresh from engine ────────────────────────────────────────────────────

void AudioSettings::refreshFromEngine()
{
    // Determine current engine state via the latency monitor
    auto& monitor = engine_.getLatencyMonitor();
    double sr = monitor.getSampleRate();
    int bs    = monitor.getBufferSize();
    int ch    = engine_.getChannelMode();

    // Select matching sample rate combo item
    for (int i = 0; i < 3; ++i) {
        if (std::abs(sr - kSampleRates[i]) < 1.0) {
            sampleRateCombo_.setSelectedId(i + 1, juce::dontSendNotification);
            break;
        }
    }

    // Select matching buffer size combo item
    for (int i = 0; i < 5; ++i) {
        if (bs == kBufferSizes[i]) {
            bufferSizeCombo_.setSelectedId(i + 1, juce::dontSendNotification);
            break;
        }
    }

    // Channel mode
    if (ch == 2)
        stereoButton_.setToggleState(true, juce::dontSendNotification);
    else
        monoButton_.setToggleState(true, juce::dontSendNotification);

    updateLatencyDisplay();
}

// ─── Callbacks ──────────────────────────────────────────────────────────────

void AudioSettings::onSampleRateChanged()
{
    double sr = sampleRateFromId(sampleRateCombo_.getSelectedId());
    engine_.setSampleRate(sr);
    updateLatencyDisplay();
}

void AudioSettings::onBufferSizeChanged()
{
    int bs = bufferSizeFromId(bufferSizeCombo_.getSelectedId());
    engine_.setBufferSize(bs);
    updateLatencyDisplay();
}

void AudioSettings::onChannelModeChanged()
{
    int channels = stereoButton_.getToggleState() ? 2 : 1;
    engine_.setChannelMode(channels);
}

void AudioSettings::updateLatencyDisplay()
{
    double sr = sampleRateFromId(sampleRateCombo_.getSelectedId());
    int    bs = bufferSizeFromId(bufferSizeCombo_.getSelectedId());

    if (sr > 0.0) {
        // One buffer of input + one buffer of output = 2x buffer latency
        double latencyMs = (static_cast<double>(bs) / sr) * 1000.0 * 2.0;
        latencyValueLabel_.setText(
            juce::String(latencyMs, 2) + " ms  (" + juce::String(bs) + " samples @ "
                + juce::String(static_cast<int>(sr)) + " Hz)",
            juce::dontSendNotification);
    }
    else {
        latencyValueLabel_.setText("-- ms", juce::dontSendNotification);
    }
}

// ─── Helpers ────────────────────────────────────────────────────────────────

double AudioSettings::sampleRateFromId(int id)
{
    if (id >= 1 && id <= 3)
        return kSampleRates[id - 1];
    return 48000.0;
}

int AudioSettings::bufferSizeFromId(int id)
{
    if (id >= 1 && id <= 5)
        return kBufferSizes[id - 1];
    return 480;
}

} // namespace directpipe
