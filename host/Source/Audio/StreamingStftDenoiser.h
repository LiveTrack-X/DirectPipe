// SPDX-License-Identifier: GPL-3.0-or-later
// Preallocated streaming STFT/Wiener core. One instance per mono channel.
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace directpipe {
class StreamingStftDenoiser {
public:
    void prepare(double rate, int size, float reductionDb = 12.0f) {
        if ((!std::isfinite(rate) || rate < 8000.0 || rate > 192000.0)
            || (size < 64 || size > 1024 || (size & (size - 1)) != 0)
            || !std::isfinite(reductionDb) || reductionDb < 0.0f || reductionDb > 24.0f)
            throw std::invalid_argument("8..192 kHz, power-of-two 64..1024 samples, 0..24 dB required");
        size_ = size;
        hop_ = size / 4;
        floor_ = std::pow(10.0f, -reductionDb / 20.0f);
        bypass_ = reductionDb == 0.0f;
        attack_ = static_cast<float>(std::exp(-hop_ / (rate * 0.002)));
        release_ = static_cast<float>(std::exp(-hop_ / (rate * 0.060)));
        noiseAlpha_ = static_cast<float>(1.0 - std::exp(-hop_ / (rate * 2.0)));
        input_.resize(size); output_.resize(2 * size); window_.resize(size);
        fft_.resize(size); roots_.resize(size / 2); reversed_.resize(size);
        const int bins = size / 2 + 1;
        power_.resize(bins); noise_.resize(bins); previousClean_.resize(bins);
        gain_.resize(bins); smoothPower_.resize(bins);
        analysisNoise_.resize(bins); analysisGains_.resize(bins);
        constexpr double pi = 3.14159265358979323846;
        for (int i = 0; i < size; ++i) {
            // Four overlapping periodic Hann windows sum to 2. Analysis and
            // synthesis each use sqrt(Hann); synthesis additionally scales 1/2.
            window_[i] = static_cast<float>(std::sqrt(0.5 - 0.5 * std::cos(2 * pi * i / size)));
            int reverse = 0;
            for (int bits = size, value = i; bits > 1; bits >>= 1, value >>= 1)
                reverse = (reverse << 1) | (value & 1);
            reversed_[i] = reverse;
        }
        for (int i = 0; i < size / 2; ++i) {
            const auto phase = -2 * pi * i / size;
            roots_[i] = {static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase))};
        }
        reset();
    }

    // Call only from one rendering thread. learn captures an explicit profile;
    // adaptNoise/autoInitialize separately control adaptive estimation. Gains
    // stay at unity until a captured, restored or automatic estimate is available.
    void process(float* samples, std::size_t count, bool learn = false, bool adaptNoise = true, bool autoInitialize = false) noexcept {
        if (size_ == 0 || samples == nullptr) return;
        for (std::size_t i = 0; i < count; ++i) {
            const auto value = std::isfinite(samples[i]) ? samples[i] : 0.0f;
            input_[inputPosition_] = value;
            inputPosition_ = (inputPosition_ + 1) % size_;
            learnWindowSamples_ = learn ? (std::min)(learnWindowSamples_ + 1, size_) : 0;
            samples[i] = output_[outputPosition_];
            output_[outputPosition_] = 0.0f;
            if (++sinceHop_ == hop_) {
                sinceHop_ = 0;
                processFrame(learn, adaptNoise, autoInitialize);
            }
            outputPosition_ = (outputPosition_ + 1) % (2 * size_);
        }
    }

    void reset() noexcept {
        inputPosition_ = outputPosition_ = sinceHop_ = learnWindowSamples_ = observedFrames_ = signalFrames_ = 0;
        learnedFrames_ = 0;
        analysisReady_ = false;
        for (auto* buffer : {&input_, &output_, &noise_, &previousClean_, &smoothPower_})
            std::fill(buffer->begin(), buffer->end(), 0.0f);
        std::fill(gain_.begin(), gain_.end(), 1.0f);
    }
    void clearNoiseProfile() noexcept {
        learnedFrames_ = 0;
        analysisReady_ = false;
        learnWindowSamples_ = 0;
        signalFrames_ = 0;
        for (auto* buffer : {&noise_, &previousClean_, &smoothPower_})
            std::fill(buffer->begin(), buffer->end(), 0.0f);
        std::fill(gain_.begin(), gain_.end(), 1.0f);
    }
    void setReductionDb(float db) noexcept {
        if (!std::isfinite(db)) return;
        db = (std::max)(0.0f, (std::min)(24.0f, db));
        floor_ = std::pow(10.0f, -db / 20.0f);
        bypass_ = db == 0.0f;
    }
    int copyNoiseProfile(float* destination, int capacity) const noexcept {
        const int bins = size_ / 2 + 1;
        if (size_ == 0 || learnedFrames_ == 0 || destination == nullptr || capacity < bins) return 0;
        std::copy(noise_.begin(), noise_.end(), destination);
        return bins;
    }
    bool setNoiseProfile(const float* source, int count) noexcept {
        if (size_ == 0 || source == nullptr || count != size_ / 2 + 1) return false;
        for (int i = 0; i < count; ++i)
            if (!std::isfinite(source[i]) || source[i] < 0.0f || source[i] > 1e12f) return false;
        std::copy(source, source + count, noise_.begin());
        std::fill(previousClean_.begin(), previousClean_.end(), 0.0f);
        std::fill(smoothPower_.begin(), smoothPower_.end(), 0.0f);
        learnedFrames_ = 1;
        analysisReady_ = false;
        learnWindowSamples_ = 0;
        return true;
    }
    int latencySamples() const noexcept { return size_; }
    // Rendering/lifecycle thread only. UI must consume a separate atomic snapshot.
    int copyAnalysis(float* inputPower, float* noisePower, float* gains, int capacity) const noexcept {
        const int bins = size_ / 2 + 1;
        if (!analysisReady_ || observedFrames_ < 4 || capacity < bins
            || inputPower == nullptr || noisePower == nullptr || gains == nullptr) return 0;
        std::copy(power_.begin(), power_.end(), inputPower);
        std::copy(analysisNoise_.begin(), analysisNoise_.end(), noisePower);
        std::copy(analysisGains_.begin(), analysisGains_.end(), gains);
        return bins;
    }
    bool hasNoiseProfile() const noexcept { return learnedFrames_ > 0; }
