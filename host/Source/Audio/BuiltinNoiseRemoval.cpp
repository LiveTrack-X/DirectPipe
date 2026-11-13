// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#include "BuiltinNoiseRemoval.h"
#include "../UI/NoiseRemovalEditPanel.h"
#include <algorithm>
#include <cmath>

namespace directpipe {

juce::AudioProcessorEditor* BuiltinNoiseRemoval::createEditor()
{
    return new NoiseRemovalEditPanel(*this);
}

// ─── Construction / Destruction ─────────────────────────────────

BuiltinNoiseRemoval::BuiltinNoiseRemoval()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // RNNoise instances are NOT created here (they malloc internally).
    // Created in prepareToPlay, destroyed in releaseResources / destructor.
}

BuiltinNoiseRemoval::~BuiltinNoiseRemoval()
{
    destroyRNNoise();
}

// ─── AudioProcessor lifecycle ───────────────────────────────────

void BuiltinNoiseRemoval::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    mode_.store(Mode::Inactive, std::memory_order_release);
    hostSampleRate_.store(sampleRate, std::memory_order_relaxed);
    needsResampling_.store(sampleRate != 48000.0, std::memory_order_relaxed);
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 192000.0) {
        destroyRNNoise();
        setLatencySamples(0);
        mode_.store(Mode::UnsupportedRate, std::memory_order_release);
        return;
    }

    // Create RNNoise denoise states
    destroyRNNoise();
    rnnL_ = rnnoise_create(nullptr);
    rnnR_ = rnnoise_create(nullptr);
    if (!rnnL_ || !rnnR_) {
        destroyRNNoise();
        setLatencySamples(0);
        mode_.store(Mode::Unavailable, std::memory_order_release);
        return;
    }

    juce::Logger::writeToLog("[AUDIO] BuiltinNoiseRemoval: prepareToPlay SR="
        + juce::String(sampleRate) + " BS=" + juce::String(samplesPerBlock)
        + " rnnL=" + juce::String(rnnL_ != nullptr ? "OK" : "NULL")
        + " rnnR=" + juce::String(rnnR_ != nullptr ? "OK" : "NULL")
        + " needsResampling=" + juce::String(needsResampling_.load() ? "YES" : "NO"));

    // Pass 1 can enqueue a complete host callback before Pass 2 drains any
    // output. Keep enough room for the 480-sample FIFO priming plus frame
    // rounding on callbacks up to the supported 4096-sample maximum. The
    // allocation happens only here, never on the RT thread.
    const int provisionedBlockSize = (std::max)(kMaxSupportedBlockSize,
                                                 (std::max)(1, samplesPerBlock));
    const size_t requiredCapacity = (std::max)(static_cast<size_t>(kMinimumOutputFifoCapacity),
        static_cast<size_t>(provisionedBlockSize) + 2 * kRNNFrameSize);
    size_t outputCapacity = 1;
    while (outputCapacity < requiredCapacity) outputCapacity *= 2;

    inputFifoL_.assign(kRNNFrameSize, 0.0f);
    inputFifoR_.assign(kRNNFrameSize, 0.0f);
    outputFifoL_.assign(outputCapacity, 0.0f);
    outputFifoR_.assign(outputCapacity, 0.0f);

    // Reset FIFO positions
    inputFifoWriteL_ = 0;
    inputFifoWriteR_ = 0;
    outputFifoReadL_  = 0;
    outputFifoWriteL_ = kRNNFrameSize;
    outputFifoReadR_  = 0;
    outputFifoWriteR_ = kRNNFrameSize;

    // Start with gate CLOSED -- prevents initial noise burst before RNNoise stabilizes
    gateGainL_ = 0.0f;
    gateGainR_ = 0.0f;
    holdCounterL_ = 0;
    holdCounterR_ = 0;

    // Gate time constants use the internal processing rate, not the host rate.
    // holdSamples_: 300ms hold time in samples (prevents choppy gating between words)
    // gateSmooth_: 20ms exponential smoothing coefficient (prevents audible gate clicks)
    holdSamples_ = 14400; // Gate operates on 48 kHz RNNoise frames, even when converted.
    gateSmooth_ = static_cast<float>(std::exp(-1.0 / (48000.0 * 0.020)));

    // Advance zero-initialized recurrent/filter state using five silent frames
    // outside rendering. FIFO priming and the initially closed gate reduce
    // startup transients; they do not guarantee artifact-free hardware startup.
    {
        float silent[kRNNFrameSize] = {};
        float dummy[kRNNFrameSize];
        for (int i = 0; i < 5; ++i) {  // 5 frames = ~50ms warmup
            if (rnnL_) rnnoise_process_frame(rnnL_, dummy, silent);
            if (rnnR_) rnnoise_process_frame(rnnR_, dummy, silent);
        }
    }

    if (needsResampling()) {
        const double ratio = sampleRate / 48000.0;
        const int conversionDelay = static_cast<int>(std::ceil(
            StreamingSincResampler::kHalfLength * (1.0 + ratio))) + 2;
        for (auto& channel : conversion_) {
            channel.to48k.prepare(sampleRate, 48000.0);
            channel.from48k.prepare(48000.0, sampleRate);
            channel.output.assign(static_cast<size_t>(conversionDelay + 64), 0.0f);
            channel.read = 0;
            channel.write = static_cast<size_t>(conversionDelay);
            channel.queued = static_cast<size_t>(conversionDelay);
        }
        setLatencySamples(conversionDelay + static_cast<int>(std::ceil(
            (kRNNFrameSize + kRNNAlgorithmDelay) * ratio)));
        mode_.store(Mode::Resampled, std::memory_order_release);
    } else {
        // Reporting includes RNNoise's own two-frame delay. No audio is added.
        setLatencySamples(kRNNFrameSize + kRNNAlgorithmDelay);
        mode_.store(Mode::Native48k, std::memory_order_release);
    }
}

