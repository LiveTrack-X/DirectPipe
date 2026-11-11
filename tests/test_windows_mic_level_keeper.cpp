// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 LiveTrack
#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "Platform/WindowsMicLevelKeeper.h"

#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <chrono>
#include <utility>
#include <vector>

namespace {
using Keeper = directpipe::WindowsMicLevelKeeper;
using Access = directpipe::WindowsMicLevelKeeperTestAccess;

struct FakeMicState {
    Keeper::Resolution resolution{Keeper::State::ready, {"capture-a", "Selected microphone"}, {}};
    Keeper::Backend::Callback callback;
    std::function<Keeper::Resolution(int)> resolver;
    std::function<void()> beforeWrite;
    juce::String attached;
    float scalar = 0.45f;
    int resolutions = 0, attaches = 0, reads = 0, attempts = 0, detaches = 0;
    bool failRead = false, failWrite = false;
    std::vector<std::pair<juce::String, float>> writes;
};

// No Windows API, native audio device, default endpoint, mute or gain control.
class FakeMicBackend final : public Keeper::Backend {
public:
    explicit FakeMicBackend(std::shared_ptr<FakeMicState> state) : state_(std::move(state)) {}
    Keeper::Resolution resolve(const Keeper::InputContext&) override
    {
        ++state_->resolutions;
        return state_->resolver ? state_->resolver(state_->resolutions) : state_->resolution;
    }
    bool attach(const juce::String& id, Callback callback, juce::String& error) override
    {
        ++state_->attaches; state_->attached = id; state_->callback = std::move(callback);
        error.clear(); return true;
    }
    bool readScalar(float& value, juce::String& error) override
    {
        ++state_->reads;
        if (state_->failRead) { error = "Owned fake read failure"; return false; }
        value = state_->scalar; error.clear(); return true;
    }
    bool setScalar100(juce::String& error) override
    {
        ++state_->attempts;
        if (state_->beforeWrite) state_->beforeWrite();
        if (state_->failWrite) { error = "Owned fake write failure"; return false; }
        state_->writes.emplace_back(state_->attached, 1.0f);
        state_->scalar = 1.0f;
        if (state_->callback) state_->callback(true, false);
        error.clear(); return true;
    }
    void detach() noexcept override
    {
        ++state_->detaches; state_->callback = {}; state_->attached.clear();
    }
private:
    std::shared_ptr<FakeMicState> state_;
};

class WindowsMicLevelKeeperTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        juce::MessageManager::getInstance();
        state = std::make_shared<FakeMicState>();
        keeper = std::make_unique<Keeper>(std::make_unique<FakeMicBackend>(state), false);
    }
    void drain(juce::int64 time) { Access::drain(*keeper, time); }
    void setConsent()
    {
        Keeper::Preferences preferences; preferences.enabledEndpointIds.add("capture-a");
        juce::String error;
        ASSERT_TRUE(keeper->setPreferences(preferences, error)) << error;
    }
    void arm()
    {
        setConsent(); keeper->setStartupReady(true); keeper->updateInputContext(context);
        drain(0); drain(100);
    }
    std::shared_ptr<FakeMicState> state;
    std::unique_ptr<Keeper> keeper;
    Keeper::InputContext context{"Windows Audio", "Selected microphone", true, "capture-a"};
};

TEST_F(WindowsMicLevelKeeperTest, DefaultOffNeverWritesAnActiveMicrophone)
{
    keeper->setStartupReady(true); keeper->updateInputContext(context);
    drain(0); drain(1000);
    EXPECT_TRUE(keeper->getPreferences().enabledEndpointIds.isEmpty());
    EXPECT_FALSE(keeper->getStatus().enabled);
    EXPECT_TRUE(state->writes.empty());
}

