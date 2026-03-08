// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file ProcessPriority.h
 * @brief Platform-specific process priority and timer resolution
 *
 * Windows:  HIGH_PRIORITY_CLASS + timeBeginPeriod(1) + Power Throttling disable
 * macOS:    setpriority + pthread QoS (USER_INTERACTIVE)
 * Linux:    setpriority (nice -10)
 */
#pragma once

namespace directpipe {
namespace Platform {

/**
 * @brief Elevate process priority for real-time audio processing.
 *
 * Windows: SetPriorityClass(HIGH_PRIORITY_CLASS), timeBeginPeriod(1),
 *          disable Power Throttling for Intel hybrid CPUs.
 */
void setHighPriority();

/**
 * @brief Restore normal process priority and clean up.
 *
 * Windows: timeEndPeriod(1).
 */
void restoreNormalPriority();

} // namespace Platform
} // namespace directpipe
