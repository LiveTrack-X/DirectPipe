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

namespace directpipe {
namespace Platform {

void setHighPriority()
{
    // Elevate process priority (lower nice value = higher priority)
    // -10 is a reasonable level that doesn't require root
    setpriority(PRIO_PROCESS, 0, -10);

    // Set the current thread to user-interactive QoS for lowest latency
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
}

void restoreNormalPriority()
{
    // Restore default nice value
    setpriority(PRIO_PROCESS, 0, 0);
}

} // namespace Platform
} // namespace directpipe

#endif // defined(__APPLE__)