TEST_F(WindowsMicLevelKeeperTest, RestoredConsentCannotWriteBeforeStableStartup)
{
    setConsent(); keeper->updateInputContext(context);
    drain(0); drain(1000);
    EXPECT_TRUE(state->writes.empty());
    keeper->setStartupReady(true); drain(1100); drain(1200);
    ASSERT_EQ(1u, state->writes.size());
    EXPECT_EQ(juce::String("capture-a"), state->writes[0].first);
    EXPECT_FLOAT_EQ(1.0f, state->writes[0].second);
    EXPECT_EQ(Keeper::State::holding, keeper->getStatus().state);
}

TEST_F(WindowsMicLevelKeeperTest, ExplicitEnableAndDisableNeverRestoresAStaleLevel)
{
    keeper->setStartupReady(true); keeper->updateInputContext(context); drain(0);
    juce::String error;
    ASSERT_TRUE(keeper->setEnabledForCurrentEndpoint(true, error)) << error;
    drain(100); drain(200);
    ASSERT_EQ(1u, state->writes.size());
    EXPECT_TRUE(keeper->getPreferences().enabledEndpointIds.contains("capture-a"));
    ASSERT_TRUE(keeper->setEnabledForCurrentEndpoint(false, error)) << error;
    state->scalar = 0.3f;
    if (state->callback) state->callback(false, false);
    drain(500); drain(2000);
    EXPECT_EQ(1u, state->writes.size());
    EXPECT_FLOAT_EQ(0.3f, state->scalar);
    EXPECT_TRUE(keeper->getPreferences().enabledEndpointIds.isEmpty());
}

TEST_F(WindowsMicLevelKeeperTest, ExternalChangesAreDebouncedAndRateBounded)
{
    arm(); ASSERT_EQ(1u, state->writes.size());
    ASSERT_TRUE(static_cast<bool>(state->callback));
    state->scalar = 0.25f;
    for (int i = 0; i < 100; ++i) state->callback(false, false);
    drain(150); drain(200); drain(349);
    EXPECT_EQ(1u, state->writes.size());
    drain(350);
    EXPECT_EQ(2u, state->writes.size());
    EXPECT_FLOAT_EQ(1.0f, state->scalar);
}

TEST_F(WindowsMicLevelKeeperTest, SelfNotificationsAndAlreadyMaximumDoNotLoop)
{
    state->scalar = 1.0f;
    arm(); EXPECT_TRUE(state->writes.empty());
    ASSERT_TRUE(static_cast<bool>(state->callback));
    for (int i = 0; i < 100; ++i) state->callback(true, false);
    drain(200); drain(1000);
    EXPECT_TRUE(state->writes.empty());
}

TEST_F(WindowsMicLevelKeeperTest, SwitchingToUnconsentedEndpointStopsOldBinding)
{
    arm(); ASSERT_EQ(1u, state->writes.size());
    const auto stale = state->callback;
    state->resolution.endpoint = {"capture-b", "Selected microphone"}; state->scalar = 0.2f;
    context.endpointId = "capture-b";
    keeper->updateInputContext(context); drain(400);
    if (stale) stale(false, false);
    drain(1000);
    EXPECT_EQ(1u, state->writes.size());
    EXPECT_FALSE(keeper->getStatus().enabled);
    EXPECT_EQ(juce::String("capture-b"), keeper->getStatus().endpointId);
    EXPECT_TRUE(keeper->getPreferences().enabledEndpointIds.contains("capture-a"));
}

TEST_F(WindowsMicLevelKeeperTest, RevalidationRejectsAChangedEndpointBeforeWriting)
{
    state->resolver = [](int count) {
        return Keeper::Resolution{Keeper::State::ready,
            {count == 1 ? "capture-a" : "capture-b", "Owned fake endpoint"}, {}};
    };
    arm(); drain(1000);
    EXPECT_TRUE(state->writes.empty());
    EXPECT_FALSE(keeper->getStatus().state == Keeper::State::holding);
}

