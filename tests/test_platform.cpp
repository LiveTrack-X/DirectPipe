// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "Platform/AutoStart.h"
#include "Platform/ProcessPriority.h"
#include "Platform/MultiInstanceLock.h"
#include "IPC/SharedMemWriter.h"
#include "directpipe/Protocol.h"

#if JUCE_WINDOWS
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#endif

using namespace directpipe;

class PlatformTest : public ::testing::Test {
protected:
#if JUCE_WINDOWS
    struct RegistryValueSnapshot {
        std::wstring name;
        bool existed = false;
        DWORD type = REG_NONE;
        std::vector<BYTE> data;
    };

    void SetUp() override {
        captureAutoStartRegistry();
    }

    void TearDown() override {
        restoreAutoStartRegistry();
    }

private:
    static constexpr const wchar_t* kRunKeyPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";

    void captureAutoStartRegistry() {
        autoStartSnapshots_.clear();
        for (const auto* name : {L"DirectPipe", L"DirectPipe (Portable)"}) {
            RegistryValueSnapshot snapshot;
            snapshot.name = name;

            HKEY key = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_READ, &key) == ERROR_SUCCESS) {
                DWORD size = 0;
                snapshot.existed = RegQueryValueExW(key, name, nullptr, &snapshot.type, nullptr, &size) == ERROR_SUCCESS;
                if (snapshot.existed && size > 0) {
                    snapshot.data.resize(size);
                    auto result = RegQueryValueExW(key, name, nullptr, &snapshot.type, snapshot.data.data(), &size);
                    if (result == ERROR_SUCCESS)
                        snapshot.data.resize(size);
                    else
                        snapshot.existed = false;
                }
                RegCloseKey(key);
            }

            autoStartSnapshots_.push_back(std::move(snapshot));
        }
    }

    void restoreAutoStartRegistry() {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
            return;

        for (const auto& snapshot : autoStartSnapshots_) {
            if (snapshot.existed) {
                RegSetValueExW(key, snapshot.name.c_str(), 0, snapshot.type,
                               snapshot.data.empty() ? nullptr : snapshot.data.data(),
                               static_cast<DWORD>(snapshot.data.size()));
            } else {
                RegDeleteValueW(key, snapshot.name.c_str());
            }
        }

        RegCloseKey(key);
    }

    std::vector<RegistryValueSnapshot> autoStartSnapshots_;

protected:
    static std::wstring readAutoStartCommand() {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_READ, &key) != ERROR_SUCCESS)
            return {};

        std::wstring command;
        for (const auto* name : {L"DirectPipe", L"DirectPipe (Portable)"}) {
            DWORD type = REG_NONE;
            DWORD size = 0;
            if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS
                || type != REG_SZ
                || size < sizeof(wchar_t)) {
                continue;
            }

            std::vector<wchar_t> buffer(size / sizeof(wchar_t), L'\0');
            if (RegQueryValueExW(key, name, nullptr, &type,
                                 reinterpret_cast<BYTE*>(buffer.data()), &size) == ERROR_SUCCESS) {
                command.assign(buffer.data());
                break;
            }
        }

        RegCloseKey(key);
        return command;
    }
#else
    void TearDown() override {
        Platform::setAutoStartEnabled(false);
    }
#endif
};

TEST_F(PlatformTest, AutoStartToggle) {
    if (!Platform::isAutoStartSupported()) {
        GTEST_SKIP() << "AutoStart not supported on this platform";
    }
    Platform::setAutoStartEnabled(true);
    EXPECT_TRUE(Platform::isAutoStartEnabled());
}

#if JUCE_WINDOWS
TEST_F(PlatformTest, AutoStartRunCommandQuotesExecutablePath) {
    ASSERT_TRUE(Platform::setAutoStartEnabled(true));

    auto command = readAutoStartCommand();
    ASSERT_GE(command.size(), 2u);
    EXPECT_EQ(command.front(), L'"');
    EXPECT_EQ(command.back(), L'"');
}
#endif

TEST_F(PlatformTest, AutoStartDisable) {
    if (!Platform::isAutoStartSupported()) {
        GTEST_SKIP() << "AutoStart not supported on this platform";
    }
    Platform::setAutoStartEnabled(true);
    Platform::setAutoStartEnabled(false);
    EXPECT_FALSE(Platform::isAutoStartEnabled());
}

TEST_F(PlatformTest, AutoStartSupported) {
    bool supported = Platform::isAutoStartSupported();
#if JUCE_WINDOWS || JUCE_MAC
    EXPECT_TRUE(supported);
#elif JUCE_LINUX
    (void)supported;
#endif
    SUCCEED();
}

TEST_F(PlatformTest, ProcessPriorityHigh) {
    Platform::setHighPriority();
    SUCCEED();
}

TEST_F(PlatformTest, ProcessPriorityRestore) {
    Platform::setHighPriority();
    Platform::restoreNormalPriority();
    SUCCEED();
}

TEST_F(PlatformTest, MultiInstanceAcquire) {
    int result = Platform::acquireExternalControlPriority(false);
    EXPECT_NE(result, 0);
}

