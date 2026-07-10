// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 LiveTrack

/**
 * @file EndpointChangeWatcher.h
 * @brief Platform audio endpoint property/state notifications.
 */
#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>

namespace directpipe {

#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
class EndpointChangeWatcherTestAccess;
#endif

class EndpointChangeWatcher {
public:
    using Callback = std::function<void(const juce::String& deviceName,
                                        const juce::String& reason)>;

    EndpointChangeWatcher();
    ~EndpointChangeWatcher();

    void setInputDeviceName(const juce::String& deviceName);
    void setCallback(Callback callback);
    void start();
    void stop();

private:
#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
    friend class EndpointChangeWatcherTestAccess;
#endif

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
class EndpointChangeWatcherTestAccess {
public:
    enum class Event {
        deviceAdded,
        deviceRemoved,
        deviceStateChanged,
        propertyChanged,
        defaultDeviceChanged,
    };

    static void setResolvedEndpointId(EndpointChangeWatcher& watcher,
                                      const juce::String& endpointId);
    static bool signalEndpointEvent(EndpointChangeWatcher& watcher,
                                    const juce::String& endpointId,
                                    Event event,
                                    unsigned long state = 0);
    static int drainPendingEvents(EndpointChangeWatcher& watcher);
};
#endif

} // namespace directpipe
