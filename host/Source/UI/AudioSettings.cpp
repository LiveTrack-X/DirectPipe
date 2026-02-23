/**
 * @file AudioSettings.cpp
 * @brief Unified audio I/O configuration panel implementation
 */

#include "AudioSettings.h"

namespace directpipe {

// ─── Construction / Destruction ─────────────────────────────────────────────

AudioSettings::AudioSettings(AudioEngine& engine)
    : engine_(engine)
{
    // ── Title ──
    titleLabel_.setFont(juce::Font(16.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(titleLabel_);

    auto styleLabel = [this](juce::Label& label) {
        label.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
        addAndMakeVisible(label);
    };

    auto styleCombo = [this](juce::ComboBox& combo) {
        addAndMakeVisible(combo);
    };

    // ── Driver type ──
    styleLabel(driverLabel_);
    styleCombo(driverCombo_);
    {
        auto types = engine_.getAvailableDeviceTypes();
        for (int i = 0; i < types.size(); ++i)
            driverCombo_.addItem(types[i], i + 1);

        // Select current type
        auto currentType = engine_.getCurrentDeviceType();
        int idx = types.indexOf(currentType);
        if (idx >= 0)
            driverCombo_.setSelectedId(idx + 1, juce::dontSendNotification);
        else if (types.size() > 0)
            driverCombo_.setSelectedId(1, juce::dontSendNotification);
    }
    driverCombo_.onChange = [this] { onDriverTypeChanged(); };

    // ── Input device ──
    styleLabel(inputLabel_);
    styleCombo(inputCombo_);
    inputCombo_.onChange = [this] { onInputDeviceChanged(); };

    // ── Output device ──
    styleLabel(outputLabel_);
    styleCombo(outputCombo_);
    outputCombo_.onChange = [this] { onOutputDeviceChanged(); };

    // ── ASIO channel selection ──
    styleLabel(inputChLabel_);
    styleCombo(inputChCombo_);
    inputChCombo_.onChange = [this] { onInputChannelChanged(); };

    styleLabel(outputChLabel_);
    styleCombo(outputChCombo_);
    outputChCombo_.onChange = [this] { onOutputChannelChanged(); };

    // ── Sample rate ──
    styleLabel(sampleRateLabel_);
    styleCombo(sampleRateCombo_);
    sampleRateCombo_.onChange = [this] { onSampleRateChanged(); };

    // ── Buffer size ──
    styleLabel(bufferSizeLabel_);
    styleCombo(bufferSizeCombo_);
    bufferSizeCombo_.onChange = [this] { onBufferSizeChanged(); };

    // ── Channel mode (radio group) ──
    styleLabel(channelModeLabel_);

    monoButton_.setRadioGroupId(1);
    stereoButton_.setRadioGroupId(1);
    stereoButton_.setToggleState(true, juce::dontSendNotification);
    monoButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(kTextColour));
    monoButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(kAccentColour));
    stereoButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(kTextColour));
    stereoButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(kAccentColour));
    monoButton_.onClick   = [this] { onChannelModeChanged(); };
    stereoButton_.onClick = [this] { onChannelModeChanged(); };
    addAndMakeVisible(monoButton_);
    addAndMakeVisible(stereoButton_);

    channelModeDescLabel_.setFont(juce::Font(11.0f));
    channelModeDescLabel_.setColour(juce::Label::textColourId, juce::Colour(kDimTextColour));
    addAndMakeVisible(channelModeDescLabel_);
    updateChannelModeDescription();

    // ── Latency display ──
    latencyTitleLabel_.setColour(juce::Label::textColourId, juce::Colour(kDimTextColour));
    latencyTitleLabel_.setFont(juce::Font(13.0f));
    addAndMakeVisible(latencyTitleLabel_);

    latencyValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kAccentColour));
    latencyValueLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    addAndMakeVisible(latencyValueLabel_);

    // ── ASIO Control Panel button ──
    asioControlBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(kSurfaceColour).brighter(0.15f));
    asioControlBtn_.setColour(juce::TextButton::textColourOnId, juce::Colour(kTextColour));
    asioControlBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(kTextColour));
    asioControlBtn_.onClick = [this] { engine_.showAsioControlPanel(); };
    addAndMakeVisible(asioControlBtn_);

    // Listen for device changes
    engine_.getDeviceManager().addChangeListener(this);

    // Synchronise the UI with current engine state
    refreshFromEngine();
}

