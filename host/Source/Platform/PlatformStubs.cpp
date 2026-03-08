// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file PlatformStubs.cpp
 * @brief Stub implementations for non-Windows platforms
 *
 * Provides no-op/default implementations of Platform services.
 * macOS/Linux-specific implementations will replace these stubs
 * when those platforms are fully supported.
 */

#if !defined(_WIN32)

#include "AutoStart.h"
#include "ProcessPriority.h"
#include "MultiInstanceLock.h"

namespace directpipe {
namespace Platform {

// ─── AutoStart stubs ─────────────────────────────────────────────

bool isAutoStartEnabled() { return false; }
void setAutoStartEnabled(bool /*enable*/) {}
bool isAutoStartSupported() { return false; }

// ─── ProcessPriority stubs ───────────────────────────────────────

void setHighPriority() {}
void restoreNormalPriority() {}

// ─── MultiInstanceLock stubs ─────────────────────────────────────

int acquireExternalControlPriority(bool /*isPortable*/) { return 1; }
void releaseExternalControlPriority() {}

} // namespace Platform
} // namespace directpipe

#endif // !defined(_WIN32)
