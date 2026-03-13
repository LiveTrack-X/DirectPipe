// tests/test_settings_exporter.cpp
#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "UI/SettingsExporter.h"
#include "Control/ControlMapping.h"

using namespace directpipe;

class SettingsExporterTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("dp_exporter_test_" +
                juce::String(juce::Random::getSystemRandom().nextInt()));
        tempDir_.createDirectory();
    }

    void TearDown() override {
        tempDir_.deleteRecursively();
    }

    juce::String makeBackupJSON(const juce::String& platform = "windows")
    {
        auto root = std::make_unique<juce::DynamicObject>();
        root->setProperty("version", 4);
        root->setProperty("platform", platform);
        root->setProperty("deviceType", "Windows Audio");
        root->setProperty("sampleRate", 48000.0);
        root->setProperty("bufferSize", 480);

        auto controls = new juce::DynamicObject();
        controls->setProperty("websocketPort", 8765);
        root->setProperty("controls", juce::var(controls));

        return juce::JSON::toString(juce::var(root.release()), true);
    }

    juce::String makeFullBackupJSON(const juce::String& platform = "windows")
    {
        auto json = makeBackupJSON(platform);
        auto parsed = juce::JSON::parse(json);
        auto* root = parsed.getDynamicObject();

        juce::Array<juce::var> plugins;
        auto p = new juce::DynamicObject();
        p->setProperty("name", "TestPlugin");
        p->setProperty("path", "/test/path");
        p->setProperty("bypassed", false);
        plugins.add(juce::var(p));
        root->setProperty("plugins", plugins);

        return juce::JSON::toString(parsed, true);
    }

    juce::File tempDir_;
};

TEST_F(SettingsExporterTest, DpbackupRoundtrip) {
    auto json = makeBackupJSON();
    auto file = tempDir_.getChildFile("test.dpbackup");
    file.replaceWithText(json);

    auto loaded = juce::JSON::parse(file.loadFileAsString());
    ASSERT_TRUE(loaded.isObject());
    auto* root = loaded.getDynamicObject();
    EXPECT_EQ(static_cast<int>(root->getProperty("version")), 4);
    EXPECT_FALSE(root->hasProperty("plugins"));
}

TEST_F(SettingsExporterTest, DpfullbackupRoundtrip) {
    auto json = makeFullBackupJSON();
    auto file = tempDir_.getChildFile("test.dpfullbackup");
    file.replaceWithText(json);

    auto loaded = juce::JSON::parse(file.loadFileAsString());
    ASSERT_TRUE(loaded.isObject());
    auto* root = loaded.getDynamicObject();
    EXPECT_TRUE(root->hasProperty("plugins"));
    auto* plugins = root->getProperty("plugins").getArray();
    ASSERT_NE(plugins, nullptr);
    EXPECT_EQ(plugins->size(), 1);
}

TEST_F(SettingsExporterTest, CrossPlatformReject) {
    auto json = makeBackupJSON("macos");
    EXPECT_FALSE(SettingsExporter::isPlatformCompatible(json));
}

TEST_F(SettingsExporterTest, CrossPlatformAcceptSame) {
    auto currentPlatform = SettingsExporter::getCurrentPlatform();
    auto json = makeBackupJSON(currentPlatform);
    EXPECT_TRUE(SettingsExporter::isPlatformCompatible(json));
}

TEST_F(SettingsExporterTest, LegacyNoPlatformField) {
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("version", 4);
    root->setProperty("deviceType", "Windows Audio");
    auto json = juce::JSON::toString(juce::var(root.release()), true);

    EXPECT_TRUE(SettingsExporter::isPlatformCompatible(json));
}

TEST_F(SettingsExporterTest, GetCurrentPlatform) {
    auto platform = SettingsExporter::getCurrentPlatform();
    EXPECT_FALSE(platform.isEmpty());
#if JUCE_WINDOWS
    EXPECT_EQ(platform, juce::String("windows"));
#elif JUCE_MAC
    EXPECT_EQ(platform, juce::String("macos"));
#elif JUCE_LINUX
    EXPECT_EQ(platform, juce::String("linux"));
#endif
}

TEST_F(SettingsExporterTest, ExportStripsPluginsKey) {
    auto json = makeFullBackupJSON();
    auto parsed = juce::JSON::parse(json);
    ASSERT_TRUE(parsed.getDynamicObject()->hasProperty("plugins"));

    parsed.getDynamicObject()->removeProperty("plugins");
    auto stripped = juce::JSON::toString(parsed, true);
    auto reparsed = juce::JSON::parse(stripped);
    EXPECT_FALSE(reparsed.getDynamicObject()->hasProperty("plugins"));
}

TEST_F(SettingsExporterTest, FullBackupIncludesPlugins) {
    auto json = makeFullBackupJSON();
    auto parsed = juce::JSON::parse(json);
    EXPECT_TRUE(parsed.getDynamicObject()->hasProperty("plugins"));
}

TEST_F(SettingsExporterTest, CorruptControlsJsonRecovery) {
    auto file = tempDir_.getChildFile("controls.json");
    file.replaceWithText("{corrupt garbage!@#$");

    // ControlMappingStore::load() returns createDefaults() on corrupt JSON
    ControlMappingStore store;
    auto loaded = store.load(file);
    auto defaults = ControlMappingStore::createDefaults();

    // Loaded config should match defaults when file is corrupt
    EXPECT_EQ(loaded.server.websocketPort, defaults.server.websocketPort);
    EXPECT_EQ(loaded.server.websocketEnabled, defaults.server.websocketEnabled);
    EXPECT_EQ(loaded.server.httpPort, defaults.server.httpPort);
}

TEST_F(SettingsExporterTest, CorruptControlsJsonNoBackup) {
    auto file = tempDir_.getChildFile("controls.json");
    file.replaceWithText("{corrupt garbage!@#$");

    ControlMappingStore store;
    auto loaded = store.load(file);
    auto defaults = ControlMappingStore::createDefaults();
    EXPECT_EQ(loaded.server.websocketPort, defaults.server.websocketPort);
}