AudioSettings::~AudioSettings()
{
    engine_.getDeviceManager().removeChangeListener(this);
}

// ─── Paint ──────────────────────────────────────────────────────────────────

void AudioSettings::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBgColour));

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

    // Driver type
    driverLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    driverCombo_.setBounds(bounds.getX() + labelW + gap, y,
                           bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // Input device (always visible)
    inputLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    inputCombo_.setBounds(bounds.getX() + labelW + gap, y,
                          bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // Output device (always visible)
    outputLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    outputCombo_.setBounds(bounds.getX() + labelW + gap, y,
                           bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // ASIO channel selection (only visible in ASIO mode)
    bool asio = isAsioMode();
    if (asio) {
        inputChLabel_.setBounds(bounds.getX(), y, labelW, rowH);
        inputChCombo_.setBounds(bounds.getX() + labelW + gap, y,
                                bounds.getWidth() - labelW - gap, rowH);
        y += rowH + gap;

        outputChLabel_.setBounds(bounds.getX(), y, labelW, rowH);
        outputChCombo_.setBounds(bounds.getX() + labelW + gap, y,
                                 bounds.getWidth() - labelW - gap, rowH);
        y += rowH + gap;

        inputChLabel_.setVisible(true);
        inputChCombo_.setVisible(true);
        outputChLabel_.setVisible(true);
        outputChCombo_.setVisible(true);
    } else {
        inputChLabel_.setVisible(false);
        inputChCombo_.setVisible(false);
        outputChLabel_.setVisible(false);
        outputChCombo_.setVisible(false);
    }

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

    // Channel mode description
    channelModeDescLabel_.setBounds(bounds.getX() + labelW + gap, y,
                                    bounds.getWidth() - labelW - gap, 18);
    y += 22;

    // Latency display
    latencyTitleLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    latencyValueLabel_.setBounds(bounds.getX() + labelW + gap, y,
                                 bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // ASIO Control Panel button (only visible in ASIO mode)
    if (asio) {
        asioControlBtn_.setBounds(bounds.getX() + labelW + gap, y,
                                  bounds.getWidth() - labelW - gap, rowH);
        asioControlBtn_.setVisible(true);
    } else {
        asioControlBtn_.setVisible(false);
    }
}

// ─── Refresh from engine ────────────────────────────────────────────────────

void AudioSettings::refreshFromEngine()
{
    // Driver type
    auto currentType = engine_.getCurrentDeviceType();
    auto types = engine_.getAvailableDeviceTypes();
    int typeIdx = types.indexOf(currentType);
    if (typeIdx >= 0)
        driverCombo_.setSelectedId(typeIdx + 1, juce::dontSendNotification);

    // Rebuild device lists
    rebuildDeviceLists();
    rebuildSampleRateList();
    rebuildBufferSizeList();

    // Channel mode
    int ch = engine_.getChannelMode();
    if (ch == 2)
        stereoButton_.setToggleState(true, juce::dontSendNotification);
    else
        monoButton_.setToggleState(true, juce::dontSendNotification);

    updateChannelModeDescription();
    updateLatencyDisplay();

    // Update ASIO-specific visibility
    bool asio = isAsioMode();
    asioControlBtn_.setVisible(asio);
    inputChLabel_.setVisible(asio);
    inputChCombo_.setVisible(asio);
    outputChLabel_.setVisible(asio);
    outputChCombo_.setVisible(asio);
    resized();
}

// ─── ChangeListener ─────────────────────────────────────────────────────────

void AudioSettings::changeListenerCallback(juce::ChangeBroadcaster* /*source*/)
{
    // Device manager changed — rebuild dynamic lists
    rebuildSampleRateList();
    rebuildBufferSizeList();
    updateLatencyDisplay();
}

// ─── Callbacks ──────────────────────────────────────────────────────────────

void AudioSettings::onDriverTypeChanged()
{
    int id = driverCombo_.getSelectedId();
    auto types = engine_.getAvailableDeviceTypes();
    if (id >= 1 && id <= types.size()) {
        engine_.setAudioDeviceType(types[id - 1]);
        rebuildDeviceLists();
        rebuildSampleRateList();
        rebuildBufferSizeList();
        updateLatencyDisplay();

        bool asio = isAsioMode();
        asioControlBtn_.setVisible(asio);
        inputChLabel_.setVisible(asio);
        inputChCombo_.setVisible(asio);
        outputChLabel_.setVisible(asio);
        outputChCombo_.setVisible(asio);
        resized();
    }
}

void AudioSettings::onInputDeviceChanged()
{
    auto selectedText = inputCombo_.getText();
    if (selectedText.isEmpty()) return;

    if (isAsioMode()) {
        // ASIO: set both input and output to the same device
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        engine_.getDeviceManager().getAudioDeviceSetup(setup);
        setup.inputDeviceName = selectedText;
        setup.outputDeviceName = selectedText;
        setup.useDefaultInputChannels = false;
        setup.useDefaultOutputChannels = false;
        setup.inputChannels.setRange(0, 2, true);
        setup.outputChannels.setRange(0, 2, true);

        auto result = engine_.getDeviceManager().setAudioDeviceSetup(setup, true);
        if (result.isNotEmpty())
            juce::Logger::writeToLog("ASIO input device change failed: " + result);

        // Sync output combo to match (ASIO single device)
        auto outputs = engine_.getAvailableOutputDevices();
        int outIdx = outputs.indexOf(selectedText);
        if (outIdx >= 0)
            outputCombo_.setSelectedId(outIdx + 1, juce::dontSendNotification);

        rebuildChannelLists();
    } else {
        engine_.setInputDevice(selectedText);
    }

    rebuildSampleRateList();
    rebuildBufferSizeList();
    updateLatencyDisplay();
}

void AudioSettings::onOutputDeviceChanged()
{
    auto selectedText = outputCombo_.getText();
    if (selectedText.isEmpty()) return;

    if (isAsioMode()) {
        // ASIO: set both input and output to the same device
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        engine_.getDeviceManager().getAudioDeviceSetup(setup);
        setup.inputDeviceName = selectedText;
        setup.outputDeviceName = selectedText;
        setup.useDefaultInputChannels = false;
        setup.useDefaultOutputChannels = false;
        setup.inputChannels.setRange(0, 2, true);
        setup.outputChannels.setRange(0, 2, true);

        auto result = engine_.getDeviceManager().setAudioDeviceSetup(setup, true);
        if (result.isNotEmpty())
            juce::Logger::writeToLog("ASIO output device change failed: " + result);

        // Sync input combo to match (ASIO single device)
        auto inputs = engine_.getAvailableInputDevices();
        int inIdx = inputs.indexOf(selectedText);
        if (inIdx >= 0)
            inputCombo_.setSelectedId(inIdx + 1, juce::dontSendNotification);

        rebuildChannelLists();
    } else {
        engine_.setOutputDevice(selectedText);
    }

    rebuildSampleRateList();
    rebuildBufferSizeList();
    updateLatencyDisplay();
}

void AudioSettings::onInputChannelChanged()
{
    int id = inputChCombo_.getSelectedId();
    if (id < 1) return;

    int firstChannel = (id - 1) * 2;  // pairs: 0-1, 2-3, 4-5, ...
    int numCh = stereoButton_.getToggleState() ? 2 : 1;
    engine_.setActiveInputChannels(firstChannel, numCh);
}

void AudioSettings::onOutputChannelChanged()
{
    int id = outputChCombo_.getSelectedId();
    if (id < 1) return;

    int firstChannel = (id - 1) * 2;
    int numCh = stereoButton_.getToggleState() ? 2 : 1;
    engine_.setActiveOutputChannels(firstChannel, numCh);
}

void AudioSettings::onSampleRateChanged()
{
    int id = sampleRateCombo_.getSelectedId();
    if (id < 1) return;

    auto rates = engine_.getAvailableSampleRates();
    if (id - 1 < rates.size()) {
        engine_.setSampleRate(rates[id - 1]);
        updateLatencyDisplay();
    }
}

void AudioSettings::onBufferSizeChanged()
{
    int id = bufferSizeCombo_.getSelectedId();
    if (id < 1) return;

    auto sizes = engine_.getAvailableBufferSizes();
    if (id - 1 < sizes.size()) {
        engine_.setBufferSize(sizes[id - 1]);
        updateLatencyDisplay();
    }
}

void AudioSettings::onChannelModeChanged()
{
    int channels = stereoButton_.getToggleState() ? 2 : 1;
    engine_.setChannelMode(channels);
    updateChannelModeDescription();
}

// ─── Helpers ────────────────────────────────────────────────────────────────

bool AudioSettings::isAsioMode() const
{
    return engine_.getCurrentDeviceType().containsIgnoreCase("ASIO");
}

void AudioSettings::rebuildDeviceLists()
{
    // Input devices (from current driver type)
    inputCombo_.clear(juce::dontSendNotification);
    auto inputs = engine_.getAvailableInputDevices();
    for (int i = 0; i < inputs.size(); ++i)
        inputCombo_.addItem(inputs[i], i + 1);

    // Output devices (from current driver type)
    outputCombo_.clear(juce::dontSendNotification);
    auto outputs = engine_.getAvailableOutputDevices();
    for (int i = 0; i < outputs.size(); ++i)
        outputCombo_.addItem(outputs[i], i + 1);

    // Select current devices
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    engine_.getDeviceManager().getAudioDeviceSetup(setup);

    // Input selection
    juce::String inputName = setup.inputDeviceName;
    if (inputName.isEmpty()) {
        if (auto* device = engine_.getDeviceManager().getCurrentAudioDevice())
            inputName = device->getName();
    }
    int inIdx = inputs.indexOf(inputName);
    if (inIdx >= 0)
        inputCombo_.setSelectedId(inIdx + 1, juce::dontSendNotification);
    else if (inputs.size() > 0)
        inputCombo_.setSelectedId(1, juce::dontSendNotification);

    // Output selection
    juce::String outputName = setup.outputDeviceName;
    if (outputName.isEmpty()) {
        if (auto* device = engine_.getDeviceManager().getCurrentAudioDevice())
            outputName = device->getName();
    }
    int outIdx = outputs.indexOf(outputName);
    if (outIdx >= 0)
        outputCombo_.setSelectedId(outIdx + 1, juce::dontSendNotification);
    else if (outputs.size() > 0)
        outputCombo_.setSelectedId(1, juce::dontSendNotification);

    // Rebuild ASIO channel lists if applicable
    if (isAsioMode())
        rebuildChannelLists();
}

void AudioSettings::rebuildChannelLists()
{
    // Input channels (stereo pairs)
    inputChCombo_.clear(juce::dontSendNotification);
    auto inNames = engine_.getInputChannelNames();
    int inPairs = inNames.size() / 2;
    if (inNames.size() % 2 != 0) inPairs++;  // odd channel count

    for (int i = 0; i < inPairs; ++i) {
        int ch1 = i * 2;
        int ch2 = ch1 + 1;
        juce::String label;
        if (ch2 < inNames.size())
            label = inNames[ch1] + " + " + inNames[ch2];
        else
            label = inNames[ch1];
        inputChCombo_.addItem(label, i + 1);
    }
    // Also add individual channels for mono use
    if (inNames.size() == 0) {
        inputChCombo_.addItem("1-2", 1);
    }

    // Select current input channel offset
    int inOffset = engine_.getActiveInputChannelOffset();
    if (inOffset >= 0)
        inputChCombo_.setSelectedId((inOffset / 2) + 1, juce::dontSendNotification);
    else if (inputChCombo_.getNumItems() > 0)
        inputChCombo_.setSelectedId(1, juce::dontSendNotification);

    // Output channels (stereo pairs)
    outputChCombo_.clear(juce::dontSendNotification);
    auto outNames = engine_.getOutputChannelNames();
    int outPairs = outNames.size() / 2;
    if (outNames.size() % 2 != 0) outPairs++;

    for (int i = 0; i < outPairs; ++i) {
        int ch1 = i * 2;
        int ch2 = ch1 + 1;
        juce::String label;
        if (ch2 < outNames.size())
            label = outNames[ch1] + " + " + outNames[ch2];
        else
            label = outNames[ch1];
        outputChCombo_.addItem(label, i + 1);
    }
    if (outNames.size() == 0) {
        outputChCombo_.addItem("1-2", 1);
    }

    // Select current output channel offset
    int outOffset = engine_.getActiveOutputChannelOffset();
    if (outOffset >= 0)
        outputChCombo_.setSelectedId((outOffset / 2) + 1, juce::dontSendNotification);
    else if (outputChCombo_.getNumItems() > 0)
        outputChCombo_.setSelectedId(1, juce::dontSendNotification);
}

void AudioSettings::rebuildSampleRateList()
{
    sampleRateCombo_.clear(juce::dontSendNotification);
    auto rates = engine_.getAvailableSampleRates();
    auto& monitor = engine_.getLatencyMonitor();
    double currentSR = monitor.getSampleRate();

    for (int i = 0; i < rates.size(); ++i) {
        sampleRateCombo_.addItem(juce::String(static_cast<int>(rates[i])) + " Hz", i + 1);
        if (std::abs(rates[i] - currentSR) < 1.0)
            sampleRateCombo_.setSelectedId(i + 1, juce::dontSendNotification);
    }
}

void AudioSettings::rebuildBufferSizeList()
{
    bufferSizeCombo_.clear(juce::dontSendNotification);
    auto sizes = engine_.getAvailableBufferSizes();
    int currentBS = engine_.getLatencyMonitor().getBufferSize();

    for (int i = 0; i < sizes.size(); ++i) {
        bufferSizeCombo_.addItem(juce::String(sizes[i]) + " samples", i + 1);
        if (sizes[i] == currentBS)
            bufferSizeCombo_.setSelectedId(i + 1, juce::dontSendNotification);
    }
}

void AudioSettings::updateLatencyDisplay()
{
    auto& monitor = engine_.getLatencyMonitor();
    double sr = monitor.getSampleRate();
    int bs = monitor.getBufferSize();

    if (sr > 0.0) {
        double latencyMs = (static_cast<double>(bs) / sr) * 1000.0 * 2.0;
        latencyValueLabel_.setText(
            juce::String(latencyMs, 2) + " ms  (" + juce::String(bs) + " samples @ "
                + juce::String(static_cast<int>(sr)) + " Hz)",
            juce::dontSendNotification);
    } else {
        latencyValueLabel_.setText("-- ms", juce::dontSendNotification);
    }
}

void AudioSettings::updateChannelModeDescription()
{
    if (stereoButton_.getToggleState()) {
        channelModeDescLabel_.setText(
            "Stereo: Input channels pass through as-is (L/R)",
            juce::dontSendNotification);
    } else {
        channelModeDescLabel_.setText(
            "Mono: Mix L+R to mono, output to both channels",
            juce::dontSendNotification);
    }
}

} // namespace directpipe
