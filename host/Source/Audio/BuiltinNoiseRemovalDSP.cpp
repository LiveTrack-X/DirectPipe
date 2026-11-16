// SPDX-License-Identifier: GPL-3.0-or-later
#include "BuiltinNoiseRemovalDSP.h"
#include "../UI/DspNoiseRemovalEditPanel.h"
#include <algorithm>
#include <cmath>

namespace directpipe {

static_assert(std::atomic<float>::is_always_lock_free && std::atomic<double>::is_always_lock_free
    && std::atomic<uint64_t>::is_always_lock_free && std::atomic<int>::is_always_lock_free
    && std::atomic<bool>::is_always_lock_free, "DSP mailboxes require lock-free atomics");

BuiltinNoiseRemovalDSP::ProfileMailbox::ProfileMailbox() noexcept
{
    for (auto& channel : spectra) for (auto& bin : channel) bin.store(0);
}

BuiltinNoiseRemovalDSP::SpectrumMailbox::SpectrumMailbox() noexcept
{
    for (int channel = 0; channel < 2; ++channel)
        for (int bin = 0; bin < kSpectrumMaxBins; ++bin) {
            inputPower[channel][bin].store(0);
            noisePower[channel][bin].store(0);
            gains[channel][bin].store(1);
        }
}

void BuiltinNoiseRemovalDSP::SpectrumMailbox::publish(const SpectrumSnapshot& snapshot) noexcept
{
    sequence.fetch_add(1);
    generation.store(snapshot.generation);
    sampleRate.store(snapshot.sampleRate);
    fftSize.store(snapshot.fftSize);
    bins.store(snapshot.bins);
    flags.store((snapshot.active ? 1 : 0) | (snapshot.learning ? 2 : 0) | (snapshot.realtime ? 4 : 0)
        | (snapshot.profileReady[0] ? 8 : 0) | (snapshot.profileReady[1] ? 16 : 0));
    for (int channel = 0; channel < 2; ++channel)
        for (int bin = 0; bin < kSpectrumMaxBins; ++bin) {
            inputPower[channel][bin].store(snapshot.inputPower[channel][bin]);
            noisePower[channel][bin].store(snapshot.noisePower[channel][bin]);
            gains[channel][bin].store(snapshot.gains[channel][bin]);
        }
    sequence.fetch_add(1);
}

bool BuiltinNoiseRemovalDSP::getSpectrumSnapshot(SpectrumSnapshot& destination) const noexcept
{
    const auto before = spectrum_.sequence.load();
    if ((before & 1u) != 0) return false;
    SpectrumSnapshot snapshot;
    snapshot.generation = spectrum_.generation.load();
    snapshot.sampleRate = spectrum_.sampleRate.load();
    snapshot.fftSize = spectrum_.fftSize.load();
    snapshot.bins = spectrum_.bins.load();
    const auto flags = spectrum_.flags.load();
    snapshot.active = (flags & 1) != 0;
    snapshot.learning = (flags & 2) != 0;
    snapshot.realtime = (flags & 4) != 0;
    snapshot.profileReady = {(flags & 8) != 0, (flags & 16) != 0};
    for (int channel = 0; channel < 2; ++channel)
        for (int bin = 0; bin < kSpectrumMaxBins; ++bin) {
            snapshot.inputPower[channel][bin] = spectrum_.inputPower[channel][bin].load();
            snapshot.noisePower[channel][bin] = spectrum_.noisePower[channel][bin].load();
            snapshot.gains[channel][bin] = spectrum_.gains[channel][bin].load();
        }
    if (spectrum_.sequence.load() != before) return false;
    destination = snapshot;
    return true;
}

void BuiltinNoiseRemovalDSP::publishSpectrum(bool includeAnalysis) noexcept
{
    SpectrumSnapshot snapshot;
    snapshot.generation = ++spectrumGeneration_;
    snapshot.sampleRate = preparedFft_ > 0 ? preparedRate_ : 0;
    snapshot.fftSize = preparedFft_;
    snapshot.learning = capturing_;
    snapshot.realtime = renderMode_ == Mode::Realtime;
    if (includeAnalysis && isActive()) {
        const int bins = preparedFft_ / 2 + 1;
        bool complete = true;
        for (int channel = 0; channel < 2; ++channel) {
            complete &= cores_[channel].copyAnalysis(snapshot.inputPower[channel].data(),
                snapshot.noisePower[channel].data(), snapshot.gains[channel].data(), kSpectrumMaxBins) == bins;
            snapshot.profileReady[channel] = cores_[channel].hasNoiseProfile();
        }
        if (complete) {
            snapshot.active = true;
            snapshot.bins = bins;
            // sqrt-periodic-Hann has sum(window^2)=N/2. One-sided power/bin is
            // |FFT|^2/(N*sum(window^2)), doubled except for DC and Nyquist.
            // Summing all bins returns the window-weighted mean-square level.
            const float base = 2.0f / static_cast<float>(preparedFft_ * preparedFft_);
            for (int channel = 0; channel < 2; ++channel)
                for (int bin = 0; bin < bins; ++bin) {
                    const float scale = bin == 0 || bin == bins - 1 ? base : 2 * base;
                    auto& input = snapshot.inputPower[channel][bin];
                    auto& noise = snapshot.noisePower[channel][bin];
                    input = std::isfinite(input) ? juce::jmax(0.0f, input * scale) : 0;
                    noise = std::isfinite(noise) ? juce::jmax(0.0f, noise * scale) : 0;
                    auto& gain = snapshot.gains[channel][bin];
                    gain = std::isfinite(gain) ? juce::jlimit(0.0f, 1.0f, gain) : 1;
                }
        }
    }
    spectrum_.publish(snapshot);
}

void BuiltinNoiseRemovalDSP::ProfileMailbox::publish(const Profile& profile) noexcept
{
    // Sequential consistency makes observing the same even sequence around
    // atomic payload reads a coherent snapshot. Each mailbox has one writer.
    sequence.fetch_add(1);
    generation.store(profile.generation);
    cancelLearnThrough.store(profile.cancelLearnThrough);
    rate.store(profile.rate);
    fftSize.store(profile.fftSize);
    bins.store(profile.bins);
    for (int channel = 0; channel < 2; ++channel)
        for (int bin = 0; bin < kMaxBins; ++bin)
            spectra[static_cast<size_t>(channel)][static_cast<size_t>(bin)].store(
                profile.spectra[static_cast<size_t>(channel)][static_cast<size_t>(bin)]);
    sequence.fetch_add(1);
}

bool BuiltinNoiseRemovalDSP::ProfileMailbox::read(Profile& profile, uint64_t& observed) const noexcept
{
    const auto before = sequence.load();
    if ((before & 1u) != 0) return false;
    profile.generation = generation.load();
    profile.cancelLearnThrough = cancelLearnThrough.load();
    profile.rate = rate.load();
    profile.fftSize = fftSize.load();
    profile.bins = bins.load();
    for (int channel = 0; channel < 2; ++channel)
        for (int bin = 0; bin < kMaxBins; ++bin)
            profile.spectra[static_cast<size_t>(channel)][static_cast<size_t>(bin)] =
                spectra[static_cast<size_t>(channel)][static_cast<size_t>(bin)].load();
    observed = sequence.load();
    return before == observed;
}

BuiltinNoiseRemovalDSP::BuiltinNoiseRemovalDSP()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

int BuiltinNoiseRemovalDSP::fftSizeForRate(double rate) noexcept
{
    if (!std::isfinite(rate) || rate < 8000 || rate > 192000) return 0;
    int size = 64;
    while (size < 1024 && size < rate / 187.5) size *= 2;
    return size;
}

bool BuiltinNoiseRemovalDSP::validProfile(const Profile& profile) noexcept
{
    if (profile.fftSize == 0 || profile.fftSize != fftSizeForRate(profile.rate)
        || profile.bins != profile.fftSize / 2 + 1) return false;
    for (const auto& channel : profile.spectra)
        for (int bin = 0; bin < profile.bins; ++bin) {
            const auto value = channel[static_cast<size_t>(bin)];
            if (!std::isfinite(value) || value < 0 || value > 1.0e12f) return false;
        }
    return true;
}

bool BuiltinNoiseRemovalDSP::compatible(const Profile& profile) const noexcept
{
    return profile.bins > 0 && profile.rate == preparedRate_ && profile.fftSize == preparedFft_;
}

BuiltinNoiseRemovalDSP::Profile BuiltinNoiseRemovalDSP::snapshotProfile() const
{
    // Control/lifecycle only, with stateMutex_ held by the caller. This retry
    // loop must never be called by processBlock or another render-thread path.
    // The completed mailbox writer needs no control mutex, so saving cannot
    // prevent an in-progress publication from finishing. RT below instead makes
    // one bounded read attempt and defers contention to a later callback.
    Profile pending, completed;
    uint64_t unused = 0;
    while (!pendingRestore_.read(pending, unused)) {}
    while (!completed_.read(completed, unused)) {}
    return pending.generation > completed.generation ? pending : completed;
}

void BuiltinNoiseRemovalDSP::prepareToPlay(double rate, int)
{
    std::lock_guard<std::mutex> stateGuard(stateMutex_);
    active_.store(false, std::memory_order_release);
    preparedRate_ = rate;
    preparedFft_ = fftSizeForRate(rate);
    statusRate_.store(preparedFft_ == 0 ? 0 : rate);
    statusLatency_.store(preparedFft_);
    unsupported_.store(preparedFft_ == 0);
    setLatencySamples(preparedFft_);
    cancelLearning();
    compatibleProfile_.store(false, std::memory_order_release);
    lastLearnRequest_ = learnRequest_.load();
    handledLearnRequest_.store(lastLearnRequest_);
    spectrumCountdown_ = 0;
    publishSpectrum(false);
    if (preparedFft_ == 0) return;
    manual_ = snapshotProfile();
    seenRestoreSequence_ = pendingRestore_.sequence.load();
    captureTarget_ = static_cast<int>(std::llround(2 * rate));
    appliedReduction_ = getReductionDb();
    for (auto& core : cores_) core.prepare(rate, preparedFft_, appliedReduction_);
    renderMode_ = getMode();
    if (renderMode_ == Mode::Learn) loadManualIntoCores();
    compatibleProfile_.store(compatible(manual_), std::memory_order_release);
    active_.store(true, std::memory_order_release);
}

void BuiltinNoiseRemovalDSP::releaseResources()
{
    // Lifecycle excludes rendering; completed state survives release/prepare.
    active_.store(false, std::memory_order_release);
    unsupported_.store(false);
    compatibleProfile_.store(false, std::memory_order_release);
    cancelLearning();
    statusLatency_.store(0);
    statusRate_.store(0);
    setLatencySamples(0);
    publishSpectrum(false);
}

void BuiltinNoiseRemovalDSP::setMode(Mode mode)
{
    if (mode != Mode::Learn && mode != Mode::Realtime) mode = Mode::Learn;
    requestedMode_.store(mode, std::memory_order_release);
}

void BuiltinNoiseRemovalDSP::startLearning()
{
    if (isActive() && getMode() == Mode::Learn) {
        progress_.store(0);
        learnRequest_.fetch_add(1, std::memory_order_release);
    }
}

bool BuiltinNoiseRemovalDSP::isLearning() const
{
    return isActive() && getMode() == Mode::Learn
        && (learning_.load(std::memory_order_acquire)
            || learnRequest_.load(std::memory_order_acquire) != handledLearnRequest_.load(std::memory_order_acquire));
}

void BuiltinNoiseRemovalDSP::setReductionDb(float value)
{
    if (std::isfinite(value)) reduction_.store(juce::jlimit(0.0f, 24.0f, value), std::memory_order_relaxed);
}

void BuiltinNoiseRemovalDSP::cancelLearning() noexcept
{
    capturing_ = false;
    captured_ = 0;
    learning_.store(false, std::memory_order_release);
    progress_.store(0);
}

void BuiltinNoiseRemovalDSP::loadManualIntoCores() noexcept
{
    for (size_t channel = 0; channel < cores_.size(); ++channel) {
        cores_[channel].clearNoiseProfile();
        if (compatible(manual_)) cores_[channel].setNoiseProfile(manual_.spectra[channel].data(), manual_.bins);
    }
}

void BuiltinNoiseRemovalDSP::processSegment(juce::AudioBuffer<float>& buffer, int offset, int count, bool learn) noexcept
{
    const bool realtime = renderMode_ == Mode::Realtime;
    for (int channel = 0; channel < 2; ++channel) {
        auto& core = cores_[static_cast<size_t>(channel)];
        if (channel < buffer.getNumChannels()) {
            core.process(buffer.getWritePointer(channel, offset), static_cast<size_t>(count), learn, realtime, realtime);
        } else {
            // A missing mono channel advances with silence. Scratch is bounded
            // stack memory, so arbitrary callback sizes require no allocation.
            std::array<float, 1024> silence{};
            for (int remaining = count; remaining > 0;) {
                const int chunk = (std::min)(remaining, static_cast<int>(silence.size()));
                std::fill(silence.begin(), silence.end(), 0.0f);
                core.process(silence.data(), static_cast<size_t>(chunk), learn, realtime, realtime);
                remaining -= chunk;
            }
        }
    }
}

void BuiltinNoiseRemovalDSP::completeLearning() noexcept
{
    Profile candidate;
    candidate.generation = manual_.generation;
    candidate.cancelLearnThrough = lastLearnRequest_;
    candidate.rate = preparedRate_;
    candidate.fftSize = preparedFft_;
    candidate.bins = preparedFft_ / 2 + 1;
    for (size_t channel = 0; channel < cores_.size(); ++channel)
        if (cores_[channel].copyNoiseProfile(candidate.spectra[channel].data(), kMaxBins) != candidate.bins)
            candidate.bins = 0;
    capturing_ = false;
    if (validProfile(candidate)) {
        manual_ = candidate;
        completed_.publish(manual_);
        compatibleProfile_.store(true, std::memory_order_release);
        progress_.store(1);
    } else {
        // Invalid/overflowed captures never replace a completed manual profile.
        loadManualIntoCores();
        compatibleProfile_.store(compatible(manual_), std::memory_order_release);
        progress_.store(0);
    }
    learning_.store(false, std::memory_order_release);
}

void BuiltinNoiseRemovalDSP::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (!isActive()) return;
    const auto requestedMode = getMode();
    const auto pendingSequence = pendingRestore_.sequence.load();
    if (pendingSequence != seenRestoreSequence_ && (pendingSequence & 1u) == 0) {
        Profile pending;
        uint64_t observed = 0;
        // One bounded attempt; contended restores wait for the next callback.
        if (pendingRestore_.read(pending, observed)) {
            seenRestoreSequence_ = observed;
            manual_ = pending;
            cancelLearning();
            lastLearnRequest_ = pending.cancelLearnThrough;
            handledLearnRequest_.store(lastLearnRequest_);
            compatibleProfile_.store(compatible(manual_), std::memory_order_release);
            if (requestedMode == Mode::Learn) loadManualIntoCores();
        }
    }
    if (renderMode_ != requestedMode) {
        cancelLearning();
        renderMode_ = requestedMode;
        if (renderMode_ == Mode::Learn) loadManualIntoCores();
        else for (auto& core : cores_) core.clearNoiseProfile();
    }
    const auto request = learnRequest_.load(std::memory_order_acquire);
    if (request != lastLearnRequest_) {
        lastLearnRequest_ = request;
        if (renderMode_ == Mode::Learn) {
            for (auto& core : cores_) core.clearNoiseProfile();
            captured_ = 0;
            capturing_ = true;
            learning_.store(true, std::memory_order_release);
            progress_.store(0);
        }
        handledLearnRequest_.store(request, std::memory_order_release);
    }
    const float reduction = getReductionDb();
    if (reduction != appliedReduction_) {
        for (auto& core : cores_) core.setReductionDb(reduction);
        appliedReduction_ = reduction;
    }
    int offset = 0;
    while (offset < buffer.getNumSamples()) {
        const int count = capturing_ ? (std::min)(buffer.getNumSamples() - offset, captureTarget_ - captured_)
                                     : buffer.getNumSamples() - offset;
        processSegment(buffer, offset, count, capturing_);
        offset += count;
        if (capturing_) {
            captured_ += count;
            progress_.store(static_cast<float>(captured_) / static_cast<float>(captureTarget_));
            if (captured_ == captureTarget_) completeLearning();
        }
    }
    // Metering reuses existing analysis; no transform, audio queue or clock call.
    // The audio thread never waits for UI readers, even with the editor closed.
    if (buffer.getNumSamples() > 0) {
        spectrumCountdown_ -= buffer.getNumSamples();
        if (spectrumCountdown_ <= 0) {
            publishSpectrum(true);
            spectrumCountdown_ = (std::max)(1, static_cast<int>(std::llround(preparedRate_ / 20.0)));
        }
    }
}