TEST_F(WindowsMicLevelKeeperTest, AmbiguousCaptureNeverFallsBackToADefaultOrName)
{
    state->resolution = {Keeper::State::unavailable, {}, "Multiple active capture endpoints are ambiguous"};
    arm(); drain(1000);
    EXPECT_TRUE(state->writes.empty()); EXPECT_EQ(0, state->attaches);
    EXPECT_FALSE(keeper->getStatus().canEnable);
    EXPECT_TRUE(keeper->getStatus().message.containsIgnoreCase("ambiguous"));
}

TEST_F(WindowsMicLevelKeeperTest, MissingExactEndpointCannotFallBackToAFriendlyName)
{
    context.endpointId.clear();
    arm(); drain(1000);
    EXPECT_TRUE(state->writes.empty());
    EXPECT_EQ(0, state->resolutions);
    EXPECT_EQ(0, state->attaches);
    EXPECT_FALSE(keeper->getStatus().canEnable);
}

TEST_F(WindowsMicLevelKeeperTest, AsioAndExclusiveCannotReachEndpointWrites)
{
    setConsent(); keeper->setStartupReady(true);
    for (const auto* type : {"ASIO", "Windows Audio (Exclusive Mode)", "Unknown"}) {
        context.deviceType = type; keeper->updateInputContext(context); drain(1000);
        EXPECT_FALSE(keeper->getStatus().canEnable);
        EXPECT_EQ(Keeper::State::unsupported, keeper->getStatus().state);
    }
    EXPECT_TRUE(state->writes.empty()); EXPECT_EQ(0, state->attaches);
}

TEST_F(WindowsMicLevelKeeperTest, SharedLowLatencyUsesTheSameExplicitConsent)
{
    context.deviceType = "Windows Audio (Low Latency Mode)";
    arm(); ASSERT_EQ(1u, state->writes.size());
    EXPECT_EQ(juce::String("capture-a"), state->writes[0].first);
}

TEST_F(WindowsMicLevelKeeperTest, DisconnectStopsCorrectionsAndAllowsTurningOff)
{
    arm(); ASSERT_EQ(1u, state->writes.size());
    const auto notification = state->callback;
    ASSERT_TRUE(static_cast<bool>(notification)); notification(false, true);
    state->scalar = 0.2f; drain(500); drain(10000);
    EXPECT_EQ(1u, state->writes.size());
    EXPECT_FALSE(keeper->getStatus().state == Keeper::State::holding);
    juce::String error;
    EXPECT_TRUE(keeper->setEnabledForCurrentEndpoint(false, error)) << error;
    EXPECT_TRUE(keeper->getPreferences().enabledEndpointIds.isEmpty());
}

TEST_F(WindowsMicLevelKeeperTest, ReadAndWriteFailuresStopWithoutRetryHammering)
{
    state->failRead = true; arm(); drain(10000);
    EXPECT_TRUE(state->writes.empty()); EXPECT_EQ(Keeper::State::error, keeper->getStatus().state);
    EXPECT_TRUE(keeper->getPreferences().enabledEndpointIds.contains("capture-a"));
    state->failRead = false;
    drain(20000); EXPECT_TRUE(state->writes.empty());
    juce::String error;
    ASSERT_TRUE(keeper->setEnabledForCurrentEndpoint(false, error));
    ASSERT_TRUE(keeper->setEnabledForCurrentEndpoint(true, error));
    state->failWrite = true; drain(21000); drain(22000); drain(100000);
    EXPECT_EQ(1, state->attempts); EXPECT_TRUE(state->writes.empty());
    EXPECT_EQ(Keeper::State::error, keeper->getStatus().state);
}

TEST_F(WindowsMicLevelKeeperTest, SuspensionAndShutdownDiscardLateNotifications)
{
    arm(); ASSERT_EQ(1u, state->writes.size());
    const auto late = state->callback;
    keeper->setStartupReady(false); state->scalar = 0.4f;
    if (late) late(false, false);
    drain(1000); EXPECT_EQ(1u, state->writes.size());
    keeper->stop(); keeper.reset();
    if (late) late(false, false);
    EXPECT_EQ(1u, state->writes.size());
}

