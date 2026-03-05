// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file Log.h
 * @brief Structured logging helpers with severity, category, audit mode, and timing.
 *
 * Usage:
 *   Log::info("AUDIO", "Device started: " + name);
 *   Log::warn("AUDIO", "Device lost: " + name);
 *   Log::error("AUDIO", "Failed to init '" + name + "': " + err);
 *   Log::audit("VST", "createPluginInstance desc=" + desc.name + " sr=" + String(sr));
 *
 *   { Log::Timer t("VST", "Chain swap"); ... }  // logs duration on scope exit
 *
 * Audit mode:
 *   Log::setAuditMode(true);   // enable verbose logging
 *   Log::audit("AUDIO", ...);  // only logged when audit mode is ON
 */
#pragma once

#include <JuceHeader.h>
#include <atomic>

namespace directpipe {
namespace Log {

// ─── Audit mode (verbose debug logging) ─────────────────────────────────────

/** Global audit mode flag. When false, audit() calls are no-ops. */
inline std::atomic<bool>& auditEnabled()
{
    static std::atomic<bool> enabled{false};
    return enabled;
}

inline void setAuditMode(bool enable) { auditEnabled().store(enable); }
inline bool isAuditMode() { return auditEnabled().load(); }

// ─── Severity-tagged logging ────────────────────────────────────────────────

inline void info(const char* cat, const juce::String& msg)
{
    juce::Logger::writeToLog("INF [" + juce::String(cat) + "] " + msg);
}

inline void warn(const char* cat, const juce::String& msg)
{
    juce::Logger::writeToLog("WRN [" + juce::String(cat) + "] " + msg);
}

inline void error(const char* cat, const juce::String& msg)
{
    juce::Logger::writeToLog("ERR [" + juce::String(cat) + "] " + msg);
}

/**
 * @brief Audit-level log — only written when audit mode is enabled.
 *
 * Use for verbose internal details: function entry/exit, parameter values,
 * thread IDs, lock acquisitions, cache states, etc.
 * Adds zero overhead when audit mode is off (atomic check only).
 */
inline void audit(const char* cat, const juce::String& msg)
{
    if (!auditEnabled().load(std::memory_order_relaxed)) return;
    juce::Logger::writeToLog("AUD [" + juce::String(cat) + "] " + msg);
}

// ─── Scoped timer ───────────────────────────────────────────────────────────

/**
 * @brief RAII timer that logs elapsed time on scope exit.
 *
 * Usage:
 *   { Log::Timer t("VST", "Cached chain swap: 3 plugins"); ... }
 *   // → "INF [VST] Cached chain swap: 3 plugins (15ms)"
 */
struct Timer {
    const char* category;
    juce::String operation;
    juce::int64 startMs;

    Timer(const char* cat, const juce::String& op)
        : category(cat), operation(op),
          startMs(juce::Time::getMillisecondCounter()) {}

    ~Timer()
    {
        auto elapsed = juce::Time::getMillisecondCounter() - startMs;
        info(category, operation + " (" + juce::String(elapsed) + "ms)");
    }

    juce::int64 elapsedMs() const
    {
        return juce::Time::getMillisecondCounter() - startMs;
    }

    JUCE_DECLARE_NON_COPYABLE(Timer)
};

// ─── Session header ─────────────────────────────────────────────────────────

/** Log full session header (version, OS, process info). Call once at startup. */
void sessionStart(const juce::String& version);

/** Log audio configuration. Call after audio device is initialized. */
void audioConfig(const juce::String& driverType,
                 const juce::String& inputDevice,
                 const juce::String& outputDevice,
                 double sampleRate, int bufferSize);

/** Log monitor configuration. */
void monitorConfig(const juce::String& deviceName, double sampleRate, int bufferSize);

/** Log plugin chain state. */
void pluginChain(const juce::StringArray& pluginNames, int activeSlot, const juce::String& presetName);

/** Log session end (call in shutdown). */
void sessionEnd(juce::int64 sessionStartTimeMs);

} // namespace Log
} // namespace directpipe
