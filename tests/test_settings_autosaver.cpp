// tests/test_settings_autosaver.cpp
#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "Control/SettingsAutosaver.h"
#include "UI/PresetManager.h"
#include "Audio/AudioEngine.h"

#if JUCE_WINDOWS
#include <windows.h>
#endif

using namespace directpipe;

namespace {

class CapturingLogger final : public juce::Logger {
public:
    void logMessage(const juce::String& message) override
    {
        messages.add(message);
    }

    int countContaining(const juce::String& text) const
    {
        int count = 0;
        for (const auto& message : messages)
            if (message.contains(text))
                ++count;
        return count;
    }

private:
    juce::StringArray messages;
};

class ScopedCurrentLogger final {
public:
    explicit ScopedCurrentLogger(juce::Logger& logger)
        : previous_(juce::Logger::getCurrentLogger())
    {
        juce::Logger::setCurrentLogger(&logger);
    }

    ~ScopedCurrentLogger()
    {
        juce::Logger::setCurrentLogger(previous_);
    }

private:
    juce::Logger* previous_;
};

} // namespace

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
        // Prepare VSTChain so isStable() returns true (mirrors real runtime state)
        engine_->getVSTChain().prepareToPlay(48000.0, 512);
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

TEST_F(SettingsAutosaverTest, DeferredAutosaveRemainsPendingAfterMaxDefer) {
    auto file = getAutoSaveFile();
    file.deleteFile();

    loadingSlot_ = true;
    autosaver_->markDirty();

    // Initial debounce plus all prolonged-defer retries.
    for (int i = 0; i < 600; ++i) autosaver_->tick();
    EXPECT_FALSE(file.existsAsFile());

    // Stabilizing the chain must be enough. The caller should not have to
    // generate another setting change to recover the discarded dirty flag.
    loadingSlot_ = false;
    for (int i = 0; i < 10; ++i) autosaver_->tick();

    EXPECT_TRUE(file.existsAsFile());
}

TEST_F(SettingsAutosaverTest, SaveNowSkipsDuringLoading) {
    auto file = getAutoSaveFile();
    const juce::String original =
        R"({"version":4,"deviceType":"Windows Audio","plugins":[{"name":"LoadingPlugin"}]})";
    ASSERT_TRUE(file.replaceWithText(original));

    loadingSlot_ = true;
    EXPECT_FALSE(autosaver_->saveNow());
    EXPECT_EQ(file.loadFileAsString(), original);
}

TEST_F(SettingsAutosaverTest, ShutdownFlushMergesSettingsWithoutReplacingCompletePlugins) {
    auto file = getAutoSaveFile();
    const juce::String original = R"json({
        "version": 4,
        "deviceType": "Windows Audio",
        "inputDevice": "Old Input",
        "outputDevice": "Old Output",
        "plugins": [
            {
                "name": "PreservedPlugin",
                "path": "C:/plugins/preserved.vst3",
                "bypassed": false
            }
        ]
    })json";
    ASSERT_TRUE(file.replaceWithText(original));

    presetMgr_->onExportAppSettings = [](juce::DynamicObject& root) {
        root.setProperty("startMinimizedToTray", true);
    };
    engine_->rememberRestoredDeviceTargets(
        "ASIO", "Recovery ASIO", "Recovery ASIO");
    engine_->setInputGain(0.75f);
    loadingSlot_ = true;

    ASSERT_TRUE(autosaver_->flushForShutdown());

    auto saved = juce::JSON::parse(file.loadFileAsString());
    auto* root = saved.getDynamicObject();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->getProperty("deviceType").toString(), "ASIO");
    EXPECT_EQ(root->getProperty("inputDevice").toString(), "Recovery ASIO");
    EXPECT_EQ(root->getProperty("outputDevice").toString(), "Recovery ASIO");
    EXPECT_DOUBLE_EQ(static_cast<double>(root->getProperty("inputGain")), 0.75);
    EXPECT_TRUE(static_cast<bool>(
        root->getProperty("startMinimizedToTray")));

    auto* plugins = root->getProperty("plugins").getArray();
    ASSERT_NE(plugins, nullptr);
    ASSERT_EQ(plugins->size(), 1);
    auto* plugin = plugins->getReference(0).getDynamicObject();
    ASSERT_NE(plugin, nullptr);
    EXPECT_EQ(plugin->getProperty("name").toString(), "PreservedPlugin");
    EXPECT_EQ(plugin->getProperty("path").toString(),
              "C:/plugins/preserved.vst3");
    EXPECT_FALSE(SettingsAutosaver::getShutdownRecoveryFile().existsAsFile());
}

