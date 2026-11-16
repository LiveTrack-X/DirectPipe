// SPDX-License-Identifier: GPL-3.0-or-later
// Raw float32 mono in/out. Offline runner; never opens devices or app IPC.
#include "StftDenoiser.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc != 10) throw std::invalid_argument(
            "usage: stft-nr-probe input.f32 output.f32 stats.json rate N reductionDb learn learnSamples blockSize");
        const double rate = std::stod(argv[4]);
        const int size = std::stoi(argv[5]), blockSize = std::stoi(argv[9]);
        const auto learnSamples = std::stoull(argv[8]);
        if (blockSize < 1 || blockSize > 8192) throw std::invalid_argument("block size 1..8192 required");
        directpipe::experiment::StftDenoiser dsp;
        dsp.prepare(rate, size, std::stof(argv[6]));
        // argv[7] is reserved for the profile: currently only explicit Learn.
        if (std::string(argv[7]) != "learn") throw std::invalid_argument("profile must be learn");
        std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("cannot open input");
        const auto bytes = input.tellg();
        if (bytes < 0 || bytes > 256 * 1024 * 1024 || bytes % 4 != 0)
            throw std::invalid_argument("invalid float32 input size");
        const auto samples = static_cast<std::size_t>(bytes / 4);
        if (learnSamples > samples) throw std::invalid_argument("learning region exceeds input");
        std::vector<float> audio(samples + 2 * size, 0.0f);
        input.seekg(0); input.read(reinterpret_cast<char*>(audio.data()), static_cast<std::streamsize>(bytes));
        if (!input) throw std::runtime_error("cannot read input");
        std::vector<double> timings;
        timings.reserve(audio.size() / blockSize + 3);
        for (std::size_t offset = 0; offset < audio.size();) {
            const bool learn = offset < learnSamples;
            auto count = (std::min)(static_cast<std::size_t>(blockSize), audio.size() - offset);
            if (learn) count = (std::min)(count, static_cast<std::size_t>(learnSamples - offset));
            const auto before = std::chrono::steady_clock::now();
            dsp.process(audio.data() + offset, count, learn);
            const auto after = std::chrono::steady_clock::now();
            timings.push_back(std::chrono::duration<double>(after - before).count());
            offset += count;
        }
        const double total = std::accumulate(timings.begin(), timings.end(), 0.0);
        std::sort(timings.begin(), timings.end());
        const auto percentile = [&](double p) { return timings[static_cast<std::size_t>(p * (timings.size() - 1))] * 1000.0; };
        std::ofstream output(argv[2], std::ios::binary);
        output.write(reinterpret_cast<const char*>(audio.data()), static_cast<std::streamsize>(audio.size() * sizeof(float)));
        if (!output) throw std::runtime_error("cannot write output");
        std::ofstream stats(argv[3]);
        stats << std::setprecision(12)
              << "{\"rate\":" << rate << ",\"size\":" << size
              << ",\"block_samples\":" << blockSize << ",\"input_samples\":" << samples
              << ",\"reported_latency_samples\":" << dsp.latencySamples()
              << ",\"processing_seconds\":" << total << ",\"audio_seconds\":" << audio.size() / rate
              << ",\"p50_block_ms\":" << percentile(0.5) << ",\"p95_block_ms\":" << percentile(0.95)
              << ",\"p99_block_ms\":" << percentile(0.99) << ",\"max_block_ms\":" << percentile(1.0) << "}\n";
        if (!stats) throw std::runtime_error("cannot write statistics");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
