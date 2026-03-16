// host/Source/Util/ThreadOwned.h
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>

namespace directpipe {

// ─── Thread tag types (zero-cost marker types) ────────────────────
struct MessageThread {};
struct AudioThread {};
struct AnyThread {};

namespace detail {

template<typename Tag>
inline bool isCorrectThread();

template<>
inline bool isCorrectThread<MessageThread>()
{
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    return mm != nullptr && mm->isThisTheMessageThread();
}

template<>
inline bool isCorrectThread<AudioThread>()
{
    // RT thread check: we're NOT on the message thread.
    // JUCE doesn't expose a "is this the audio thread" API, so we
    // approximate: the audio callback runs on a non-message thread.
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    return mm == nullptr || !mm->isThisTheMessageThread();
}

template<>
inline bool isCorrectThread<AnyThread>()
{
    return true;
}

} // namespace detail

/**
 * @brief Thread ownership wrapper — enforces single-thread access at runtime.
 *
 * Wraps a value that should only be accessed from a specific thread.
 * In debug builds, every access asserts the correct thread.
 * In release builds, zero overhead (assertions compile out).
 *
 * Usage:
 *   ThreadOwned<juce::String, MessageThread> desiredInputDevice_;
 *
 *   // Access (debug-checked):
 *   desiredInputDevice_.get(MessageThread{}) = newName;
 *   auto name = desiredInputDevice_.get(MessageThread{});
 *
 *   // Wrong thread → jassert fires in debug
 */
template<typename T, typename ThreadTag>
class ThreadOwned {
    T value_{};

public:
    ThreadOwned() = default;
    explicit ThreadOwned(T initial) : value_(std::move(initial)) {}

    T& get(ThreadTag)
    {
        jassert(detail::isCorrectThread<ThreadTag>());
        return value_;
    }

    const T& get(ThreadTag) const
    {
        jassert(detail::isCorrectThread<ThreadTag>());
        return value_;
    }

    /** Unchecked access — use only when thread safety is guaranteed by other means. */
    T& getUnchecked() { return value_; }
    const T& getUnchecked() const { return value_; }
};

} // namespace directpipe
