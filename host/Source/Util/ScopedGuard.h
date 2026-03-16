// host/Source/Util/ScopedGuard.h
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <atomic>

namespace directpipe {

/**
 * @brief RAII guard for std::atomic<bool> flags.
 *
 * Sets the flag to true on construction, false on destruction.
 * Guarantees cleanup on all exit paths (return, exception, scope exit).
 *
 * Usage:
 *   void loadPreset() {
 *       AtomicGuard guard(loadingSlot_);  // sets true
 *       // ... any early return safely resets to false
 *   }
 */
class AtomicGuard {
    std::atomic<bool>& flag_;
    bool released_ = false;

public:
    explicit AtomicGuard(std::atomic<bool>& f,
                         std::memory_order order = std::memory_order_release)
        : flag_(f)
    {
        flag_.store(true, order);
    }

    ~AtomicGuard()
    {
        if (!released_)
            flag_.store(false, std::memory_order_release);
    }

    /** Early release — flag is set to false before scope end. */
    void release()
    {
        if (!released_) {
            flag_.store(false, std::memory_order_release);
            released_ = true;
        }
    }

    AtomicGuard(const AtomicGuard&) = delete;
    AtomicGuard& operator=(const AtomicGuard&) = delete;
};

/**
 * @brief RAII guard for plain bool flags (message-thread-only usage).
 *
 * Same pattern as AtomicGuard but for non-atomic bools.
 */
class BoolGuard {
    bool& flag_;
    bool released_ = false;

public:
    explicit BoolGuard(bool& f) : flag_(f) { flag_ = true; }

    ~BoolGuard()
    {
        if (!released_)
            flag_ = false;
    }

    void release()
    {
        if (!released_) {
            flag_ = false;
            released_ = true;
        }
    }

    BoolGuard(const BoolGuard&) = delete;
    BoolGuard& operator=(const BoolGuard&) = delete;
};

} // namespace directpipe
