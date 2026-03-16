// host/Source/Util/StrongIndex.h
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

namespace directpipe {

/**
 * @brief Strong type wrapper for indices to prevent argument mix-ups.
 *
 * Wrapping int indices in distinct types turns accidental parameter swaps
 * into compile errors:
 *
 *   void movePlugin(PluginIndex from, PluginIndex to);
 *   vstChain.movePlugin(PluginIndex{rowIndex}, PluginIndex{fromIndex});
 *   // Swapping the two arguments still compiles (same type), but
 *   // mixing PluginIndex with SlotIndex does NOT compile.
 *
 * Zero-cost: same layout as int, no overhead in release builds.
 */
template<typename Tag>
struct StrongIndex {
    int value;
    explicit constexpr StrongIndex(int v) : value(v) {}

    constexpr bool operator==(StrongIndex other) const { return value == other.value; }
    constexpr bool operator!=(StrongIndex other) const { return value != other.value; }
    constexpr bool operator<(StrongIndex other) const { return value < other.value; }
    constexpr bool operator>=(StrongIndex other) const { return value >= other.value; }
};

// ─── Concrete index types ─────────────────────────────────────────
// Each tag struct creates a distinct type that won't implicitly convert.

struct PluginIndexTag {};
struct SlotIndexTag {};
struct MappingIndexTag {};

/** Index into VSTChain's plugin array (0-based). */
using PluginIndex = StrongIndex<PluginIndexTag>;

/** Index into preset slot buttons A-E (0-4). */
using SlotIndex = StrongIndex<SlotIndexTag>;

/** Index into MidiHandler's binding list. */
using MappingIndex = StrongIndex<MappingIndexTag>;

} // namespace directpipe
