/**
 * @file LevelMeter.h
 * @brief Real-time audio level meter UI component
 */
#pragma once

#include <JuceHeader.h>
#include <atomic>

namespace directpipe {

/**
 * @brief Visual audio level meter with peak hold.
 *
 * Displays a vertical or horizontal bar meter with:
 * - Current RMS level (green → yellow → red)
 * - Peak hold indicator
 * - Clipping indicator
 */
class LevelMeter : public juce::Component,
                   public juce::Timer {
public:
    explicit LevelMeter(const juce::String& label = "");
    ~LevelMeter() override;

    /**
     * @brief Set the current level (0.0 - 1.0). Thread-safe.
     */
    void setLevel(float level);

    /**
     * @brief Set orientation.
     * @param vertical true for vertical meter, false for horizontal.
     */
    void setVertical(bool vertical) { vertical_ = vertical; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    juce::String label_;
    bool vertical_ = true;

    // Current level (thread-safe)
    std::atomic<float> targetLevel_{0.0f};
    float displayLevel_ = 0.0f;
    float peakLevel_ = 0.0f;
    int peakHoldCounter_ = 0;

    bool clipping_ = false;

    // Smoothing
    static constexpr float kAttack = 0.3f;   // Fast attack
    static constexpr float kRelease = 0.05f;  // Slow release
    static constexpr int kPeakHoldFrames = 30; // ~1 second at 30Hz

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
};

} // namespace directpipe
