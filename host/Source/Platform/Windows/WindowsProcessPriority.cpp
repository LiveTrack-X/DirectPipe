// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file WindowsProcessPriority.cpp
 * @brief Windows process priority, timer resolution, and power throttling
 */

#include "../ProcessPriority.h"

#if defined(_WIN32)

#include <Windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

namespace directpipe {
namespace Platform {

void setHighPriority()
{
    // Elevate process priority class
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    // Reduce Windows timer granularity to 1ms
    timeBeginPeriod(1);

    // Disable Power Throttling (prevents Intel E-core scheduling on hybrid CPUs)
    PROCESS_POWER_THROTTLING_STATE throttling{};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttling.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling,
                          &throttling, sizeof(throttling));
}

void restoreNormalPriority()
{
    timeEndPeriod(1);
}

} // namespace Platform
} // namespace directpipe

#endif // defined(_WIN32)