TEST_F(SettingsAutosaverTest, ShutdownFlushUsesSidecarUntilCompleteChainReturns) {
    auto file = getAutoSaveFile();
    file.deleteFile();
    file.getSiblingFile(file.getFileName() + ".bak").deleteFile();
    file.withFileExtension(file.getFileExtension() + ".backup").deleteFile();
    auto recoveryFile = SettingsAutosaver::getShutdownRecoveryFile();
    recoveryFile.deleteFile();

    presetMgr_->onExportAppSettings = [](juce::DynamicObject& root) {
        root.setProperty("startMinimizedToTray", true);
    };
    engine_->rememberRestoredDeviceTargets(
        "ASIO", "Recovery ASIO", "Recovery ASIO");
    loadingSlot_ = true;

    ASSERT_TRUE(autosaver_->flushForShutdown());
    EXPECT_FALSE(file.existsAsFile());
    ASSERT_TRUE(recoveryFile.existsAsFile());

    auto pending = juce::JSON::parse(recoveryFile.loadFileAsString());
    auto* pendingRoot = pending.getDynamicObject();
    ASSERT_NE(pendingRoot, nullptr);
    EXPECT_FALSE(pendingRoot->hasProperty("plugins"));
    EXPECT_EQ(pendingRoot->getProperty("deviceType").toString(), "ASIO");
    EXPECT_TRUE(static_cast<bool>(
        pendingRoot->getProperty("startMinimizedToTray")));

    ASSERT_TRUE(file.replaceWithText(R"json({
        "version": 4,
        "deviceType": "Windows Audio",
        "plugins": [
            {
                "name": "RecoveredPlugin",
                "path": "C:/plugins/recovered.vst3",
                "bypassed": false
            }
        ]
    })json"));

    bool restoredStartMinimized = false;
    presetMgr_->onImportAppSettings =
        [&restoredStartMinimized](const juce::DynamicObject& root) {
            restoredStartMinimized =
                root.hasProperty("startMinimizedToTray")
                && static_cast<bool>(
                    root.getProperty("startMinimizedToTray"));
            return true;
        };
    loadingSlot_ = false;

    autosaver_->loadFromFile();

    EXPECT_TRUE(restoredStartMinimized);
    EXPECT_FALSE(recoveryFile.existsAsFile());
    auto merged = juce::JSON::parse(file.loadFileAsString());
    auto* mergedRoot = merged.getDynamicObject();
    ASSERT_NE(mergedRoot, nullptr);
    auto* plugins = mergedRoot->getProperty("plugins").getArray();
    ASSERT_NE(plugins, nullptr);
    ASSERT_EQ(plugins->size(), 1);
    EXPECT_EQ(plugins->getReference(0).getDynamicObject()
                  ->getProperty("name").toString(),
              "RecoveredPlugin");
    EXPECT_TRUE(static_cast<bool>(
        mergedRoot->getProperty("startMinimizedToTray")));
}

TEST_F(SettingsAutosaverTest, ShutdownSidecarRestoresImmediatelyWithoutMainPreset) {
    auto file = getAutoSaveFile();
    file.deleteFile();
    file.getSiblingFile(file.getFileName() + ".bak").deleteFile();
    file.withFileExtension(file.getFileExtension() + ".backup").deleteFile();
    auto recoveryFile = SettingsAutosaver::getShutdownRecoveryFile();
    recoveryFile.deleteFile();

    presetMgr_->onExportAppSettings = [](juce::DynamicObject& root) {
        root.setProperty("startMinimizedToTray", true);
    };
    loadingSlot_ = true;
    ASSERT_TRUE(autosaver_->flushForShutdown());
    ASSERT_FALSE(file.existsAsFile());
    ASSERT_TRUE(recoveryFile.existsAsFile());

    bool restoredStartMinimized = false;
    presetMgr_->onImportAppSettings =
        [&restoredStartMinimized](const juce::DynamicObject& root) {
            restoredStartMinimized =
                root.hasProperty("startMinimizedToTray")
                && static_cast<bool>(
                    root.getProperty("startMinimizedToTray"));
            return true;
        };
    loadingSlot_ = false;

    autosaver_->loadFromFile();

    EXPECT_TRUE(restoredStartMinimized);
    EXPECT_FALSE(partialLoad_.load());
    EXPECT_FALSE(file.existsAsFile());
    EXPECT_TRUE(recoveryFile.existsAsFile());
}