void BuiltinNoiseRemoval::releaseResources()
{
    mode_.store(Mode::Inactive, std::memory_order_release);
    destroyRNNoise();
    setLatencySamples(0);
}

// ─── processBlock  [RT audio thread] ────────────────────────────

void BuiltinNoiseRemoval::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (!isActive())
        return;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples == 0)
        return;

    if (getMode() == Mode::Resampled) {
        for (int channel = 0; channel < juce::jmin(2, numChannels); ++channel)
            processConvertedChannel(buffer.getWritePointer(channel), numSamples, channel);
        return;
    }

    // Channel 0 (Left / mono)
    if (numChannels > 0) {
        processChannel(buffer.getReadPointer(0), buffer.getWritePointer(0),
                       numSamples, rnnL_,
                       inputFifoL_, inputFifoWriteL_,
                       outputFifoL_, outputFifoReadL_, outputFifoWriteL_,
                       gateGainL_, holdCounterL_);
    }

    // Channel 1 (Right) -- dual-mono, independent RNNoise instance
    if (numChannels > 1 && rnnR_ != nullptr) {
        processChannel(buffer.getReadPointer(1), buffer.getWritePointer(1),
                       numSamples, rnnR_,
                       inputFifoR_, inputFifoWriteR_,
                       outputFifoR_, outputFifoReadR_, outputFifoWriteR_,
                       gateGainR_, holdCounterR_);
    }
}

juce::String BuiltinNoiseRemoval::getStatusText() const
{
    switch (getMode()) {
        case Mode::Native48k: return "Active: native 48 kHz";
        case Mode::Resampled: {
            const auto rate = juce::String(hostSampleRate_.load(std::memory_order_relaxed) / 1000.0, 1);
            return "Active: " + rate + " -> 48 -> " + rate + " kHz";
        }
        case Mode::UnsupportedRate: return "Inactive: unsupported rate (8-192 kHz)";
        case Mode::Unavailable: return "Inactive: RNNoise unavailable";
        default: return "Inactive: audio not prepared";
    }
}

void BuiltinNoiseRemoval::processConvertedChannel(float* samples, int count, int channel)
{
    auto& c = conversion_[channel];
    for (int i = 0; i < count; ++i) {
        // Consume this input before writing its output; no later sample is touched.
        c.to48k.push(samples[i]);
        while (c.to48k.canRead()) {
            float value = c.to48k.read();
            if (channel == 0)
                processChannel(&value, &value, 1, rnnL_, inputFifoL_, inputFifoWriteL_,
                    outputFifoL_, outputFifoReadL_, outputFifoWriteL_, gateGainL_, holdCounterL_);
            else
                processChannel(&value, &value, 1, rnnR_, inputFifoR_, inputFifoWriteR_,
                    outputFifoR_, outputFifoReadR_, outputFifoWriteR_, gateGainR_, holdCounterR_);
            c.from48k.push(value);
            while (c.from48k.canRead()) {
                const float output = c.from48k.read();
                jassert(c.queued < c.output.size());
                if (c.queued < c.output.size()) {
                    c.output[c.write] = output;
                    c.write = (c.write + 1) % c.output.size();
                    ++c.queued;
                }
            }
        }
        jassert(c.queued > 0);
        if (c.queued > 0) {
            samples[i] = c.output[c.read];
            c.read = (c.read + 1) % c.output.size();
            --c.queued;
        } else {
            samples[i] = 0.0f;
        }
    }
}