TEST(WindowsMicLevelKeeperPreferencesTest, StrictRoundTripPreservesOpaqueEndpointIds)
{
    Keeper::Preferences before; before.enabledEndpointIds.add("{capture-a}"); before.enabledEndpointIds.add("capture-b");
    Keeper::Preferences after; juce::String error;
    ASSERT_TRUE(Keeper::parsePreferences(Keeper::serializePreferences(before), after, error)) << error;
    EXPECT_EQ(before.enabledEndpointIds, after.enabledEndpointIds);
}

TEST(WindowsMicLevelKeeperPreferencesTest, MalformedPreferencesLeaveOutputUntouched)
{
    const auto object = [](juce::var version, juce::var ids) {
        auto value = std::make_unique<juce::DynamicObject>();
        value->setProperty("version", version); value->setProperty("enabledEndpointIds", ids);
        return juce::var(value.release());
    };
    const juce::Array<juce::var> valid{juce::var("capture-a")};
    juce::Array<juce::var> tooMany;
    for (int i = 0; i < 129; ++i) tooMany.add("capture-" + juce::String(i));
    const std::vector<juce::var> invalid{
        {}, "not object", object(2, valid), object(1, "not array"),
        object(1, juce::Array<juce::var>{""}), object(1, juce::Array<juce::var>{7}),
        object(1, juce::Array<juce::var>{"capture-a", "capture-a"}), object(1, tooMany),
        object(1, juce::Array<juce::var>{juce::String::repeatedString("x", 1025)}),
        object(1, juce::Array<juce::var>{"capture\nother"})};
    for (const auto& value : invalid) {
        Keeper::Preferences output; output.enabledEndpointIds.add("already opted-in"); juce::String error;
        EXPECT_FALSE(Keeper::parsePreferences(value, output, error));
        EXPECT_EQ(juce::StringArray{"already opted-in"}, output.enabledEndpointIds);
    }
}

TEST_F(WindowsMicLevelKeeperTest, InvalidProgrammaticPreferencesCannotArm)
{
    Keeper::Preferences invalid; invalid.enabledEndpointIds.add("capture-a"); invalid.enabledEndpointIds.add("capture-a");
    juce::String error;
    EXPECT_FALSE(keeper->setPreferences(invalid, error));
    EXPECT_TRUE(keeper->getPreferences().enabledEndpointIds.isEmpty());
    EXPECT_TRUE(state->writes.empty());
}

TEST_F(WindowsMicLevelKeeperTest, TurningOffWaitsForAnAdmittedFakeWriteAndPreventsLaterWrites)
{
    setConsent(); keeper->setStartupReady(true); keeper->updateInputContext(context); drain(0);
    std::promise<void> entered, release;
    auto enteredFuture = entered.get_future();
    auto releaseFuture = release.get_future().share();
    state->beforeWrite = [&] { entered.set_value(); releaseFuture.wait(); };
    std::thread worker([&] { drain(100); }); // Only the injected fake backend.
    const bool admitted = enteredFuture.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    std::promise<void> disableStarted;
    auto disableStartedFuture = disableStarted.get_future();
    auto disabled = std::async(std::launch::async, [&] {
        disableStarted.set_value();
        juce::String error; return keeper->setEnabledForCurrentEndpoint(false, error);
    });
    const bool disabling = disableStartedFuture.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    const auto beforeRelease = disabled.wait_for(std::chrono::milliseconds(30));
    release.set_value(); worker.join();
    const bool disabledOk = disabled.get();
    ASSERT_TRUE(admitted);
    EXPECT_TRUE(disabling);
    EXPECT_EQ(std::future_status::timeout, beforeRelease);
    ASSERT_TRUE(disabledOk);
    ASSERT_EQ(1u, state->writes.size());
    state->beforeWrite = {}; state->scalar = 0.2f;
    drain(1000); drain(10000);
    EXPECT_EQ(1u, state->writes.size());
    EXPECT_FALSE(keeper->getStatus().enabled);
}
} // namespace