juce::String BuiltinNoiseRemovalDSP::getStatusText() const
{
    const bool active = isActive();
    const int delay = statusLatency_.load();
    const double rate = statusRate_.load();
    const auto latency = "DSP latency: " + juce::String(delay) + " samples ("
        + juce::String(rate > 0 ? 1000.0 * delay / rate : 0, 3) + " ms)";
    if (!active) return (unsupported_.load() ? "Inactive: unsupported rate (8-192 kHz); "
                                           : "Inactive: audio not prepared; ") + latency;
    juce::String text;
    if (getMode() == Mode::Realtime)
        text = "Realtime: adaptive estimate" + juce::String(hasLearnedProfile() ? "; manual profile retained" : "");
    else if (isLearning()) text = "Learn: capturing noise only (" + juce::String(getLearnProgress() * 100, 1) + "%)";
    else text = hasLearnedProfile() ? "Learn: frozen profile" : "Learn required: capture 2 seconds of noise only";
    return text + "\n" + latency;
}

juce::AudioProcessorEditor* BuiltinNoiseRemovalDSP::createEditor()
{
    return new DspNoiseRemovalEditPanel(*this);
}

void BuiltinNoiseRemovalDSP::getStateInformation(juce::MemoryBlock& destination)
{
    std::lock_guard<std::mutex> stateGuard(stateMutex_);
    const auto profile = snapshotProfile();
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("version", 1);
    object->setProperty("mode", static_cast<int>(getMode()));
    object->setProperty("reductionDb", getReductionDb());
    if (validProfile(profile)) {
        auto saved = std::make_unique<juce::DynamicObject>();
        saved->setProperty("rate", profile.rate);
        saved->setProperty("fftSize", profile.fftSize);
        juce::Array<juce::var> channels;
        for (const auto& channel : profile.spectra) {
            juce::Array<juce::var> values;
            for (int bin = 0; bin < profile.bins; ++bin) values.add(channel[static_cast<size_t>(bin)]);
            channels.add(juce::var(values));
        }
        saved->setProperty("channels", channels);
        object->setProperty("profile", juce::var(saved.release()));
    }
    const auto json = juce::JSON::toString(juce::var(object.release()));
    destination.replaceAll(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void BuiltinNoiseRemovalDSP::setStateInformation(const void* data, int bytes)
{
    const auto parsed = data != nullptr && bytes > 0
        ? juce::JSON::parse(juce::String::fromUTF8(static_cast<const char*>(data), bytes)) : juce::var();
    Profile profile;
    Mode mode = Mode::Learn;
    float reduction = 12;
    const auto numeric = [](const juce::var& value) { return value.isInt() || value.isInt64() || value.isDouble(); };
    if (parsed.isObject()) {
        const auto modeValue = parsed["mode"];
        if (numeric(modeValue) && static_cast<double>(modeValue) == 1) mode = Mode::Realtime;
        const auto reductionValue = parsed["reductionDb"];
        if (numeric(reductionValue) && std::isfinite(static_cast<double>(reductionValue)))
            reduction = static_cast<float>(juce::jlimit(0.0, 24.0, static_cast<double>(reductionValue)));
        const auto saved = parsed["profile"];
        if (saved.isObject() && numeric(saved["rate"]) && numeric(saved["fftSize"])) {
            profile.rate = static_cast<double>(saved["rate"]);
            const double sizeValue = static_cast<double>(saved["fftSize"]);
            // Validate before conversion, including huge finite JSON numbers.
            const int expectedSize = fftSizeForRate(profile.rate);
            if (expectedSize != 0 && sizeValue == expectedSize) {
                profile.fftSize = expectedSize;
                profile.bins = expectedSize / 2 + 1;
                const auto channelsValue = saved["channels"];
                const auto* channels = channelsValue.getArray();
                bool valid = channels != nullptr && channels->size() == 2;
                for (int channel = 0; valid && channel < 2; ++channel) {
                    const auto* values = channels->getReference(channel).getArray();
                    valid = values != nullptr && values->size() == profile.bins;
                    for (int bin = 0; valid && bin < profile.bins; ++bin) {
                        const auto& value = values->getReference(bin);
                        const double power = static_cast<double>(value);
                        valid = numeric(value) && std::isfinite(power) && power >= 0 && power <= 1.0e12;
                        if (valid) profile.spectra[static_cast<size_t>(channel)][static_cast<size_t>(bin)] = static_cast<float>(power);
                    }
                }
                if (!valid) profile = Profile{};
            }
        }
    }
    if (!validProfile(profile)) profile = Profile{};
    std::lock_guard<std::mutex> stateGuard(stateMutex_);
    profile.generation = ++nextRestoreGeneration_;
    profile.cancelLearnThrough = learnRequest_.load();
    pendingRestore_.publish(profile);
    setMode(mode);
    setReductionDb(reduction);
    progress_.store(0);
    const auto rate = statusRate_.load();
    const auto size = statusLatency_.load();
    compatibleProfile_.store(isActive() && profile.bins > 0 && profile.rate == rate && profile.fftSize == size,
                             std::memory_order_release);
}
}
