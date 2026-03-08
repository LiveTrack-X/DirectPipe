// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file LinuxProcessPriority.cpp
 * @brief Linux process priority implementation
 *
 * Uses setpriority() for elevated process priority.
 * Does not require root for moderate nice values (> -20).
 */

#include "../ProcessPriority.h"

#if defined(__linux__)

#include <sys/resource.h>
#include <unistd.h>

namespace directpipe {
namespace Platform {

void setHighPriority()
{
    // Elevate process priority (lower nice value = higher priority)
    // -10 is a reasonable level that doesn't require root on most distros
    setpriority(PRIO_PROCESS, 0, -10);
}

void restoreNormalPriority()
{
    setpriority(PRIO_PROCESS, 0, 0);
}

} // namespace Platform
} // namespace directpipe

#endif // defined(__linux__)