TEST_F(SettingsAutosaverTest, RecoverySidecarSurvivesLockedMainMergeFailure) {
#if JUCE_WINDOWS
    auto file = getAutoSaveFile();
    ASSERT_TRUE(file.replaceWithText(R"json({
        "version": 4,
        "plugins": [
            {
                "name": "PreservedPlugin",
                "path": "C:/plugins/preserved.vst3"
            }
        ]
    })json"));
    auto recoveryFile = SettingsAutosaver::getShutdownRecoveryFile();
    ASSERT_TRUE(recoveryFile.replaceWithText(R"json({
        "version": 4,
        "startMinimizedToTray": true
    })json"));

    bool restoredStartMinimized = false;
    presetMgr_->onImportAppSettings =
        [&restoredStartMinimized](const juce::DynamicObject& root) {
            restoredStartMinimized =
                root.hasProperty("startMinimizedToTray")
                && static_cast<bool>(
                    root.getProperty("startMinimizedToTray"));
            return true;
        };

    auto lockedPrimary = CreateFileW(
        file.getFullPathName().toWideCharPointer(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    ASSERT_NE(lockedPrimary, INVALID_HANDLE_VALUE) << GetLastError();

    autosaver_->loadFromFile();

    EXPECT_TRUE(restoredStartMinimized);
    EXPECT_TRUE(recoveryFile.existsAsFile());
    EXPECT_FALSE(file.loadFileAsString().contains(
        "startMinimizedToTray"));
    CloseHandle(lockedPrimary);
#else
    GTEST_SKIP() << "Windows file-share semantics are required";
#endif
}

TEST_F(SettingsAutosaverTest, FactoryResetDeletesShutdownRecoveryFamily) {
    const auto recoveryFile =
        SettingsAutosaver::getShutdownRecoveryFile();
    const juce::File recoveryFamily[] {
        recoveryFile,
        recoveryFile.getSiblingFile(
            recoveryFile.getFileName() + ".bak"),
        recoveryFile.withFileExtension(
            recoveryFile.getFileExtension() + ".backup"),
        recoveryFile.getSiblingFile(
            recoveryFile.getFileName() + ".tmp")
    };
    for (const auto& file : recoveryFamily)
        ASSERT_TRUE(file.replaceWithText("{}"));

    ASSERT_TRUE(SettingsAutosaver::deleteShutdownRecoveryFiles());

    for (const auto& file : recoveryFamily)
        EXPECT_FALSE(file.existsAsFile());
}

TEST_F(SettingsAutosaverTest, SaveNowPreservesSettingsFileDuringPartialLoad) {
    auto file = getAutoSaveFile();
    const juce::String original = R"json({
        "version": 4,
        "deviceType": "Windows Audio",
        "inputDevice": "Old Input",
        "outputDevice": "CABLE Input(VB-Audio Virtual Cable)",
        "plugins": [
            {
                "name": "MissingPlugin",
                "path": "C:/missing/plugin.vst3",
                "bypassed": false
            }
        ]
    })json";
    ASSERT_TRUE(file.replaceWithText(original));

    const auto slotFile = PresetManager::getSlotFile(presetMgr_->getActiveSlot());
    const juce::String originalSlot =
        R"({"version":4,"type":"chain","plugins":[{"name":"SlotPlugin"}]})";
    ASSERT_TRUE(slotFile.replaceWithText(originalSlot));

    engine_->rememberRestoredDeviceTargets(
        "ASIO", "Recovery ASIO", "Recovery ASIO");
    partialLoad_ = true;
    ASSERT_TRUE(autosaver_->saveNow());

    auto saved = juce::JSON::parse(file.loadFileAsString());
    auto* root = saved.getDynamicObject();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->getProperty("deviceType").toString(), "ASIO");
    EXPECT_EQ(root->getProperty("inputDevice").toString(), "Recovery ASIO");
    EXPECT_EQ(root->getProperty("outputDevice").toString(), "Recovery ASIO");

    auto* plugins = root->getProperty("plugins").getArray();
    ASSERT_NE(plugins, nullptr);
    ASSERT_EQ(plugins->size(), 1);
    auto* plugin = plugins->getReference(0).getDynamicObject();
    ASSERT_NE(plugin, nullptr);
    EXPECT_EQ(plugin->getProperty("name").toString(), "MissingPlugin");
    EXPECT_EQ(plugin->getProperty("path").toString(), "C:/missing/plugin.vst3");
    EXPECT_FALSE(static_cast<bool>(plugin->getProperty("bypassed")));
    EXPECT_EQ(slotFile.loadFileAsString(), originalSlot);
}

