// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#pragma once

#include <algorithm>

namespace directpipe::monitor_drift {

constexpr int kMinTargetFrames = 128;

struct TrimPlan {
    int targetFill = 0;
    int highThreshold = 0;
    int trimFrames = 0;
};

inline TrimPlan calculateTrimPlan(int availableFrames,
                                  int capacityFrames,
                                  int producerBlockFrames,
                                  int consumerBlockFrames)
{
    if (availableFrames <= 0 || capacityFrames <= 0)
        return {};

    const int capacity = (std::max)(1, capacityFrames);
    const int producerBlock = (std::max)(kMinTargetFrames, producerBlockFrames);
    const int consumerBlock = (std::max)(kMinTargetFrames, consumerBlockFrames);
    const int baseBlock = (std::max)(producerBlock, consumerBlock);

    // Keep enough queued audio for one bursty producer write plus several
    // monitor callbacks. This avoids trimming below the main callback
    // granularity when the monitor device uses a smaller buffer.
    const int requestedTarget = (std::max)(producerBlock * 2, consumerBlock * 4);
    const int targetFill = (std::min)((std::max)(kMinTargetFrames, requestedTarget),
                                      (std::max)(1, capacity / 2));

    const int maxThreshold = (std::max)(targetFill + 1, capacity - 1);
    const int highThreshold = (std::min)((std::max)(targetFill + baseBlock, targetFill + 1),
                                         maxThreshold);
    const int trimFrames = (availableFrames > highThreshold) ? availableFrames - targetFill : 0;

    return { targetFill, highThreshold, trimFrames };
}

} // namespace directpipe::monitor_drift
