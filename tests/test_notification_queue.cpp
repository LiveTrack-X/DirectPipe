// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file test_notification_queue.cpp
 * @brief Unit tests for the lock-free SPSC notification queue pattern
 *
 * Tests the same pattern used by AudioEngine's notification queue
 * (device thread → message thread) in isolation.
 */

#include <gtest/gtest.h>
#include <JuceHeader.h>
#include <atomic>
#include <thread>

namespace {

// ── Replicate the AudioEngine notification queue pattern ──

enum class NotifLevel { Info, Warning, Error, Critical };

struct Notification {
    juce::String message;
    NotifLevel level = NotifLevel::Error;
};

class NotificationQueue {
public:
    static constexpr int kCapacity = 8;

    bool push(const juce::String& msg, NotifLevel level) {
        uint32_t wi = writeIdx_.load(std::memory_order_relaxed);
        uint32_t ri = readIdx_.load(std::memory_order_acquire);

        // Capacity check (overflow guard)
        if (wi - ri >= kCapacity)
            return false;

        auto& slot = queue_[wi % kCapacity];
        slot.message = msg;
        slot.level = level;
        ready_[wi % kCapacity].store(true, std::memory_order_release);
        writeIdx_.store(wi + 1, std::memory_order_release);
        return true;
    }

    bool pop(Notification& out) {
        uint32_t ri = readIdx_.load(std::memory_order_relaxed);
        if (!ready_[ri % kCapacity].load(std::memory_order_acquire))
            return false;

        out = queue_[ri % kCapacity];
        ready_[ri % kCapacity].store(false, std::memory_order_release);
        readIdx_.store(ri + 1, std::memory_order_release);
        return true;
    }

    int pending() const {
        return static_cast<int>(writeIdx_.load(std::memory_order_acquire) -
                                readIdx_.load(std::memory_order_acquire));
    }

private:
    Notification queue_[kCapacity];
    std::atomic<bool> ready_[kCapacity]{};
    std::atomic<uint32_t> writeIdx_{0};
    std::atomic<uint32_t> readIdx_{0};
};

} // anonymous namespace

class NotificationQueueTest : public ::testing::Test {
protected:
    NotificationQueue queue_;
};

// ─── Basic push/pop ─────────────────────────────────────────────────────────

TEST_F(NotificationQueueTest, EmptyQueuePopReturnsFalse) {
    Notification n;
    EXPECT_FALSE(queue_.pop(n));
}

TEST_F(NotificationQueueTest, PushAndPop) {
    EXPECT_TRUE(queue_.push("test message", NotifLevel::Error));
    Notification n;
    EXPECT_TRUE(queue_.pop(n));
    EXPECT_EQ(n.message, juce::String("test message"));
    EXPECT_EQ(n.level, NotifLevel::Error);
}

TEST_F(NotificationQueueTest, FifoOrder) {
    queue_.push("first", NotifLevel::Info);
    queue_.push("second", NotifLevel::Warning);
    queue_.push("third", NotifLevel::Error);

    Notification n;
    EXPECT_TRUE(queue_.pop(n));
    EXPECT_EQ(n.message, juce::String("first"));
    EXPECT_TRUE(queue_.pop(n));
    EXPECT_EQ(n.message, juce::String("second"));
    EXPECT_TRUE(queue_.pop(n));
    EXPECT_EQ(n.message, juce::String("third"));
    EXPECT_FALSE(queue_.pop(n));
}

TEST_F(NotificationQueueTest, PendingCount) {
    EXPECT_EQ(queue_.pending(), 0);
    queue_.push("a", NotifLevel::Info);
    EXPECT_EQ(queue_.pending(), 1);
    queue_.push("b", NotifLevel::Info);
    EXPECT_EQ(queue_.pending(), 2);

    Notification n;
    queue_.pop(n);
    EXPECT_EQ(queue_.pending(), 1);
    queue_.pop(n);
    EXPECT_EQ(queue_.pending(), 0);
}

// ─── Overflow ──────────────────────────────────────────────────────────────

TEST_F(NotificationQueueTest, OverflowRejectsPush) {
    // Fill to capacity
    for (int i = 0; i < NotificationQueue::kCapacity; ++i) {
        EXPECT_TRUE(queue_.push("msg" + juce::String(i), NotifLevel::Info));
    }
    // Next push should fail (overflow guard)
    EXPECT_FALSE(queue_.push("overflow", NotifLevel::Error));
    EXPECT_EQ(queue_.pending(), NotificationQueue::kCapacity);
}

TEST_F(NotificationQueueTest, OverflowRecoveryAfterDrain) {
    // Fill
    for (int i = 0; i < NotificationQueue::kCapacity; ++i)
        queue_.push("msg", NotifLevel::Info);
    EXPECT_FALSE(queue_.push("overflow", NotifLevel::Info));

    // Drain one
    Notification n;
    queue_.pop(n);

    // Now push should succeed again
    EXPECT_TRUE(queue_.push("recovered", NotifLevel::Warning));
}

// ─── Level preservation ─────────────────────────────────────────────────────

TEST_F(NotificationQueueTest, LevelPreserved) {
    queue_.push("info", NotifLevel::Info);
    queue_.push("warn", NotifLevel::Warning);
    queue_.push("err", NotifLevel::Error);
    queue_.push("crit", NotifLevel::Critical);

    Notification n;
    queue_.pop(n); EXPECT_EQ(n.level, NotifLevel::Info);
    queue_.pop(n); EXPECT_EQ(n.level, NotifLevel::Warning);
    queue_.pop(n); EXPECT_EQ(n.level, NotifLevel::Error);
    queue_.pop(n); EXPECT_EQ(n.level, NotifLevel::Critical);
}

// ─── Wrap-around ───────────────────────────────────────────────────────────

TEST_F(NotificationQueueTest, WrapAround) {
    // Push and pop more than kCapacity total items (forces index wrap)
    for (int cycle = 0; cycle < 5; ++cycle) {
        for (int i = 0; i < NotificationQueue::kCapacity; ++i) {
            EXPECT_TRUE(queue_.push("cycle" + juce::String(cycle) + "_" + juce::String(i),
                                     NotifLevel::Info));
        }
        Notification n;
        for (int i = 0; i < NotificationQueue::kCapacity; ++i) {
            EXPECT_TRUE(queue_.pop(n));
        }
        EXPECT_FALSE(queue_.pop(n));
    }
}

// ─── Cross-thread ──────────────────────────────────────────────────────────

TEST_F(NotificationQueueTest, CrossThreadPushPop) {
    constexpr int kCount = 100;
    std::atomic<int> consumed{0};

    // Producer thread
    std::thread producer([this, kCount] {
        for (int i = 0; i < kCount; ++i) {
            while (!queue_.push("msg" + juce::String(i), NotifLevel::Info)) {
                // Spin-wait if queue full
                std::this_thread::yield();
            }
        }
    });

    // Consumer (this thread)
    while (consumed.load() < kCount) {
        Notification n;
        if (queue_.pop(n))
            consumed.fetch_add(1);
        else
            std::this_thread::yield();
    }

    producer.join();
    EXPECT_EQ(consumed.load(), kCount);
}

TEST_F(NotificationQueueTest, UnicodeMessageRoundtrip) {
    auto msg = juce::String::fromUTF8("장치 오류: 마이크 연결 끊김");
    queue_.push(msg, NotifLevel::Error);
    Notification n;
    EXPECT_TRUE(queue_.pop(n));
    EXPECT_EQ(n.message, msg);
}