TEST_F(SettingsAutosaverTest, PartialLoadAutosavePersistsDeviceSettingsWithoutReplacingPlugins) {
    auto file = getAutoSaveFile();
    ASSERT_TRUE(file.replaceWithText(R"({
        "version": 4,
        "deviceType": "Windows Audio",
        "plugins": [{"name":"MissingPlugin","path":"C:/missing/plugin.vst3"}]
    })"));

    engine_->rememberRestoredDeviceTargets(
        "ASIO", "Recovery ASIO", "Recovery ASIO");
    partialLoad_ = true;
    autosaver_->markDirty();
    for (int i = 0; i < 30; ++i)
        autosaver_->tick();

    auto saved = juce::JSON::parse(file.loadFileAsString());
    auto* root = saved.getDynamicObject();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->getProperty("deviceType").toString(), "ASIO");
    auto* plugins = root->getProperty("plugins").getArray();
    ASSERT_NE(plugins, nullptr);
    ASSERT_EQ(plugins->size(), 1);
    auto* plugin = plugins->getReference(0).getDynamicObject();
    ASSERT_NE(plugin, nullptr);
    EXPECT_EQ(plugin->getProperty("name").toString(), "MissingPlugin");
}

TEST_F(SettingsAutosaverTest, PartialLoadSaveUsesCompleteBackupPluginChain) {
    auto file = getAutoSaveFile();
    auto backup = file.getSiblingFile(file.getFileName() + ".bak");
    ASSERT_TRUE(file.replaceWithText(
        R"({"version":4,"deviceType":"Windows Audio"})"));
    ASSERT_TRUE(backup.replaceWithText(R"({
        "version": 4,
        "deviceType": "Windows Audio",
        "plugins": [{"name":"BackupPlugin","path":"C:/backup/plugin.vst3"}]
    })"));

    engine_->rememberRestoredDeviceTargets(
        "ASIO", "Recovery ASIO", "Recovery ASIO");
    partialLoad_ = true;
    ASSERT_TRUE(autosaver_->saveNow());

    auto saved = juce::JSON::parse(file.loadFileAsString());
    auto* root = saved.getDynamicObject();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->getProperty("deviceType").toString(), "ASIO");
    auto* plugins = root->getProperty("plugins").getArray();
    ASSERT_NE(plugins, nullptr);
    ASSERT_EQ(plugins->size(), 1);
    auto* plugin = plugins->getReference(0).getDynamicObject();
    ASSERT_NE(plugin, nullptr);
    EXPECT_EQ(plugin->getProperty("name").toString(), "BackupPlugin");
}

