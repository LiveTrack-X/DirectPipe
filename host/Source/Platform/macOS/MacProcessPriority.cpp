// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file MacProcessPriority.cpp
 * @brief macOS process priority implementation
 *
 * Uses setpriority() for elevated process priority and pthread QoS
 * for real-time audio thread performance.
 */

#include "../ProcessPriority.h"

#if defined(__APPLE__)

#include <sys/resource.h>
#include <unistd.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <cerrno>
#include <JuceHeader.h>

namespace directpipe {
namespace Platform {

void setHighPriority()
{
    // Elevate process priority (lower nice value = higher priority)
    // -10 is a reasonable level that doesn't require root
    errno = 0;
    if (setpriority(PRIO_PROCESS, 0, -10) == -1 && errno != 0)
        juce::Logger::writeToLog("[APP] macOS setpriority(-10) failed: errno=" + juce::String(errno));

    // Set the current thread to user-interactive QoS for lowest latency
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
}

void restoreNormalPriority()
{
    // Restore default nice value
    errno = 0;
    if (setpriority(PRIO_PROCESS, 0, 0) == -1 && errno != 0)
        juce::Logger::writeToLog("[APP] macOS setpriority(0) failed: errno=" + juce::String(errno));
}

} // namespace Platform
} // namespace directpipe

#endif // defined(__APPLE__)