// ─── Per-channel FIFO + RNNoise processing ──────────────────────
//
// This is the core processing function, called once per channel per processBlock.
//
// ## 2-Pass Architecture (critical for correctness)
//
// `in` and `out` may alias. This native callback path stages all input before
// draining its output; the converted path invokes it with one consumed sample
// at a time. Neither path overwrites input that is still needed.
//
// Solution: 2-pass approach:
//   Pass 1: Read ALL input samples into inputFifo. Process complete frames through
//           RNNoise and store results in outputFifo. Never touch `out` buffer.
//   Pass 2: Copy processed samples from outputFifo to `out` buffer.
//
// This call consumes its input before writing any output, including scalar calls.
//
// ## Data Flow
//
//   Host input → inputFifo (accumulate) → [480 samples ready?] → scale to int16 range
//   → rnnoise_process_frame() → scale back to float → VAD gate → outputFifo → Host output

void BuiltinNoiseRemoval::processChannel(
    const float* in, float* out, int numSamples,
    DenoiseState* rnn,
    std::vector<float>& inputFifo,  int& inputFifoWrite,
    std::vector<float>& outputFifo, uint32_t& outputFifoRead, uint32_t& outputFifoWrite,
    float& gateGain, int& holdCounter)
{
    const float threshold = vadThreshold_.load(std::memory_order_relaxed);

    // Gate smoothing coefficient (gateSmooth_): controls how fast the gate opens/closes.
    // Derivation: for a 20ms time constant at sample rate SR:
    //   gateSmooth_ = exp(-1 / (SR * 0.020))
    //   At the internal 48 kHz rate: exp(-1/960) ≈ 0.9990.
    //
    // The 20ms time constant avoids audible gate clicks between words while
    // remaining short enough for natural speech transitions.
    // Configured at the internal rate in prepareToPlay (gateSmooth_).

    // IMPORTANT: RNNoise was trained on int16 audio data (range [-32767, +32767]).
    // JUCE provides float audio in [-1.0, +1.0]. We MUST scale up before processing
    // and scale back down after, or RNNoise treats all input as near-zero silence
    // and outputs garbage.
    constexpr float kScale = 32767.0f;
    constexpr float kInvScale = 1.0f / 32767.0f;

    // ══ PASS 1: Consume ALL host input, process complete RNNoise frames ══
    // IMPORTANT: in and out may alias (JUCE AudioProcessorGraph in-place buffer reuse).
    // We MUST read ALL input before writing ANY output. See function-level comment above.
    for (int i = 0; i < numSamples; ++i) {
        inputFifo[static_cast<size_t>(inputFifoWrite)] = in[i];
        ++inputFifoWrite;

        if (inputFifoWrite >= kRNNFrameSize) {
            float rnnIn[kRNNFrameSize];
            float rnnOut[kRNNFrameSize];

            for (int j = 0; j < kRNNFrameSize; ++j)
                rnnIn[j] = inputFifo[static_cast<size_t>(j)] * kScale;

            float vad = rnnoise_process_frame(rnn, rnnOut, rnnIn);

            // VAD gate with hold time — keeps gate open between words
            // holdCounter tracks how many samples since last voice detection
            float targetGate;
            if (vad >= threshold) {
                targetGate = 1.0f;
                holdCounter = 0;  // reset hold
            } else if (holdCounter < holdSamples_) {
                targetGate = 1.0f;  // still in hold period — stay open
                // Hold counter tracks time in SAMPLES, not frames. Since this decision runs once per
                // RNNoise frame (480 samples), advance by kRNNFrameSize (not by 1).
                // Incrementing by 1 would count frames as samples and make a
                // nominal 300ms hold last 480 times longer at 48kHz.
                holdCounter += kRNNFrameSize;
            } else {
                targetGate = 0.0f;  // hold expired — close gate
            }

            // Apply per-sample gate smoothing and store in output ring buffer.
            // NOTE: outputFifoWrite grows monotonically and wraps by the
            // runtime-sized FIFO capacity.
            // This ring buffer approach avoids the need for a linear reset of read/write
            // positions (which would require coordination). The modulo wrap is safe because
            // A power-of-two capacity divides the uint32_t counter range, so
            // modulo ring positions remain continuous across counter wrap.
            // uint32_t wraparound is handled correctly: the drain comparison uses
            // (write - read) > 0u instead of read < write, so unsigned modular
            // subtraction gives the correct count even after UINT32_MAX wraparound
            // (~25 hours at 48kHz).
            const auto outputCapacity = static_cast<uint32_t>(outputFifo.size());
            const auto queuedSamples = outputFifoWrite - outputFifoRead;
            jassert(outputCapacity >= static_cast<uint32_t>(kRNNFrameSize));
            jassert(queuedSamples <= outputCapacity - static_cast<uint32_t>(kRNNFrameSize));

            // A callback beyond the provisioned maximum must never overwrite
            // unread audio. Supported callbacks (<=4096, or the prepare hint
            // when larger) cannot enter this fallback.
            if (outputCapacity >= static_cast<uint32_t>(kRNNFrameSize)
                && queuedSamples <= outputCapacity - static_cast<uint32_t>(kRNNFrameSize)) {
                for (int j = 0; j < kRNNFrameSize; ++j) {
                    gateGain = gateSmooth_ * gateGain + (1.0f - gateSmooth_) * targetGate;
                    outputFifo[static_cast<size_t>(outputFifoWrite % outputCapacity)] = rnnOut[j] * kInvScale * gateGain;
                    ++outputFifoWrite;
                }
            }

            inputFifoWrite = 0;
        }
    }

    // ══ PASS 2: Drain output ring buffer to host output ══
    // Now safe to write to `out` -- all input has been consumed in Pass 1.
    for (int i = 0; i < numSamples; ++i) {
        if ((outputFifoWrite - outputFifoRead) > 0u) {
            const auto outputCapacity = static_cast<uint32_t>(outputFifo.size());
            out[i] = outputFifo[static_cast<size_t>(outputFifoRead % outputCapacity)];
            ++outputFifoRead;
        } else {
            // No processed data available yet -- output silence.
            // This happens during the initial latency fill period (first ~480 samples)
            // while the FIFO accumulates enough input for the first RNNoise frame.
            out[i] = 0.0f;
        }
    }
}