TEST_F(SettingsAutosaverTest, MissingCompleteChainRetriesQuietlyWithBoundedBackoff) {
    auto file = getAutoSaveFile();
    file.deleteFile();
    file.getSiblingFile(file.getFileName() + ".bak").deleteFile();
    file.withFileExtension(file.getFileExtension() + ".backup").deleteFile();

    engine_->rememberRestoredDeviceTargets(
        "ASIO", "Recovery ASIO", "Recovery ASIO");
    partialLoad_ = true;

    CapturingLogger logger;
    ScopedCurrentLogger loggerScope(logger);

    autosaver_->markDirty();
    for (int i = 0; i < 30; ++i)
        autosaver_->tick();

    EXPECT_EQ(logger.countContaining(
                  "complete persisted plugin chain unavailable"),
              1);
    EXPECT_EQ(logger.countContaining(
                  "Autosave failed; keeping changes pending"),
              0);

    // Exercise every backoff step while no complete chain is available:
    // 5s, 10s, 20s, then the bounded 30s maximum twice.
    for (int ticks : { 150, 300, 600, 900 })
        for (int i = 0; i < ticks; ++i)
            autosaver_->tick();

    EXPECT_EQ(logger.countContaining(
                  "complete persisted plugin chain unavailable"),
              1);

    ASSERT_TRUE(file.replaceWithText(R"({
        "version": 4,
        "deviceType": "Windows Audio",
        "plugins": [{"name":"RecoveredPlugin","path":"C:/recovered/plugin.vst3"}]
    })"));

    for (int i = 0; i < 899; ++i)
        autosaver_->tick();

    auto beforeRetry = juce::JSON::parse(file.loadFileAsString());
    ASSERT_NE(beforeRetry.getDynamicObject(), nullptr);
    EXPECT_EQ(beforeRetry.getDynamicObject()
                  ->getProperty("deviceType").toString(),
              "Windows Audio");

    autosaver_->tick();

    auto afterRetry = juce::JSON::parse(file.loadFileAsString());
    auto* root = afterRetry.getDynamicObject();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->getProperty("deviceType").toString(), "ASIO");
    auto* plugins = root->getProperty("plugins").getArray();
    ASSERT_NE(plugins, nullptr);
    ASSERT_EQ(plugins->size(), 1);
    auto* plugin = plugins->getReference(0).getDynamicObject();
    ASSERT_NE(plugin, nullptr);
    EXPECT_EQ(plugin->getProperty("name").toString(), "RecoveredPlugin");
    EXPECT_EQ(logger.countContaining(
                  "complete persisted plugin chain unavailable"),
              1);
}

TEST_F(SettingsAutosaverTest, FailedAutosaveRemainsDirtyAndRetriesWithoutAnotherEdit) {
    auto file = getAutoSaveFile();
    file.deleteFile();

    auto blockedTemp = file.getSiblingFile(file.getFileName() + ".tmp");
    ASSERT_TRUE(blockedTemp.createDirectory());
    ASSERT_TRUE(blockedTemp.getChildFile("write-blocker").replaceWithText("blocked"));

    autosaver_->markDirty();
    for (int i = 0; i < 30; ++i) autosaver_->tick();
    EXPECT_FALSE(file.existsAsFile());

    ASSERT_TRUE(blockedTemp.deleteRecursively());
    for (int i = 0; i < 10; ++i) autosaver_->tick();
    EXPECT_TRUE(file.existsAsFile());
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

TEST_F(SettingsAutosaverTest, PresetManagerRoundtripSupportsCustomAppSettings) {
    auto file = getAutoSaveFile();
    file.deleteFile();

    bool exportHookCalled = false;
    bool importHookCalled = false;
    bool restoredStartMinimizedToTray = false;

    presetMgr_->onExportAppSettings = [&](juce::DynamicObject& root) {
        exportHookCalled = true;
        root.setProperty("startMinimizedToTray", true);
    };
    presetMgr_->onImportAppSettings = [&](const juce::DynamicObject& root) {
        importHookCalled = true;
        if (root.hasProperty("startMinimizedToTray"))
            restoredStartMinimizedToTray = static_cast<bool>(root.getProperty("startMinimizedToTray"));
        return true;
    };

    autosaver_->saveNow();
    EXPECT_TRUE(file.existsAsFile());
    EXPECT_TRUE(exportHookCalled);

    autosaver_->loadFromFile();
    EXPECT_TRUE(importHookCalled);
    EXPECT_TRUE(restoredStartMinimizedToTray);
}

TEST_F(SettingsAutosaverTest, LoadFromFileMarksPartialLoadWhenPresetChainIncomplete) {
    auto file = getAutoSaveFile();
    file.replaceWithText(R"({
        "version": 4,
        "activeSlot": 0,
        "plugins": [
            {
                "name": "MissingPlugin",
                "path": "/tmp/directpipe-missing-plugin.vst3",
                "bypassed": false
            }
        ],
        "outputMuted": false
    })");

    partialLoad_ = false;
    autosaver_->loadFromFile();

    EXPECT_TRUE(partialLoad_.load());
    EXPECT_FALSE(loadingSlot_.load());
    EXPECT_EQ(engine_->getVSTChain().getPluginCount(), 0);
}

