// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#pragma once

#include <algorithm>
#include <cmath>

namespace directpipe::monitor_drift {

// These are safety bounds, not fixed latency policy. The target fill is derived
// from the runtime producer/consumer blocks and then slowly adapted.
constexpr double kNormalPllPitchToleranceCents = 4.0;
constexpr double kEmergencyPllPitchToleranceCents = 12.0;
constexpr double kPllSettlingTargetWindows = 4.0;

enum class TargetReason {
    RuntimeMinimum = 0,
    UnderrunRaised,
    StableReduced,
    CapacityLimited
};

struct AdaptiveTargetState {
    int extraLatencyFrames = 0;
    int stableCallbacks = 0;
    int underrunEvents = 0;
    int nearOverflowEvents = 0;
    int lastProducerBlock = 0;
    int lastConsumerBlock = 0;
    TargetReason reason = TargetReason::RuntimeMinimum;
};

struct BufferPlan {
    int targetFill = 0;
    int minimumTargetFill = 0;
    int maximumTargetFill = 0;
    int highThreshold = 0;
    int emergencyThreshold = 0;
    int producerBlock = 0;
    int consumerBlock = 0;
    int baseBlock = 0;
    int stabilityWindowCallbacks = 0;
    TargetReason reason = TargetReason::RuntimeMinimum;
};

struct TrimPlan {
    int targetFill = 0;
    int highThreshold = 0;
    int trimFrames = 0;
};

struct PllState {
    double correction = 0.0;
    double driftEstimateFramesPerCallback = 0.0;
    double previousErrorFrames = 0.0;
    bool initialized = false;
};

struct PllUpdate {
    double playbackRatio = 1.0;
    double correction = 0.0;
    double errorFrames = 0.0;
    double errorMs = 0.0;
    double driftEstimate = 0.0;
    double ratioLimit = 0.0;
};

inline int positiveOrFallback(int value, int fallback)
{
    return value > 0 ? value : (std::max)(1, fallback);
}

inline double positiveOrFallback(double value, double fallback)
{
    return value > 0.0 ? value : (std::max)(1.0, fallback);
}

inline int clampInt(int value, int lo, int hi)
{
    if (hi < lo)
        return lo;
    return (std::min)((std::max)(value, lo), hi);
}

inline double clampDouble(double value, double lo, double hi)
{
    return (std::min)((std::max)(value, lo), hi);
}

inline double ratioDeltaFromCents(double cents)
{
    return std::pow(2.0, cents / 1200.0) - 1.0;
}

inline const char* targetReasonToString(TargetReason reason)
{
    switch (reason) {
        case TargetReason::RuntimeMinimum: return "runtime-minimum";
        case TargetReason::UnderrunRaised: return "underrun-raised";
        case TargetReason::StableReduced: return "stable-reduced";
        case TargetReason::CapacityLimited: return "capacity-limited";
    }
    return "unknown";
}

inline BufferPlan calculateBufferPlan(int capacityFrames,
                                      int producerBlockFrames,
                                      int consumerBlockFrames,
                                      double sampleRate,
                                      const AdaptiveTargetState& state = {})
{
    if (capacityFrames <= 0)
        return {};

    const int capacity = (std::max)(1, capacityFrames);
    const int consumerBlock = positiveOrFallback(consumerBlockFrames, 1);
    const int producerBlock = positiveOrFallback(producerBlockFrames, consumerBlock);
    const int baseBlock = (std::max)(producerBlock, consumerBlock);

    // Runtime minimum: one producer burst plus one monitor callback worth of
    // scheduling margin. This follows the actual callback sizes instead of a
    // fixed frame-count latency target.
    int minimumTarget = producerBlock + consumerBlock;
    minimumTarget = clampInt(minimumTarget, consumerBlock, (std::max)(1, capacity / 2));

    const int runtimeHeadroom = (std::max)(baseBlock, consumerBlock);
    const int maximumTarget = clampInt(minimumTarget + runtimeHeadroom * 2,
                                       minimumTarget,
                                       (std::max)(minimumTarget, capacity / 2));

    TargetReason reason = state.reason;
    int target = minimumTarget + (std::max)(0, state.extraLatencyFrames);
    if (target > maximumTarget) {
        target = maximumTarget;
        reason = TargetReason::CapacityLimited;
    } else if (state.extraLatencyFrames <= 0) {
        reason = TargetReason::RuntimeMinimum;
    }

    const int highThreshold = clampInt(target + baseBlock,
                                       target + 1,
                                       (std::max)(target + 1, capacity - 1));
    const int nearOverflowHeadroom = (std::max)(baseBlock + consumerBlock, consumerBlock * 2);
    const int emergencyThreshold = clampInt(capacity - nearOverflowHeadroom,
                                            highThreshold + 1,
                                            (std::max)(highThreshold + 1, capacity - 1));

    const double sr = positiveOrFallback(sampleRate, 48000.0);
    const int stabilityWindow = (std::max)(1, static_cast<int>(std::ceil(sr / static_cast<double>(consumerBlock))));

    return { target, minimumTarget, maximumTarget, highThreshold, emergencyThreshold,
             producerBlock, consumerBlock, baseBlock, stabilityWindow, reason };
}

inline void noteUnderrun(AdaptiveTargetState& state, const BufferPlan& plan)
{
    state.extraLatencyFrames = clampInt(state.extraLatencyFrames + plan.consumerBlock,
                                        0,
                                        (std::max)(0, plan.maximumTargetFill - plan.minimumTargetFill));
    state.stableCallbacks = 0;
    ++state.underrunEvents;
    state.reason = TargetReason::UnderrunRaised;
}

inline void noteNearOverflow(AdaptiveTargetState& state)
{
    state.stableCallbacks = 0;
    ++state.nearOverflowEvents;
}

inline void noteStableCallback(AdaptiveTargetState& state, const BufferPlan& plan, int errorFrames)
{
    if (std::abs(errorFrames) > plan.consumerBlock) {
        state.stableCallbacks = 0;
        return;
    }

    ++state.stableCallbacks;
    if (state.stableCallbacks < plan.stabilityWindowCallbacks)
        return;

    state.stableCallbacks = 0;
    if (state.extraLatencyFrames > 0) {
        const int reduction = (std::max)(1, plan.consumerBlock / 4);
        state.extraLatencyFrames = (std::max)(0, state.extraLatencyFrames - reduction);
        state.reason = state.extraLatencyFrames > 0 ? TargetReason::StableReduced
                                                    : TargetReason::RuntimeMinimum;
    }
}

inline TrimPlan calculateEmergencyTrimPlan(int availableFrames, const BufferPlan& plan)
{
    if (availableFrames <= plan.emergencyThreshold)
        return { plan.targetFill, plan.emergencyThreshold, 0 };

    const int requestedTrim = (std::max)(0, availableFrames - plan.targetFill);
    const int trimLimit = (std::max)(1, plan.consumerBlock / 2);
    return { plan.targetFill, plan.emergencyThreshold, (std::min)(requestedTrim, trimLimit) };
}

inline TrimPlan calculateTrimPlan(int availableFrames,
                                  int capacityFrames,
                                  int producerBlockFrames,
                                  int consumerBlockFrames)
{
    const auto plan = calculateBufferPlan(capacityFrames,
                                          producerBlockFrames,
                                          consumerBlockFrames,
                                          48000.0);
    return calculateEmergencyTrimPlan(availableFrames, plan);
}

inline PllUpdate updatePll(PllState& state,
                           double errorFrames,
                           int targetFillFrames,
                           int consumerBlockFrames,
                           double sampleRate,
                           bool emergencyCorrection)
{
    const int consumerBlock = positiveOrFallback(consumerBlockFrames, 1);
    const double sr = positiveOrFallback(sampleRate, 48000.0);
    const double callbackSeconds = static_cast<double>(consumerBlock) / sr;
    const double targetSeconds = (std::max)(static_cast<double>((std::max)(1, targetFillFrames)) / sr,
                                           callbackSeconds);
    const double alpha = callbackSeconds / (targetSeconds + callbackSeconds);

    const double deltaFrames = state.initialized ? (errorFrames - state.previousErrorFrames) : 0.0;
    state.driftEstimateFramesPerCallback += alpha * (deltaFrames - state.driftEstimateFramesPerCallback);
    state.previousErrorFrames = errorFrames;
    state.initialized = true;

    const double settleSeconds = targetSeconds * kPllSettlingTargetWindows;
    const double errorSeconds = errorFrames / sr;
    const double proportional = errorSeconds / (std::max)(callbackSeconds, settleSeconds);
    const double driftFeedForward = state.driftEstimateFramesPerCallback / static_cast<double>(consumerBlock);
    const double desiredCorrection = proportional + driftFeedForward;

    const double limit = ratioDeltaFromCents(emergencyCorrection
        ? kEmergencyPllPitchToleranceCents
        : kNormalPllPitchToleranceCents);
    const double clamped = clampDouble(desiredCorrection, -limit, limit);
    state.correction += alpha * (clamped - state.correction);

    return {
        1.0 + state.correction,
        state.correction,
        errorFrames,
        errorSeconds * 1000.0,
        state.driftEstimateFramesPerCallback,
        limit
    };
}

inline void resetPll(PllState& state)
{
    state = {};
}

} // namespace directpipe::monitor_drift