private:
    void transform(bool inverse) noexcept {
        for (int i = 0; i < size_; ++i)
            if (reversed_[i] > i) std::swap(fft_[i], fft_[reversed_[i]]);
        for (int length = 2; length <= size_; length *= 2) {
            const int half = length / 2, step = size_ / length;
            for (int base = 0; base < size_; base += length)
                for (int j = 0; j < half; ++j) {
                    const auto root = inverse ? std::conj(roots_[j * step]) : roots_[j * step];
                    const auto even = fft_[base + j];
                    const auto odd = fft_[base + j + half] * root;
                    fft_[base + j] = even + odd;
                    fft_[base + j + half] = even - odd;
                }
        }
        if (inverse) for (auto& value : fft_) value /= static_cast<float>(size_);
    }

    void processFrame(bool learn, bool adaptNoise, bool autoInitialize) noexcept {
        for (int j = 0; j < size_; ++j)
            fft_[j] = {input_[(inputPosition_ + j) % size_] * window_[j], 0.0f};
        transform(false);
        const int bins = size_ / 2 + 1;
        for (int k = 0; k < bins; ++k) power_[k] = std::norm(fft_[k]);
        observedFrames_ = (std::min)(observedFrames_ + 1, 4);
        const bool hasSignal = std::any_of(power_.begin(), power_.end(), [](float power) { return power > 1e-10f; });
        signalFrames_ = hasSignal ? (std::min)(signalFrames_ + 1, 5) : 0;
        // Learn may begin mid-hop with old audio still in the input history.
        // Accumulate only windows made entirely of post-request capture samples.
        const bool accumulate = learn && learnWindowSamples_ >= size_;
        const bool silentProfile = std::all_of(noise_.begin(), noise_.end(), [](float power) { return power <= 1e-12f; });
        const bool initialize = !learn && autoInitialize && observedFrames_ >= 4 && signalFrames_ >= 5
            && (learnedFrames_ == 0 || silentProfile);
        if (accumulate || initialize) ++learnedFrames_;
        for (int k = 0; k < bins; ++k) {
            const float power = 0.5f * power_[k]
                + 0.25f * power_[(std::max)(0, k - 1)]
                + 0.25f * power_[(std::min)(bins - 1, k + 1)];
            smoothPower_[k] = 0.9f * smoothPower_[k] + 0.1f * power;
            if (accumulate) noise_[k] += (power - noise_[k]) / static_cast<float>(learnedFrames_);
            if (initialize) noise_[k] = power;
            float target = 1.0f;
            if (!learn && learnedFrames_ > 0) {
                const float noise = (std::max)(noise_[k], 1e-12f);
                const float posterior = power / noise;
                const float prior = 0.92f * previousClean_[k] / noise
                    + 0.08f * (std::max)(posterior - 1.0f, 0.0f);
                if (!bypass_) target = (std::max)(floor_, prior / (1.0f + prior));
                // A conservative tracker: avoid learning loud voice or
                // collapsing the profile during clean/silent intervals.
                if (adaptNoise && posterior > 0.1f && posterior < 3.0f && smoothPower_[k] < 1.5f * noise)
                    noise_[k] += noiseAlpha_ * (power - noise_[k]);
            }
            const float smooth = target > gain_[k] ? attack_ : release_;
            gain_[k] = (bypass_ || learnedFrames_ == 0) ? 1.0f
                : smooth * gain_[k] + (1.0f - smooth) * target;
            previousClean_[k] = gain_[k] * gain_[k] * power;
            analysisNoise_[k] = noise_[k];
            analysisGains_[k] = gain_[k];
            fft_[k] *= gain_[k];
            if (k > 0 && k < size_ / 2) fft_[size_ - k] *= gain_[k];
        }
        analysisReady_ = true;
        transform(true);
        // Frame start is t-N+1. Schedule its j-th sample for t+1+j:
        // (t-N+1+j) + N. This yields exactly N samples delay regardless
        // of callback boundaries, including zero padding at startup/EOF.
        for (int j = 0; j < size_; ++j)
            output_[(outputPosition_ + 1 + j) % (2 * size_)] += fft_[j].real() * window_[j] * 0.5f;
    }

    int size_ = 0, hop_ = 0, inputPosition_ = 0, outputPosition_ = 0, sinceHop_ = 0;
    int learnWindowSamples_ = 0, observedFrames_ = 0, signalFrames_ = 0;
    std::size_t learnedFrames_ = 0;
    bool bypass_ = false;
    bool analysisReady_ = false;
    float floor_ = 0.25f, attack_ = 0.0f, release_ = 0.0f, noiseAlpha_ = 0.0f;
    std::vector<float> input_, output_, window_, power_, noise_, previousClean_, gain_, smoothPower_;
    std::vector<float> analysisNoise_, analysisGains_;
    std::vector<int> reversed_;
    std::vector<std::complex<float>> fft_, roots_;
};
}