// ─── Strength / VAD threshold ───────────────────────────────────

void BuiltinNoiseRemoval::setStrength(int strength)
{
    strength = juce::jlimit(0, 2, strength);
    strength_.store(strength, std::memory_order_relaxed);

    float threshold;
    switch (strength) {
        case 0:  threshold = 0.50f; break;  // Light
        case 2:  threshold = 0.90f; break;  // Aggressive
        default: threshold = 0.70f; break;  // Standard
    }
    vadThreshold_.store(threshold, std::memory_order_relaxed);
}

void BuiltinNoiseRemoval::setVADThreshold(float threshold)
{
    vadThreshold_.store(juce::jlimit(0.0f, 1.0f, threshold), std::memory_order_relaxed);
}

// ─── State persistence (JSON) ───────────────────────────────────

void BuiltinNoiseRemoval::getStateInformation(juce::MemoryBlock& destData)
{
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty("strength", getStrength());
    obj->setProperty("vadThreshold", static_cast<double>(getVadThreshold()));

    auto json = juce::JSON::toString(juce::var(obj.release()));
    destData.replaceWith(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void BuiltinNoiseRemoval::setStateInformation(const void* data, int sizeInBytes)
{
    auto json = juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
    auto parsed = juce::JSON::parse(json);
    if (auto* obj = parsed.getDynamicObject()) {
        if (obj->hasProperty("strength"))
            setStrength(static_cast<int>(obj->getProperty("strength")));
        // A persisted advanced override takes precedence over the strength preset.
        if (obj->hasProperty("vadThreshold"))
            setVADThreshold(static_cast<float>(static_cast<double>(obj->getProperty("vadThreshold"))));
    }
}

// ─── Internal helpers ───────────────────────────────────────────

void BuiltinNoiseRemoval::destroyRNNoise()
{
    if (rnnL_ != nullptr) {
        rnnoise_destroy(rnnL_);
        rnnL_ = nullptr;
    }
    if (rnnR_ != nullptr) {
        rnnoise_destroy(rnnR_);
        rnnR_ = nullptr;
    }
}

} // namespace directpipe
