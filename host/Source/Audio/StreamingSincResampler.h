// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <vector>

namespace directpipe {

/** Bounded streaming converter. prepare() allocates; push/read never allocate.
 *  Each output uses a phase-interpolated, Blackman-windowed low-pass kernel.
 *  The fractional clock stays bounded rather than growing with session length.
 *  Call read() until !canRead() before pushing the next input sample. */
class StreamingSincResampler {
public:
    static constexpr int kHalfLength = 64;
    static constexpr int kTaps = kHalfLength * 2;
    static constexpr int kPhases = 256;

    void prepare(double inputRate, double outputRate)
    {
        step_ = inputRate / outputRate;
        untilNext_ = kHalfLength + 1.0;
        history_.fill(0.0f);
        write_ = 0;
        coefficients_.resize((kPhases + 1) * kTaps);
        const double cutoff = 0.90 * (outputRate < inputRate ? outputRate / inputRate : 1.0);
        constexpr double pi = 3.14159265358979323846;
        for (int phase = 0; phase <= kPhases; ++phase) {
            const double fraction = static_cast<double>(phase) / kPhases;
            double sum = 0.0;
            for (int tap = 0; tap < kTaps; ++tap) {
                const double distance = fraction - (tap - kHalfLength + 1);
                const double z = pi * cutoff * distance;
                const double window = std::abs(distance) <= kHalfLength
                    ? 0.42 + 0.5 * std::cos(pi * distance / kHalfLength)
                           + 0.08 * std::cos(2.0 * pi * distance / kHalfLength)
                    : 0.0;
                const double value = cutoff * (std::abs(z) < 1.0e-12 ? 1.0 : std::sin(z) / z) * window;
                coefficients_[static_cast<size_t>(phase * kTaps + tap)] = static_cast<float>(value);
                sum += value;
            }
            for (int tap = 0; tap < kTaps; ++tap)
                coefficients_[static_cast<size_t>(phase * kTaps + tap)] /= static_cast<float>(sum);
        }
    }

    void push(float sample) noexcept
    {
        history_[static_cast<size_t>(write_)] = sample;
        write_ = (write_ + 1) % kHistory;
        untilNext_ -= 1.0;
    }

    bool canRead() const noexcept { return untilNext_ <= 0.0; }

    float read() noexcept
    {
        const int whole = static_cast<int>(std::floor(untilNext_));
        const double phase = (untilNext_ - whole) * kPhases;
        const int index = (std::min)(kPhases - 1, static_cast<int>(phase));
        const float blend = static_cast<float>(phase - index);
        const auto* a = coefficients_.data() + index * kTaps;
        const auto* b = a + kTaps;
        float result = 0.0f;
        for (int tap = 0; tap < kTaps; ++tap) {
            const int offset = kHalfLength - whole - (tap - kHalfLength + 1);
            const int pos = (write_ - 1 - offset + 2 * kHistory) % kHistory;
            result += history_[static_cast<size_t>(pos)] * (a[tap] + blend * (b[tap] - a[tap]));
        }
        untilNext_ += step_;
        return result;
    }

private:
    static constexpr int kHistory = kTaps + 2;
    std::array<float, kHistory> history_{};
    std::vector<float> coefficients_;
    double step_ = 1.0;
    double untilNext_ = kHalfLength + 1.0;
    int write_ = 0;
};

} // namespace directpipe
