// tests/test_settings_autosaver.cpp
#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "Control/SettingsAutosaver.h"
#include "UI/PresetManager.h"
#include "Audio/AudioEngine.h"

using namespace directpipe;

class SettingsAutosaverTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Enable portable mode BEFORE constructing any objects
        auto exeDir = juce::File::getSpecialLocation(
            juce::File::currentExecutableFile).getParentDirectory();
        portableFlag_ = exeDir.getChildFile("portable.flag");
        portableFlag_.create();

        configDir_ = exeDir.getChildFile("config");
        configDir_.createDirectory();

        engine_ = std::make_unique<AudioEngine>();
        presetMgr_ = std::make_unique<PresetManager>(*engine_);
        autosaver_ = std::make_unique<SettingsAutosaver>(
            *presetMgr_, *engine_, loadingSlot_, partialLoad_);
    }

    void TearDown() override {
        autosaver_.reset();
        presetMgr_.reset();
        engine_.reset();
        configDir_.deleteRecursively();
        portableFlag_.deleteFile();
    }

    juce::File getAutoSaveFile() {
        return PresetManager::getAutoSaveFile();
    }

    juce::File portableFlag_;
    juce::File configDir_;
    std::atomic<bool> loadingSlot_{false};
    std::atomic<bool> partialLoad_{false};
    std::unique_ptr<AudioEngine> engine_;
    std::unique_ptr<PresetManager> presetMgr_;
    std::unique_ptr<SettingsAutosaver> autosaver_;
};

TEST_F(SettingsAutosaverTest, DirtyFlagReset) {
    auto file = getAutoSaveFile();
    file.deleteFile();

    autosaver_->markDirty();
    for (int i = 0; i < 30; ++i) autosaver_->tick();

    EXPECT_TRUE(file.existsAsFile());
}

TEST_F(SettingsAutosaverTest, CooldownResetOnReDirty) {
    auto file = getAutoSaveFile();
    file.deleteFile();

    autosaver_->markDirty();
    for (int i = 0; i < 15; ++i) autosaver_->tick();
    EXPECT_FALSE(file.existsAsFile());

    autosaver_->markDirty();
    for (int i = 0; i < 15; ++i) autosaver_->tick();
    EXPECT_FALSE(file.existsAsFile());

    for (int i = 0; i < 15; ++i) autosaver_->tick();
    EXPECT_TRUE(file.existsAsFile());
}

TEST_F(SettingsAutosaverTest, DeferDuringLoading) {
    auto file = getAutoSaveFile();
    file.deleteFile();

    loadingSlot_ = true;
    autosaver_->markDirty();

    for (int i = 0; i < 40; ++i) autosaver_->tick();
    EXPECT_FALSE(file.existsAsFile());

    loadingSlot_ = false;
    autosaver_->markDirty();
    for (int i = 0; i < 30; ++i) autosaver_->tick();
    EXPECT_TRUE(file.existsAsFile());
}

TEST_F(SettingsAutosaverTest, ForceAfterMaxDefer) {
    auto file = getAutoSaveFile();
    file.deleteFile();

    loadingSlot_ = true;
    autosaver_->markDirty();

    for (int i = 0; i < 600; ++i) autosaver_->tick();

    loadingSlot_ = false;
    autosaver_->markDirty();
    for (int i = 0; i < 30; ++i) autosaver_->tick();
    EXPECT_TRUE(file.existsAsFile());
}

TEST_F(SettingsAutosaverTest, SaveNowSkipsDuringLoading) {
    auto file = getAutoSaveFile();
    file.deleteFile();

    loadingSlot_ = true;
    autosaver_->saveNow();
    EXPECT_FALSE(file.existsAsFile());
}

TEST_F(SettingsAutosaverTest, DebounceTiming) {
    auto file = getAutoSaveFile();
    file.deleteFile();

    autosaver_->markDirty();

    for (int i = 0; i < 29; ++i) autosaver_->tick();
    EXPECT_FALSE(file.existsAsFile());

    autosaver_->tick();
    EXPECT_TRUE(file.existsAsFile());
}

TEST_F(SettingsAutosaverTest, DeferAndForce) {
    auto file = getAutoSaveFile();
    file.deleteFile();

    loadingSlot_ = true;
    autosaver_->markDirty();

    for (int i = 0; i < 30; ++i) autosaver_->tick();
    EXPECT_FALSE(file.existsAsFile());

    for (int i = 0; i < 50 * 10; ++i) autosaver_->tick();

    loadingSlot_ = false;
    autosaver_->markDirty();
    for (int i = 0; i < 30; ++i) autosaver_->tick();
    EXPECT_TRUE(file.existsAsFile());
}
