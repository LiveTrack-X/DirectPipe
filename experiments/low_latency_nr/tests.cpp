// SPDX-License-Identifier: GPL-3.0-or-later
#include "StftDenoiser.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

static thread_local bool trackAllocations = false;
static thread_local std::size_t allocations = 0;
void* operator new(std::size_t size) {
    if (trackAllocations) ++allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using directpipe::experiment::StftDenoiser;
static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static void render(StftDenoiser& dsp, std::vector<float>& audio,
                   const std::vector<int>& blocks, bool learn = false) {
    std::size_t offset = 0, index = 0;
    while (offset < audio.size()) {
        const auto count = (std::min)(static_cast<std::size_t>(blocks[index++ % blocks.size()]),
                                      audio.size() - offset);
        dsp.process(audio.data() + offset, count, learn);
        offset += count;
    }
}
static double rms(const std::vector<float>& x, std::size_t begin, std::size_t end) {
    double sum = 0.0;
    for (auto i = begin; i < end; ++i) sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(end - begin));
}

int main() {
    int failed = 0, passed = 0;
    const auto test = [&](const std::string& name, const std::function<void()>& run) {
        try { run(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    };
    for (const double rate : {44100.0, 48000.0}) for (const int size : {256, 512}) {
        const auto suffix = " N=" + std::to_string(size) + " rate=" + std::to_string(static_cast<int>(rate));
        test("unity reconstructs random signal including startup/tail" + suffix, [&] {
            StftDenoiser dsp; dsp.prepare(rate, size, 0.0f);
            std::mt19937 rng(123);
            std::uniform_real_distribution<float> dist(-0.4f, 0.4f);
            std::vector<float> original(4093);
            for (auto& value : original) value = dist(rng);
            auto output = original; output.resize(original.size() + 2 * size, 0.0f);
            render(dsp, output, {1, 17, 64, 0, 257, 4096});
            require(dsp.latencySamples() == size, "incorrect declared delay");
            for (int i = 0; i < size; ++i) require(std::abs(output[i]) < 2e-6f, "startup emitted undelayed input");
            for (std::size_t i = 0; i < original.size(); ++i)
                require(std::abs(output[i + size] - original[i]) < 2e-5f, "WOLA reconstruction/delay mismatch");
        });
        test("impulse has same exact delay at every hop phase and EOF" + suffix, [&] {
            for (int phase = 0; phase < size / 4; ++phase) {
                StftDenoiser dsp; dsp.prepare(rate, size, 0.0f);
                const int last = size + phase;
                std::vector<float> output(last + 1 + 2 * size, 0.0f);
                output[phase] = 0.5f; output[last] = -0.25f;
                render(dsp, output, {31, 1, 128});
                for (std::size_t i = 0; i < output.size(); ++i) {
                    const float expected = i == static_cast<std::size_t>(phase + size) ? 0.5f
                        : i == static_cast<std::size_t>(last + size) ? -0.25f : 0.0f;
                    require(std::abs(output[i] - expected) < 2e-5f, "phase-dependent impulse/tail delay");
                }
            }
        });
        test("partition invariant under learned suppression" + suffix, [&] {
            StftDenoiser first, second; first.prepare(rate, size); second.prepare(rate, size);
            std::mt19937 rng(456); std::normal_distribution<float> noise(0.0f, 0.015f);
            std::vector<float> learning(static_cast<int>(rate));
            for (auto& x : learning) x = noise(rng);
            auto copy = learning;
            render(first, learning, {128}, true); render(second, copy, {1, 63, 257, 0}, true);
            std::vector<float> a(48000), b;
            for (std::size_t i = 0; i < a.size(); ++i) a[i] = noise(rng) + 0.1f * std::sin(static_cast<float>(i * 0.03));
            b = a; render(first, a, {128}); render(second, b, {17, 1, 2048, 64, 0});
            require(a == b, "callback partition changed output");
        });
        test("learned held-out noise is reduced without muting a strong tone" + suffix, [&] {
            StftDenoiser dsp; dsp.prepare(rate, size, 12.0f);
            std::mt19937 rng(78); std::normal_distribution<float> noise(0.0f, 0.02f);
            std::vector<float> learning(static_cast<int>(rate));
            for (auto& x : learning) x = noise(rng);
            render(dsp, learning, {128}, true);
            std::vector<float> heldOut(static_cast<int>(rate));
            for (auto& x : heldOut) x = noise(rng);
            render(dsp, heldOut, {128});
            require(rms(heldOut, size * 2, heldOut.size()) < 0.014, "less than 3 dB held-out noise reduction");
            std::vector<float> tone(static_cast<int>(rate));
            for (std::size_t i = 0; i < tone.size(); ++i)
                tone[i] = 0.2f * std::sin(static_cast<float>(6.283185307179586 * 750.0 * i / rate));
            render(dsp, tone, {128});
            require(rms(tone, size * 4, tone.size()) > 0.12, "strong voice-band tone suppressed excessively");
        });
        test("processing allocates nothing and reset is deterministic" + suffix, [&] {
            StftDenoiser dsp; dsp.prepare(rate, size);
            std::vector<float> input(8192, 0.01f), a = input, b = input;
            allocations = 0; trackAllocations = true;
            dsp.process(nullptr, 0); dsp.process(a.data(), a.size(), true);
            dsp.process(a.data(), a.size()); dsp.reset();
            trackAllocations = false;
            require(allocations == 0, "allocation in render/reset");
            render(dsp, a = input, {128}, true);
            dsp.reset(); render(dsp, b, {128}, true);
            require(a == b, "reset did not clear all DSP state");
        });
        test("silence remains finite and exactly silent" + suffix, [&] {
            StftDenoiser dsp; dsp.prepare(rate, size);
            std::vector<float> silence(12000);
            render(dsp, silence, {1, 64, 128}, true); render(dsp, silence, {128});
            for (const auto x : silence) require(std::isfinite(x) && x == 0.0f, "silence created NaN/noise");
        });
        test("unlearned startup voice passes with delay rather than becoming a noise profile" + suffix, [&] {
            StftDenoiser dsp; dsp.prepare(rate, size);
            std::vector<float> original(4096);
            for (std::size_t i = 0; i < original.size(); ++i)
                original[i] = 0.15f * std::sin(static_cast<float>(i * 0.04));
            auto output = original; output.resize(output.size() + 2 * size, 0.0f);
            render(dsp, output, {1, 128, 257});
            for (std::size_t i = 0; i < original.size(); ++i)
                require(std::abs(output[i + size] - original[i]) < 2e-5f, "startup voice learned or attenuated implicitly");
        });
        test("nonfinite input cannot poison subsequent silence" + suffix, [&] {
            StftDenoiser dsp; dsp.prepare(rate, size);
            std::vector<float> output(8192, 0.0f);
            output[0] = std::numeric_limits<float>::quiet_NaN();
            output[43] = std::numeric_limits<float>::infinity();
            output[95] = -std::numeric_limits<float>::infinity();
            render(dsp, output, {64, 128}, true);
            for (const auto x : output) require(std::isfinite(x) && x == 0.0f, "nonfinite input poisoned output");
        });
    }
    test("invalid settings rejected", [] {
        for (const auto rate : {0.0, 7999.0, 192001.0, std::numeric_limits<double>::quiet_NaN(),
                                std::numeric_limits<double>::infinity()}) {
            bool rejected = false; try { StftDenoiser x; x.prepare(rate, 256); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "unsupported rate accepted");
        }
        bool rejected = false; try { StftDenoiser x; x.prepare(48000.0, 300); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "invalid FFT size accepted");
        rejected = false;
        try { StftDenoiser x; x.prepare(48000.0, 256, std::numeric_limits<float>::quiet_NaN()); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "NaN reduction accepted");
    });
    test("native supported rates can use a bounded scaled FFT with exact delay", [] {
        for (const auto rate : {8000.0, 16000.0, 32000.0, 88200.0, 96000.0, 192000.0}) {
            int size = 64;
            while (size < rate / 187.5) size *= 2;
            StftDenoiser dsp; dsp.prepare(rate, size, 0.0f);
            std::vector<float> impulse(4 * size, 0.0f); impulse[0] = 0.5f;
            render(dsp, impulse, {1, 64, 17, 257});
            for (int i = 0; i < 4 * size; ++i)
                require(std::abs(impulse[i] - (i == size ? 0.5f : 0.0f)) < 2e-5f, "scaled FFT delay mismatch");
        }
    });
    test("frozen profile persists and can be restored without resetting the audio timeline", [] {
        StftDenoiser dsp; dsp.prepare(48000.0, 256);
        std::vector<float> noise(48000);
        std::mt19937 rng(1234); std::normal_distribution<float> dist(0, 0.02f);
        for (auto& x : noise) x = dist(rng);
        render(dsp, noise, {128}, true);
        std::vector<float> before(129), after(129);
        require(dsp.copyNoiseProfile(before.data(), 129) == 129, "completed profile unavailable");
        std::vector<float> voice(48000, 0.1f);
        dsp.process(voice.data(), voice.size(), false, false);
        require(dsp.copyNoiseProfile(after.data(), 129) == 129 && before == after, "frozen profile changed during speech");
        StftDenoiser restored; restored.prepare(48000.0, 256);
        require(restored.setNoiseProfile(before.data(), 129), "valid saved profile rejected");
        require(restored.copyNoiseProfile(after.data(), 129) == 129 && before == after, "profile restore changed learned power");
        before[0] = -1;
        require(!restored.setNoiseProfile(before.data(), 129), "negative noise power accepted");
    });
    test("automatic estimation initializes without explicit learning", [] {
        StftDenoiser dsp; dsp.prepare(48000.0, 256);
        std::vector<float> noise(48000, 0.01f), profile(129);
        dsp.process(noise.data(), noise.size(), false, true, true);
        require(dsp.copyNoiseProfile(profile.data(), 129) == 129, "realtime failed to initialize profile");
        dsp.clearNoiseProfile();
        require(dsp.copyNoiseProfile(profile.data(), 129) == 0, "cleared profile remained ready");
        require(dsp.latencySamples() == 256, "clearing noise reset reported latency");
    });
    test("learn capture excludes loud audio from before the request", [] {
        StftDenoiser dsp; dsp.prepare(48000.0, 256);
        std::vector<float> loud(777, 0.8f);
        dsp.process(loud.data(), loud.size());
        dsp.clearNoiseProfile();
        std::vector<float> quiet(96000, 0.0f), profile(129);
        render(dsp, quiet, {1, 97, 4096}, true);
        require(dsp.copyNoiseProfile(profile.data(), 129) == 129, "quiet capture was not learned");
        for (const auto value : profile)
            require(value == 0.0f, "pre-Learn audio contaminated the frozen profile");
    });
    test("profile clear and restore preserve the exact unity audio timeline", [] {
        StftDenoiser dsp; dsp.prepare(48000.0, 256, 0.0f);
        std::vector<float> input(4096), output, profile(129, 0.01f);
        for (std::size_t i = 0; i < input.size(); ++i)
            input[i] = 0.2f * std::sin(static_cast<float>(i * 0.04));
        output = input; output.resize(output.size() + 512, 0.0f);
        dsp.process(output.data(), 77);
        dsp.clearNoiseProfile();
        dsp.process(output.data() + 77, 113);
        require(dsp.setNoiseProfile(profile.data(), 129), "profile install failed");
        dsp.process(output.data() + 190, output.size() - 190, false, false);
        for (std::size_t i = 0; i < input.size(); ++i)
            require(std::abs(output[i + 256] - input[i]) < 2e-5f, "profile transition reset the audio timeline");
    });
    test("realtime recovers from silent startup when stationary noise appears", [] {
        StftDenoiser dsp; dsp.prepare(48000.0, 256);
        std::vector<float> silent(48000, 0.0f);
        dsp.process(silent.data(), silent.size(), false, true, true);
        std::mt19937 rng(113); std::normal_distribution<float> dist(0, 0.02f);
        std::vector<float> noise(192000), profile(129);
        for (auto& x : noise) x = dist(rng);
        dsp.process(noise.data(), noise.size(), false, true, true);
        require(dsp.copyNoiseProfile(profile.data(), 129) == 129, "realtime profile unavailable");
        require(*std::max_element(profile.begin(), profile.end()) > 1e-5f, "silent startup locked the noise estimate to zero");
        require(rms(noise, 144000, noise.size()) < 0.016, "realtime failed to suppress noise after silent startup");
    });
    test("realtime tracking continues at zero reduction while audio stays unity", [] {
        StftDenoiser dsp; dsp.prepare(48000.0, 256, 0.0f);
        std::mt19937 rng(889); std::normal_distribution<float> dist(0, 0.02f);
        std::vector<float> learning(48000), before(129), after(129);
        for (auto& x : learning) x = dist(rng);
        render(dsp, learning, {128}, true);
        require(dsp.copyNoiseProfile(before.data(), 129) == 129, "learning profile missing");
        std::vector<float> input(48000), output;
        for (auto& x : input) x = dist(rng) * 0.8f;
        output = input; output.resize(output.size() + 512, 0.0f);
        dsp.process(output.data(), output.size(), false, true, true);
        require(dsp.copyNoiseProfile(after.data(), 129) == 129 && before != after, "zero reduction disabled realtime estimation");
        for (std::size_t i = 0; i < input.size(); ++i)
            require(std::abs(output[i + 256] - input[i]) < 2e-5f, "tracking at zero reduction altered unity audio");
    });
    test("analysis snapshot exposes actual FFT input power and unity gains", [] {
        StftDenoiser dsp; dsp.prepare(48000, 256, 0);
        std::array<float, 129> input{}, noise{}, gains{};
        require(dsp.copyAnalysis(input.data(), noise.data(), gains.data(), 129) == 0, "analysis claimed ready before audio");
        std::vector<float> tone(2048);
        for (std::size_t i = 0; i < tone.size(); ++i)
            tone[i] = std::sin(static_cast<float>(6.283185307179586 * 1500 * i / 48000));
        dsp.process(tone.data(), tone.size());
        allocations = 0; trackAllocations = true;
        const int copied = dsp.copyAnalysis(input.data(), noise.data(), gains.data(), 129);
        trackAllocations = false;
        require(copied == 129, "actual analysis unavailable");
        require(allocations == 0, "analysis copy allocated in rendering");
        const auto peak = std::max_element(input.begin(), input.end()) - input.begin();
        require(peak == 8, "analysis did not expose actual 1500Hz FFT bin");
        double normalizedPower = 0;
        for (int k = 0; k < 129; ++k) {
            normalizedPower += input[k] * (k == 0 || k == 128 ? 2.0 : 4.0) / (256.0 * 256.0);
            require(gains[k] == 1.0f, "zero reduction showed a fabricated mask");
        }
        require(std::abs(normalizedPower - 0.5) < 1e-5, "single-sided power normalization does not conserve sine RMS");
    });
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
