// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "Platform/AutoStart.h"
#include "Platform/ProcessPriority.h"
#include "Platform/MultiInstanceLock.h"

#if JUCE_WINDOWS
#include <Windows.h>
#include <string>
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
