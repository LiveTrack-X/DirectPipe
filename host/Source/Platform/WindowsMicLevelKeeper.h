// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 LiveTrack
#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>

namespace directpipe {

class WindowsMicLevelKeeperTestAccess;

/** Optional, per-endpoint Windows master capture-level correction.
    All native work belongs to a worker, never the audio callback. */
class WindowsMicLevelKeeper : public juce::ChangeBroadcaster {
public:
    enum class State { unsupported, unavailable, ready, holding, error };

    struct InputContext {
        juce::String deviceType;
        juce::String inputDeviceName; // Change identity only; never a native-ID lookup.
        bool captureActive = false;
        juce::String endpointId; // Exact opened-device ID from the WASAPI adapter.
    };
    struct Endpoint {
        juce::String id;
        juce::String name;
    };
    struct Resolution {
        State state = State::unavailable;
        Endpoint endpoint;
        juce::String message;
    };
    struct Status {
        State state = State::unavailable;
        juce::String endpointId;
        juce::String endpointName;
        juce::String message = "Select an active supported Windows audio input.";
        bool canEnable = false;
        bool enabled = false;
    };
    struct Preferences {
        juce::StringArray enabledEndpointIds;
    };

    /** Injection boundary for deterministic tests. Native implementations must
        validate the exact opened capture endpoint, with no default endpoint,
        session inference or name guess. All methods except Callback run on one worker. */
    class Backend {
    public:
        using Callback = std::function<void(bool selfChange, bool disconnected)>;
        virtual ~Backend() = default;
        virtual Resolution resolve(const InputContext&) = 0;
        virtual bool attach(const juce::String& endpointId, Callback, juce::String& error) = 0;
        virtual bool readScalar(float& value, juce::String& error) = 0;
        virtual bool setScalar100(juce::String& error) = 0;
        virtual void detach() noexcept = 0;
    };

    explicit WindowsMicLevelKeeper(std::unique_ptr<Backend> backend = {}, bool startWorker = true);
    ~WindowsMicLevelKeeper() override;

    void updateInputContext(const InputContext&);
    void setStartupReady(bool ready); // Default false; restore never arms early.
    Status getStatus() const;
    bool setEnabledForCurrentEndpoint(bool enabled, juce::String& error);
    Preferences getPreferences() const;
    bool setPreferences(const Preferences&, juce::String& error);
    void stop(); // Idempotent shutdown; never restores a stale volume value.

    /** Strict present-field parser. Callers map an absent optional field to
        empty Preferences. Failure leaves the output untouched; no native work. */
    static bool parsePreferences(const juce::var&, Preferences&, juce::String& error);
    static juce::var serializePreferences(const Preferences&);

private:
    friend class WindowsMicLevelKeeperTestAccess;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WindowsMicLevelKeeper)
};

#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
class WindowsMicLevelKeeperTestAccess {
public:
    /** Uses the supplied fake backend with startWorker=false; no native API. */
    static void drain(WindowsMicLevelKeeper&, juce::int64 nowMs);
};
#endif

} // namespace directpipe