TEST_F(SettingsAutosaverTest, StartupGuardRestoresMuteWhenNoSettingsFile) {
    auto file = getAutoSaveFile();
    file.deleteFile();

    engine_->setOutputMuted(false);
    autosaver_->loadFromFile();

    EXPECT_FALSE(engine_->isOutputManuallyMuted());
}

TEST_F(SettingsAutosaverTest, NoSettingsFileAlwaysReleasesFactoryResetLoadingState) {
    auto file = getAutoSaveFile();
    file.deleteFile();

    loadingSlot_ = true;
    partialLoad_ = true;
    autosaver_->loadFromFile();

    EXPECT_FALSE(loadingSlot_.load());
    EXPECT_FALSE(partialLoad_.load());
}

TEST_F(SettingsAutosaverTest, StartupGuardRestoresMuteForLegacyPresetWithoutOutputMuted) {
    auto file = getAutoSaveFile();
    file.replaceWithText(R"({
        "version": 4,
        "deviceType": "Windows Audio",
        "sampleRate": 48000.0,
        "bufferSize": 512
    })");

    engine_->setOutputMuted(false);
    autosaver_->loadFromFile();

    EXPECT_FALSE(engine_->isOutputManuallyMuted());
}

TEST_F(SettingsAutosaverTest, StartupGuardKeepsExplicitOutputMutedFromPreset) {
    auto file = getAutoSaveFile();
    file.replaceWithText(R"({
        "version": 4,
        "outputMuted": true
    })");

    engine_->setOutputMuted(false);
    autosaver_->loadFromFile();

    EXPECT_TRUE(engine_->isOutputManuallyMuted());

    // Atomic writes may be interrupted after primary -> .bak rotation. The
    // backup-only family must still restore settings and explicit mute intent.
    auto backup = file.getSiblingFile(file.getFileName() + ".bak");
    ASSERT_TRUE(backup.replaceWithText(R"({
        "version": 4,
        "outputMuted": true
    })"));
    ASSERT_TRUE(file.deleteFile());
    engine_->setOutputMuted(false);

    autosaver_->loadFromFile();

    EXPECT_TRUE(engine_->isOutputManuallyMuted());

#if JUCE_WINDOWS
    // When a corrupt primary cannot be repaired because another process has
    // it open, the loaded backup JSON remains the authority for safety fields.
    ASSERT_TRUE(file.replaceWithText("{corrupt"));
    ASSERT_TRUE(backup.replaceWithText(R"({
        "version": 4,
        "outputMuted": true
    })"));
    auto lockedPrimary = CreateFileW(file.getFullPathName().toWideCharPointer(),
                                     GENERIC_READ,
                                     FILE_SHARE_READ,
                                     nullptr,
                                     OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL,
                                     nullptr);
    ASSERT_NE(lockedPrimary, INVALID_HANDLE_VALUE) << GetLastError();
    engine_->setOutputMuted(false);

    autosaver_->loadFromFile();

    EXPECT_TRUE(engine_->isOutputManuallyMuted());
    CloseHandle(lockedPrimary);
#endif
}

TEST_F(SettingsAutosaverTest, PartialLoadKeepsExplicitOutputMutedFromPreset) {
    auto file = getAutoSaveFile();
    file.replaceWithText(R"({
        "version": 4,
        "plugins": [
            {
                "name": "MissingPlugin",
                "path": "/tmp/directpipe-missing-plugin.vst3",
                "bypassed": false
            }
        ],
        "outputMuted": true
    })");

    engine_->setOutputMuted(false);
    autosaver_->loadFromFile();

    EXPECT_TRUE(partialLoad_.load());
    EXPECT_TRUE(engine_->isOutputManuallyMuted());
}
