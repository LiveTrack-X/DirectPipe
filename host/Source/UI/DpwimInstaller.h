// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <thread>

namespace directpipe::dpwim {
struct Release {
    juce::String version, assetName, url, sha256;
};
struct Outcome {
    bool success = false, unchanged = false;
    juce::String message, version;
    juce::File bundle;
};
using Fetch = std::function<juce::Result(const juce::String&, juce::MemoryBlock&,
                                        int64_t, const std::atomic<bool>&)>;
using BundleInspector = std::function<juce::Result(const juce::File&, const juce::String&)>;

bool validVersion(const juce::String& version);
int compareVersions(const juce::String& a, const juce::String& b);
juce::Result parseRelease(const juce::var& json, Release& release);
juce::Result inspectBundle(const juce::File& bundle, const juce::String& version);
juce::File managedRoot();
Outcome installedCopy(const juce::File& root, const BundleInspector& inspector = inspectBundle);
/** Install into a new version directory; atomically publish its manifest last.
    Never replace a loaded plug-in, external installation, or existing version. */
Outcome installArchive(const juce::File& root, const Release& release,
                       const juce::MemoryBlock& archive, const std::atomic<bool>& cancel,
                       const BundleInspector& inspector = inspectBundle);
Outcome installLatest(const juce::File& root, const Fetch& fetch,
                      const std::atomic<bool>& cancel,
                      const BundleInspector& inspector = inspectBundle);
juce::Result fetchHttps(const juce::String& url, juce::MemoryBlock& bytes,
                       int64_t limit, const std::atomic<bool>& cancel);

/** Message-thread owner. Workers capture shared non-UI state only. */
class Installer final : private juce::Timer {
public:
    ~Installer() override;
    bool start(const juce::File& root, std::function<void(Outcome)> complete,
               Fetch fetch = fetchHttps);
    bool isBusy() const { return state_ != nullptr; }
private:
    struct State { std::atomic<bool> cancel{false}, done{false}; Outcome outcome; };
    void timerCallback() override;
    std::shared_ptr<State> state_;
    std::thread worker_;
    std::function<void(Outcome)> complete_;
};
} // namespace directpipe::dpwim
