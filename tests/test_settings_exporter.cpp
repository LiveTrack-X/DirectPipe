// tests/test_settings_exporter.cpp
#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "UI/SettingsExporter.h"
#include "Control/ControlMapping.h"
#include "Util/AtomicFileIO.h"

using namespace directpipe;

namespace {

juce::var makeAudioSettings(int activeSlot = -1)
{
    auto audio = new juce::DynamicObject();
    audio->setProperty("version", 4);
    audio->setProperty("sampleRate", 48000.0);
    audio->setProperty("bufferSize", 512);
    if (activeSlot >= -1)
        audio->setProperty("activeSlot", activeSlot);

    juce::Array<juce::var> plugins;
    auto plugin = new juce::DynamicObject();
    plugin->setProperty("name", "ShouldNotImport");
    plugin->setProperty("path", "/tmp/should-not-import.vst3");
    plugin->setProperty("bypassed", false);
    plugins.add(juce::var(plugin));
    audio->setProperty("plugins", plugins);

    return juce::var(audio);
}

juce::var makeSlot(const juce::String& name)
{
    auto slot = new juce::DynamicObject();
    slot->setProperty("version", 4);
    slot->setProperty("name", name);

    juce::Array<juce::var> plugins;
    auto plugin = new juce::DynamicObject();
    plugin->setProperty("name", name + "_Plugin");
    plugin->setProperty("path", "/tmp/" + name + ".vst3");
    plugin->setProperty("bypassed", false);
    plugins.add(juce::var(plugin));
    slot->setProperty("plugins", plugins);

    return juce::var(slot);
}

juce::String makeExporterBackupJSON(const juce::var& audioSettings)
{
    auto root = new juce::DynamicObject();
    root->setProperty("version", 2);
    root->setProperty("platform", SettingsExporter::getCurrentPlatform());
    root->setProperty("audioSettings", audioSettings);
    return juce::JSON::toString(juce::var(root), true);
}

juce::String makeFullRestoreJSON(const juce::var& presetSlots)
{
    auto root = new juce::DynamicObject();
    root->setProperty("version", 2);
    root->setProperty("type", "full");
    root->setProperty("platform", SettingsExporter::getCurrentPlatform());
    root->setProperty("audioSettings", makeAudioSettings(-1));
    root->setProperty("presetSlots", presetSlots);
    return juce::JSON::toString(juce::var(root), true);
}

} // namespace

class SettingsExporterTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("dp_exporter_test_" +
                juce::String(juce::Random::getSystemRandom().nextInt()));
        tempDir_.createDirectory();

        auto exeDir = juce::File::getSpecialLocation(
            juce::File::currentExecutableFile).getParentDirectory();
        portableFlag_ = exeDir.getChildFile("portable.flag");
        hadPortableFlag_ = portableFlag_.existsAsFile();
        if (!hadPortableFlag_)
            portableFlag_.create();

        configDir_ = exeDir.getChildFile("config");
        configDir_.deleteRecursively();
        configDir_.createDirectory();
    }

    void TearDown() override {
        tempDir_.deleteRecursively();
        configDir_.deleteRecursively();
        if (!hadPortableFlag_)
            portableFlag_.deleteFile();
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
    juce::File portableFlag_;
    juce::File configDir_;
    bool hadPortableFlag_ = false;
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
    // Use a platform that is NOT the current one
    auto current = SettingsExporter::getCurrentPlatform();
    juce::String other = (current == "macos") ? "windows" : "macos";
    auto json = makeBackupJSON(other);
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

TEST_F(SettingsExporterTest, ExportAllOmitsActiveSlotAndPlugins) {
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    presetManager.setActiveSlot(2);
    auto json = SettingsExporter::exportAll(presetManager, controlStore);
    auto parsed = juce::JSON::parse(json);
    ASSERT_TRUE(parsed.isObject());

    auto* root = parsed.getDynamicObject();
    auto* audio = root->getProperty("audioSettings").getDynamicObject();
    ASSERT_NE(audio, nullptr);
    EXPECT_FALSE(audio->hasProperty("activeSlot"));
    EXPECT_FALSE(audio->hasProperty("plugins"));
}

TEST_F(SettingsExporterTest, ImportAllPreservesCurrentActiveSlot) {
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    presetManager.setActiveSlot(3);
    auto json = makeExporterBackupJSON(makeAudioSettings(1));

    EXPECT_TRUE(SettingsExporter::importAll(json, presetManager, controlStore));
    EXPECT_EQ(presetManager.getActiveSlot(), 3);
}

TEST_F(SettingsExporterTest, ImportAllReturnsFalseWhenAudioSettingsInvalid) {
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    auto json = makeExporterBackupJSON(juce::var("not-json-object"));

    EXPECT_FALSE(SettingsExporter::importAll(json, presetManager, controlStore));
}

TEST_F(SettingsExporterTest, ImportFullBackupClearsSlotsMissingFromBackup) {
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    auto staleSlot = PresetManager::getSlotFile(1);
    EXPECT_TRUE(atomicWriteFile(staleSlot, juce::JSON::toString(makeSlot("StaleB"), true)));
    EXPECT_TRUE(staleSlot.getSiblingFile(staleSlot.getFileName() + ".bak")
                    .replaceWithText(juce::JSON::toString(makeSlot("OldBackupB"), true)));
    EXPECT_TRUE(staleSlot.withFileExtension(staleSlot.getFileExtension() + ".backup")
                    .replaceWithText(juce::JSON::toString(makeSlot("OldLegacyBackupB"), true)));
    EXPECT_TRUE(staleSlot.getSiblingFile(staleSlot.getFileName() + ".tmp")
                    .replaceWithText(juce::JSON::toString(makeSlot("OldTempB"), true)));
    auto legacySlot = ControlMappingStore::getConfigDirectory()
        .getChildFile("Slots")
        .getChildFile("slot_" + juce::String(static_cast<int>(PresetManager::slotLabel(1))) + ".dppreset");
    EXPECT_TRUE(legacySlot.replaceWithText(juce::JSON::toString(makeSlot("OldNumericB"), true)));
    EXPECT_TRUE(legacySlot.getSiblingFile(legacySlot.getFileName() + ".bak")
                    .replaceWithText(juce::JSON::toString(makeSlot("OldNumericBackupB"), true)));
    EXPECT_TRUE(legacySlot.withFileExtension(legacySlot.getFileExtension() + ".backup")
                    .replaceWithText(juce::JSON::toString(makeSlot("OldNumericLegacyBackupB"), true)));
    EXPECT_TRUE(legacySlot.getSiblingFile(legacySlot.getFileName() + ".tmp")
                    .replaceWithText(juce::JSON::toString(makeSlot("OldNumericTempB"), true)));
    presetManager.refreshSlotOccupancy();
    EXPECT_TRUE(presetManager.isSlotOccupied(1));

    auto slots = new juce::DynamicObject();
    slots->setProperty("A", makeSlot("RestoredA"));
    auto json = makeFullRestoreJSON(juce::var(slots));

    EXPECT_TRUE(SettingsExporter::importFullBackup(json, presetManager, controlStore));
    EXPECT_FALSE(staleSlot.existsAsFile());
    EXPECT_FALSE(staleSlot.getSiblingFile(staleSlot.getFileName() + ".bak").existsAsFile());
    EXPECT_FALSE(staleSlot.withFileExtension(staleSlot.getFileExtension() + ".backup").existsAsFile());
    EXPECT_FALSE(staleSlot.getSiblingFile(staleSlot.getFileName() + ".tmp").existsAsFile());
    EXPECT_FALSE(legacySlot.existsAsFile());
    EXPECT_FALSE(legacySlot.getSiblingFile(legacySlot.getFileName() + ".bak").existsAsFile());
    EXPECT_FALSE(legacySlot.withFileExtension(legacySlot.getFileExtension() + ".backup").existsAsFile());
    EXPECT_FALSE(legacySlot.getSiblingFile(legacySlot.getFileName() + ".tmp").existsAsFile());
    EXPECT_FALSE(presetManager.isSlotOccupied(1));
}

TEST_F(SettingsExporterTest, ImportFullBackupCleansBackupFamiliesAfterRestoringSlot) {
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    auto slotFile = PresetManager::getSlotFile(2);
    EXPECT_TRUE(slotFile.getSiblingFile(slotFile.getFileName() + ".bak")
                    .replaceWithText(juce::JSON::toString(makeSlot("OldBackupC"), true)));
    EXPECT_TRUE(slotFile.withFileExtension(slotFile.getFileExtension() + ".backup")
                    .replaceWithText(juce::JSON::toString(makeSlot("OldLegacyBackupC"), true)));
    EXPECT_TRUE(slotFile.getSiblingFile(slotFile.getFileName() + ".tmp")
                    .replaceWithText(juce::JSON::toString(makeSlot("OldTempC"), true)));
    auto legacySlot = ControlMappingStore::getConfigDirectory()
        .getChildFile("Slots")
        .getChildFile("slot_" + juce::String(static_cast<int>(PresetManager::slotLabel(2))) + ".dppreset");
    EXPECT_TRUE(legacySlot.replaceWithText(juce::JSON::toString(makeSlot("OldNumericC"), true)));
    EXPECT_TRUE(legacySlot.getSiblingFile(legacySlot.getFileName() + ".bak")
                    .replaceWithText(juce::JSON::toString(makeSlot("OldNumericBackupC"), true)));
    EXPECT_TRUE(legacySlot.withFileExtension(legacySlot.getFileExtension() + ".backup")
                    .replaceWithText(juce::JSON::toString(makeSlot("OldNumericLegacyBackupC"), true)));
    EXPECT_TRUE(legacySlot.getSiblingFile(legacySlot.getFileName() + ".tmp")
                    .replaceWithText(juce::JSON::toString(makeSlot("OldNumericTempC"), true)));

    auto slots = new juce::DynamicObject();
    slots->setProperty("C", makeSlot("RestoredC"));
    auto json = makeFullRestoreJSON(juce::var(slots));

    EXPECT_TRUE(SettingsExporter::importFullBackup(json, presetManager, controlStore));
    EXPECT_TRUE(slotFile.existsAsFile());
    EXPECT_FALSE(slotFile.getSiblingFile(slotFile.getFileName() + ".bak").existsAsFile());
    EXPECT_FALSE(slotFile.withFileExtension(slotFile.getFileExtension() + ".backup").existsAsFile());
    EXPECT_FALSE(slotFile.getSiblingFile(slotFile.getFileName() + ".tmp").existsAsFile());
    EXPECT_FALSE(legacySlot.existsAsFile());
    EXPECT_FALSE(legacySlot.getSiblingFile(legacySlot.getFileName() + ".bak").existsAsFile());
    EXPECT_FALSE(legacySlot.withFileExtension(legacySlot.getFileExtension() + ".backup").existsAsFile());
    EXPECT_FALSE(legacySlot.getSiblingFile(legacySlot.getFileName() + ".tmp").existsAsFile());
}

TEST_F(SettingsExporterTest, ImportFullBackupReturnsFalseWhenAudioSettingsInvalid) {
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    auto slots = new juce::DynamicObject();
    auto root = new juce::DynamicObject();
    root->setProperty("version", 2);
    root->setProperty("type", "full");
    root->setProperty("platform", SettingsExporter::getCurrentPlatform());
    root->setProperty("audioSettings", juce::var("not-json-object"));
    root->setProperty("presetSlots", juce::var(slots));
    auto json = juce::JSON::toString(juce::var(root), true);

    EXPECT_FALSE(SettingsExporter::importFullBackup(json, presetManager, controlStore));
}

TEST_F(SettingsExporterTest, ExportFullBackupUsesSlotBackupFallback) {
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    auto slotFile = PresetManager::getSlotFile(0);
    EXPECT_TRUE(slotFile.replaceWithText("{corrupt"));
    EXPECT_TRUE(slotFile.getSiblingFile(slotFile.getFileName() + ".bak")
                    .replaceWithText(juce::JSON::toString(makeSlot("RecoveredA"), true)));

    auto json = SettingsExporter::exportFullBackup(presetManager, controlStore);
    auto parsed = juce::JSON::parse(json);
    ASSERT_TRUE(parsed.isObject());

    auto* root = parsed.getDynamicObject();
    auto* slots = root->getProperty("presetSlots").getDynamicObject();
    ASSERT_NE(slots, nullptr);
    auto* slotA = slots->getProperty("A").getDynamicObject();
    ASSERT_NE(slotA, nullptr);
    EXPECT_EQ(slotA->getProperty("name").toString(), juce::String("RecoveredA"));
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