TEST_F(PlatformTest, MultiInstanceAlreadyHeld) {
    int first = Platform::acquireExternalControlPriority(false);
    if (first != 1) {
        GTEST_SKIP() << "Could not acquire lock for test";
    }
    int second = Platform::acquireExternalControlPriority(false);
    (void)second;
    SUCCEED();
}

#if JUCE_WINDOWS
TEST(SharedMemWriterTest, EventCreationFailureLeavesShutdownSafe) {
    if (auto* existingEvent = OpenEventA(SYNCHRONIZE, FALSE, EVENT_NAME)) {
        CloseHandle(existingEvent);
        GTEST_SKIP() << "DirectPipe named event is already in use";
    }

    if (auto* existingMapping = OpenFileMappingA(FILE_MAP_READ, FALSE, SHM_NAME)) {
        CloseHandle(existingMapping);
        GTEST_SKIP() << "DirectPipe shared memory is already in use";
    }

    // Windows kernel objects share a namespace. A mutex with the event name
    // makes CreateEventA fail deterministically with ERROR_INVALID_HANDLE.
    auto* conflictingMutex = CreateMutexA(nullptr, FALSE, EVENT_NAME);
    ASSERT_NE(conflictingMutex, nullptr) << "CreateMutexA failed: " << GetLastError();

    const auto mappingSize = calculateSharedMemorySize(1024, 2);
    auto* retainedMapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                               0, static_cast<DWORD>(mappingSize), SHM_NAME);
    ASSERT_NE(retainedMapping, nullptr) << "CreateFileMappingA failed: " << GetLastError();
    auto* retainedView = static_cast<DirectPipeHeader*>(
        MapViewOfFile(retainedMapping, FILE_MAP_ALL_ACCESS, 0, 0, mappingSize));
    ASSERT_NE(retainedView, nullptr) << "MapViewOfFile failed: " << GetLastError();

    {
        SharedMemWriter writer;
        EXPECT_FALSE(writer.initialize(48000, 2, 1024));
        EXPECT_FALSE(writer.isConnected());

        // Holding a second mapping handle proves initialize() reached and
        // initialized the ring before the named-event failure.
        EXPECT_EQ(retainedView->sample_rate, 48000u);
        EXPECT_EQ(retainedView->channels, 2u);
        EXPECT_EQ(retainedView->buffer_frames, 1024u);
        EXPECT_FALSE(retainedView->producer_active.load(std::memory_order_acquire));

        // This used to dereference RingBuffer pointers after initialize()
        // had already unmapped their backing shared memory.
        writer.shutdown();
        EXPECT_FALSE(writer.isConnected());
    }

    UnmapViewOfFile(retainedView);
    CloseHandle(retainedMapping);
    CloseHandle(conflictingMutex);
}
#endif

#if JUCE_WINDOWS && defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
namespace {

struct SharedMemWriteBarrier {
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};

    static void wait(void* context) {
        auto& barrier = *static_cast<SharedMemWriteBarrier*>(context);
        barrier.entered.store(true, std::memory_order_release);
        while (!barrier.release.load(std::memory_order_acquire))
            std::this_thread::yield();
    }
};

template <typename Predicate>
bool spinWaitUntil(Predicate&& predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::yield();
    }
    return true;
}

} // namespace

TEST(SharedMemWriterTest, ShutdownWaitsForActiveWrite) {
    if (auto* existingEvent = OpenEventA(SYNCHRONIZE, FALSE, EVENT_NAME)) {
        CloseHandle(existingEvent);
        GTEST_SKIP() << "DirectPipe named event is already in use";
    }

    if (auto* existingMapping = OpenFileMappingA(FILE_MAP_READ, FALSE, SHM_NAME)) {
        CloseHandle(existingMapping);
        GTEST_SKIP() << "DirectPipe shared memory is already in use";
    }

    SharedMemWriter writer;
    ASSERT_TRUE(writer.initialize(48000, 2, 1024));

    SharedMemWriteBarrier barrier;
    SharedMemWriterTestAccess::setWriteBarrier(writer, &SharedMemWriteBarrier::wait, &barrier);

    juce::AudioBuffer<float> audio(2, 64);
    audio.clear();
    std::thread writeThread([&] { writer.writeAudio(audio, audio.getNumSamples()); });

    if (!spinWaitUntil([&] { return barrier.entered.load(std::memory_order_acquire); },
                       std::chrono::seconds(2))) {
        barrier.release.store(true, std::memory_order_release);
        writeThread.join();
        FAIL() << "writeAudio did not reach the test barrier";
        return;
    }

    std::atomic<bool> shutdownReturned{false};
    std::thread shutdownThread([&] {
        writer.shutdown();
        shutdownReturned.store(true, std::memory_order_release);
    });

    if (!spinWaitUntil([&] { return !writer.isConnected(); }, std::chrono::seconds(2))) {
        barrier.release.store(true, std::memory_order_release);
        writeThread.join();
        shutdownThread.join();
        FAIL() << "shutdown did not start";
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(shutdownReturned.load(std::memory_order_acquire));

    barrier.release.store(true, std::memory_order_release);
    writeThread.join();
    shutdownThread.join();

    EXPECT_TRUE(shutdownReturned.load(std::memory_order_acquire));
    EXPECT_FALSE(writer.isConnected());
}
#endif
