// tests/test_settings_exporter.cpp
#include <JuceHeader.h>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <limits>
#include <thread>
#include "UI/SettingsExporter.h"
#include "Control/ControlMapping.h"
#include "Util/AtomicFileIO.h"

#if JUCE_WINDOWS
#include <windows.h>
#endif

using namespace directpipe;

namespace {

#if JUCE_WINDOWS
bool pumpMessagesUntil(const std::atomic<bool>& completed,
                       std::chrono::milliseconds timeout = std::chrono::seconds(3))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!completed.load(std::memory_order_acquire)
           && std::chrono::steady_clock::now() < deadline) {
        MSG message;
        bool dispatched = false;
        while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessage(&message);
            dispatched = true;
        }
        if (!dispatched)
            std::this_thread::yield();
    }
    return completed.load(std::memory_order_acquire);
}
#endif

juce::var makeAudioSettings(int activeSlot = -1, bool includePlugins = true)
{
    auto audio = new juce::DynamicObject();
    audio->setProperty("version", 4);
    audio->setProperty("sampleRate", 48000.0);
    audio->setProperty("bufferSize", 512);
    if (activeSlot >= -1)
        audio->setProperty("activeSlot", activeSlot);

    if (includePlugins) {
        juce::Array<juce::var> plugins;
        auto plugin = new juce::DynamicObject();
        plugin->setProperty("name", "ShouldNotImport");
        plugin->setProperty("path", "/tmp/should-not-import.vst3");
        plugin->setProperty("bypassed", false);
        plugins.add(juce::var(plugin));
        audio->setProperty("plugins", plugins);
    }

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

juce::var makeControlConfig(int websocketPort)
{
    auto controls = new juce::DynamicObject();
    controls->setProperty("hotkeys", juce::Array<juce::var>{});
    controls->setProperty("midi", juce::Array<juce::var>{});
    auto server = new juce::DynamicObject();
    server->setProperty("websocketPort", websocketPort);
    server->setProperty("websocketEnabled", true);
    server->setProperty("httpPort", 8766);
    server->setProperty("httpEnabled", true);
    controls->setProperty("server", juce::var(server));
    return juce::var(controls);
}

juce::String makeExporterBackupJSON(const juce::var& audioSettings,
                                    const juce::var& controlConfig = {})
{
    auto root = new juce::DynamicObject();
    root->setProperty("version", 2);
    root->setProperty("platform", SettingsExporter::getCurrentPlatform());
    root->setProperty("audioSettings", audioSettings);
    if (!controlConfig.isVoid())
        root->setProperty("controlConfig", controlConfig);
    return juce::JSON::toString(juce::var(root), true);
}

juce::String makeFullRestoreJSON(const juce::var& presetSlots,
                                 const juce::var& audioSettings = {},
                                 const juce::var& controlConfig = {})
{
    auto root = new juce::DynamicObject();
    root->setProperty("version", 2);
    root->setProperty("type", "full");
    root->setProperty("platform", SettingsExporter::getCurrentPlatform());
    root->setProperty("audioSettings",
                      audioSettings.isVoid() ? makeAudioSettings(-1, false) : audioSettings);
    if (!controlConfig.isVoid())
        root->setProperty("controlConfig", controlConfig);
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

TEST_F(SettingsExporterTest, ImportAllDoesNotApplyControlsWhenAudioSettingsInvalid) {
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    auto original = ControlMappingStore::createDefaults();
    original.server.websocketPort = 9001;
    ASSERT_TRUE(controlStore.save(original));

    auto json = makeExporterBackupJSON(
        juce::var("not-json-object"),
        makeControlConfig(12345));

    EXPECT_FALSE(SettingsExporter::importAll(json, presetManager, controlStore));
    EXPECT_EQ(controlStore.load().server.websocketPort, 9001);
}

TEST_F(SettingsExporterTest, ImportAllRejectsMalformedControlConfigsBeforeApplyingAudio) {
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    engine.setInputGain(0.25f);
    auto original = ControlMappingStore::createDefaults();
    original.server.websocketPort = 9001;
    ASSERT_TRUE(controlStore.save(original));

    auto incomingAudio = makeAudioSettings(-1, false);
    incomingAudio.getDynamicObject()->setProperty("inputGain", 0.75);
    auto json = makeExporterBackupJSON(
        incomingAudio, juce::var(new juce::DynamicObject()));

    EXPECT_FALSE(SettingsExporter::importAll(json, presetManager, controlStore));
    EXPECT_FLOAT_EQ(engine.getInputGain(), 0.25f);
    EXPECT_EQ(controlStore.load().server.websocketPort, 9001);

    auto invalidActionControls = makeControlConfig(12345);
    auto mapping = new juce::DynamicObject();
    mapping->setProperty("modifiers", 0);
    mapping->setProperty("virtualKey", 0x70);
    auto action = new juce::DynamicObject();
    action->setProperty("action", 999);
    mapping->setProperty("action", juce::var(action));
    juce::Array<juce::var> hotkeys;
    hotkeys.add(juce::var(mapping));
    invalidActionControls.getDynamicObject()->setProperty("hotkeys", hotkeys);

    json = makeExporterBackupJSON(incomingAudio, invalidActionControls);
    EXPECT_FALSE(SettingsExporter::importAll(json, presetManager, controlStore));
    EXPECT_FLOAT_EQ(engine.getInputGain(), 0.25f);
    EXPECT_EQ(controlStore.load().server.websocketPort, 9001);

    action->setProperty("action", static_cast<juce::int64>(4294967296LL));
    json = makeExporterBackupJSON(incomingAudio, invalidActionControls);
    EXPECT_FALSE(SettingsExporter::importAll(json, presetManager, controlStore));
    EXPECT_FLOAT_EQ(engine.getInputGain(), 0.25f);
    EXPECT_EQ(controlStore.load().server.websocketPort, 9001);

    action->setProperty("action", (std::numeric_limits<double>::max)());
    json = makeExporterBackupJSON(incomingAudio, invalidActionControls);
    EXPECT_FALSE(SettingsExporter::importAll(json, presetManager, controlStore));
    EXPECT_FLOAT_EQ(engine.getInputGain(), 0.25f);
    EXPECT_EQ(controlStore.load().server.websocketPort, 9001);

    action->setProperty("action", static_cast<int>(Action::PanicMute));
    const auto expectOptionalActionFieldRejected = [&] {
        const auto malformedJson = makeExporterBackupJSON(incomingAudio, invalidActionControls);
        EXPECT_FALSE(SettingsExporter::importAll(
            malformedJson, presetManager, controlStore));
        EXPECT_FLOAT_EQ(engine.getInputGain(), 0.25f);
        EXPECT_EQ(controlStore.load().server.websocketPort, 9001);
    };

    action->setProperty("intParam", 1.5);
    expectOptionalActionFieldRejected();
    action->removeProperty("intParam");

    action->setProperty("intParam2", "not-an-integer");
    expectOptionalActionFieldRejected();
    action->removeProperty("intParam2");

    action->setProperty("floatParam", "not-a-number");
    expectOptionalActionFieldRejected();
    action->removeProperty("floatParam");

    action->setProperty("stringParam", 1234);
    expectOptionalActionFieldRejected();
    action->removeProperty("stringParam");

    action->setProperty("floatParam", (std::numeric_limits<double>::max)());
    expectOptionalActionFieldRejected();
}

TEST_F(SettingsExporterTest, ImportAllRollsBackAudioWhenControlSaveFails) {
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    engine.setInputGain(0.25f);
    auto original = ControlMappingStore::createDefaults();
    original.server.websocketPort = 9001;
    ASSERT_TRUE(controlStore.save(original));

    const auto controlFile = ControlMappingStore::getDefaultConfigFile();
    const auto blockedTemp = controlFile.getSiblingFile(controlFile.getFileName() + ".tmp");
    ASSERT_TRUE(blockedTemp.createDirectory());
    ASSERT_TRUE(blockedTemp.getChildFile("write-blocker").replaceWithText("blocked"));

    auto incomingAudio = makeAudioSettings(-1, false);
    incomingAudio.getDynamicObject()->setProperty("inputGain", 0.75);
    const auto json = makeExporterBackupJSON(incomingAudio, makeControlConfig(12345));

    EXPECT_FALSE(SettingsExporter::importAll(json, presetManager, controlStore));
    EXPECT_FLOAT_EQ(engine.getInputGain(), 0.25f);
    EXPECT_EQ(controlStore.load().server.websocketPort, 9001);
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

    EXPECT_FALSE(SettingsExporter::importFullBackup(json, presetManager, controlStore, false));
    EXPECT_TRUE(staleSlot.existsAsFile());

    EXPECT_TRUE(SettingsExporter::importFullBackup(json, presetManager, controlStore, true));
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

    EXPECT_TRUE(SettingsExporter::importFullBackup(json, presetManager, controlStore, true));
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

    EXPECT_FALSE(SettingsExporter::importFullBackup(json, presetManager, controlStore, true));
}

TEST_F(SettingsExporterTest, ImportFullBackupPrevalidatesSlotsBeforeWriting) {
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    auto slotA = PresetManager::getSlotFile(0);
    ASSERT_TRUE(atomicWriteFile(slotA, juce::JSON::toString(makeSlot("ExistingA"), true)));

    auto slots = new juce::DynamicObject();
    slots->setProperty("A", makeSlot("RestoredA"));
    slots->setProperty("B", juce::var("not-a-slot-object"));
    auto json = makeFullRestoreJSON(juce::var(slots));

    EXPECT_FALSE(SettingsExporter::importFullBackup(json, presetManager, controlStore, true));

    auto parsed = juce::JSON::parse(slotA.loadFileAsString());
    ASSERT_TRUE(parsed.isObject());
    EXPECT_EQ(parsed.getDynamicObject()->getProperty("name").toString(), juce::String("ExistingA"));
}

TEST_F(SettingsExporterTest, ImportFullBackupRejectsStructurallyInvalidSlotObjects) {
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    auto slotA = PresetManager::getSlotFile(0);
    ASSERT_TRUE(atomicWriteFile(slotA, juce::JSON::toString(makeSlot("ExistingA"), true)));

    const auto expectRejectedWithoutOverwrite = [&](const juce::var& invalidSlot) {
        auto slots = new juce::DynamicObject();
        slots->setProperty("A", invalidSlot);
        EXPECT_FALSE(SettingsExporter::importFullBackup(
            makeFullRestoreJSON(juce::var(slots)), presetManager, controlStore, true));

        auto parsed = juce::JSON::parse(slotA.loadFileAsString());
        ASSERT_TRUE(parsed.isObject());
        EXPECT_EQ(parsed.getDynamicObject()->getProperty("name").toString(),
                  juce::String("ExistingA"));
    };

    expectRejectedWithoutOverwrite(juce::var(new juce::DynamicObject()));

    auto malformed = new juce::DynamicObject();
    malformed->setProperty("version", 4);
    juce::Array<juce::var> plugins;
    plugins.add(juce::var(new juce::DynamicObject()));
    malformed->setProperty("plugins", plugins);
    expectRejectedWithoutOverwrite(juce::var(malformed));
}

TEST_F(SettingsExporterTest, ImportFullBackupRollsBackWholeTransactionAfterLateSlotFailure) {
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    engine.setInputGain(0.25f);
    auto originalControls = ControlMappingStore::createDefaults();
    originalControls.server.websocketPort = 9001;
    ASSERT_TRUE(controlStore.save(originalControls));

    auto slotA = PresetManager::getSlotFile(0);
    auto blockedSlotB = PresetManager::getSlotFile(1);
    ASSERT_TRUE(atomicWriteFile(slotA, juce::JSON::toString(makeSlot("ExistingA"), true)));
    ASSERT_TRUE(blockedSlotB.createDirectory());
    ASSERT_TRUE(blockedSlotB.getChildFile("write-blocker").replaceWithText("blocked"));

    auto restoredAudio = makeAudioSettings(-1, false);
    restoredAudio.getDynamicObject()->setProperty("inputGain", 0.75);

    auto slots = new juce::DynamicObject();
    slots->setProperty("A", makeSlot("RestoredA"));
    slots->setProperty("B", makeSlot("RestoredB"));
    auto json = makeFullRestoreJSON(
        juce::var(slots), restoredAudio, makeControlConfig(12345));

    EXPECT_FALSE(SettingsExporter::importFullBackup(json, presetManager, controlStore, true));

    EXPECT_FLOAT_EQ(engine.getInputGain(), 0.25f);
    EXPECT_EQ(controlStore.load().server.websocketPort, 9001);

    auto parsedA = juce::JSON::parse(slotA.loadFileAsString());
    ASSERT_TRUE(parsedA.isObject());
    EXPECT_EQ(parsedA.getDynamicObject()->getProperty("name").toString(), juce::String("ExistingA"));
}

#if JUCE_WINDOWS
TEST_F(SettingsExporterTest, ImportFullBackupRollsBackSlotFilesWhenExactClearFails) {
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    auto slotA = PresetManager::getSlotFile(0);
    auto slotB = PresetManager::getSlotFile(1);
    ASSERT_TRUE(atomicWriteFile(slotA, juce::JSON::toString(makeSlot("ExistingA"), true)));
    ASSERT_TRUE(atomicWriteFile(slotB, juce::JSON::toString(makeSlot("ExistingB"), true)));

    auto lockedHandle = CreateFileW(slotB.getFullPathName().toWideCharPointer(),
                                   GENERIC_READ,
                                   0,
                                   nullptr,
                                   OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
    if (lockedHandle == INVALID_HANDLE_VALUE)
        GTEST_SKIP() << "Could not lock slot file for delete-failure simulation";

    auto slots = new juce::DynamicObject();
    slots->setProperty("A", makeSlot("RestoredA"));
    auto json = makeFullRestoreJSON(juce::var(slots));

    EXPECT_FALSE(SettingsExporter::importFullBackup(json, presetManager, controlStore, true));

    CloseHandle(lockedHandle);

    auto parsedA = juce::JSON::parse(slotA.loadFileAsString());
    ASSERT_TRUE(parsedA.isObject());
    EXPECT_EQ(parsedA.getDynamicObject()->getProperty("name").toString(), juce::String("ExistingA"));

    auto parsedB = juce::JSON::parse(slotB.loadFileAsString());
    ASSERT_TRUE(parsedB.isObject());
    EXPECT_EQ(parsedB.getDynamicObject()->getProperty("name").toString(), juce::String("ExistingB"));
}
#endif

TEST_F(SettingsExporterTest, AsyncFullRestorePreparationFailurePreservesEverything) {
#if ! JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    juce::MessageManager::getInstance();
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    ASSERT_TRUE(engine.getVSTChain()
                    .addBuiltinProcessor(PluginSlot::Type::BuiltinFilter).success);
    engine.setInputGain(0.25f);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    auto originalControls = ControlMappingStore::createDefaults();
    originalControls.server.websocketPort = 9001;
    ASSERT_TRUE(controlStore.save(originalControls));
    auto slotA = PresetManager::getSlotFile(0);
    ASSERT_TRUE(atomicWriteFile(slotA,
        juce::JSON::toString(makeSlot("ExistingA"), true)));

    auto targetAudio = makeAudioSettings(-1, true);
    targetAudio.getDynamicObject()->setProperty("inputGain", 0.75);
    auto targetSlots = new juce::DynamicObject();
    targetSlots->setProperty("A", makeSlot("RestoredA"));
    const auto json = makeFullRestoreJSON(
        juce::var(targetSlots), targetAudio, makeControlConfig(12345));

    std::atomic<bool> completed{false};
    bool restored = true;
    SettingsExporter::importFullBackupAsync(
        json, presetManager, controlStore, true,
        [&](bool ok) {
            restored = ok;
            completed.store(true, std::memory_order_release);
        });

    ASSERT_TRUE(pumpMessagesUntil(completed));
    EXPECT_FALSE(restored);
    EXPECT_FLOAT_EQ(engine.getInputGain(), 0.25f);
    EXPECT_EQ(controlStore.load().server.websocketPort, 9001);
    auto parsedA = juce::JSON::parse(slotA.loadFileAsString());
    ASSERT_TRUE(parsedA.isObject());
    EXPECT_EQ(parsedA.getDynamicObject()->getProperty("name").toString(),
              juce::String("ExistingA"));
    ASSERT_EQ(engine.getVSTChain().getPluginCount(), 1);
    auto* current = engine.getVSTChain().getPluginSlot(0);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->type, PluginSlot::Type::BuiltinFilter);
#endif
}

TEST_F(SettingsExporterTest, AsyncFullRestoreCommitsDiskControlAndCompleteChain) {
#if ! JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    juce::MessageManager::getInstance();
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    ASSERT_TRUE(engine.getVSTChain()
                    .addBuiltinProcessor(PluginSlot::Type::BuiltinFilter).success);
    engine.setInputGain(0.25f);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    auto targetAudioObject = new juce::DynamicObject();
    targetAudioObject->setProperty("version", 4);
    targetAudioObject->setProperty("inputGain", 0.75);
    juce::Array<juce::var> targetPlugins;
    auto targetPlugin = new juce::DynamicObject();
    targetPlugin->setProperty("name", "Auto Gain");
    targetPlugin->setProperty("type", "builtin_auto_gain");
    targetPlugin->setProperty("bypassed", false);
    targetPlugins.add(juce::var(targetPlugin));
    targetAudioObject->setProperty("plugins", targetPlugins);

    auto targetSlots = new juce::DynamicObject();
    targetSlots->setProperty("A", makeSlot("RestoredA"));
    const auto json = makeFullRestoreJSON(
        juce::var(targetSlots), juce::var(targetAudioObject),
        makeControlConfig(12345));

    std::atomic<bool> completed{false};
    bool restored = false;
    SettingsExporter::importFullBackupAsync(
        json, presetManager, controlStore, true,
        [&](bool ok) {
            restored = ok;
            completed.store(true, std::memory_order_release);
        });

    ASSERT_TRUE(pumpMessagesUntil(completed));
    EXPECT_TRUE(restored);
    EXPECT_FLOAT_EQ(engine.getInputGain(), 0.75f);
    EXPECT_EQ(controlStore.load().server.websocketPort, 12345);
    auto slotA = PresetManager::getSlotFile(0);
    auto parsedA = juce::JSON::parse(slotA.loadFileAsString());
    ASSERT_TRUE(parsedA.isObject());
    EXPECT_EQ(parsedA.getDynamicObject()->getProperty("name").toString(),
              juce::String("RestoredA"));
    ASSERT_EQ(engine.getVSTChain().getPluginCount(), 1);
    auto* current = engine.getVSTChain().getPluginSlot(0);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->type, PluginSlot::Type::BuiltinAutoGain);
#endif
}

TEST_F(SettingsExporterTest, ExportFullBackupRejectsUnstableRuntimeAndUsesSlotBackupFallback) {
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    auto activeSlotFile = PresetManager::getSlotFile(1);
    ASSERT_TRUE(activeSlotFile.replaceWithText(
        juce::JSON::toString(makeSlot("OriginalB"), true)));
    const auto originalActiveSlot = activeSlotFile.loadFileAsString();
    presetManager.setActiveSlot(1);

    EXPECT_TRUE(SettingsExporter::exportFullBackup(
        presetManager, controlStore, false).isEmpty());
    EXPECT_EQ(activeSlotFile.loadFileAsString(), originalActiveSlot);

    presetManager.setActiveSlot(-1);
    auto slotFile = PresetManager::getSlotFile(0);
    EXPECT_TRUE(slotFile.replaceWithText("{corrupt"));
    EXPECT_TRUE(slotFile.getSiblingFile(slotFile.getFileName() + ".bak")
                    .replaceWithText(juce::JSON::toString(makeSlot("RecoveredA"), true)));

    auto json = SettingsExporter::exportFullBackup(presetManager, controlStore, true);
    auto parsed = juce::JSON::parse(json);
    ASSERT_TRUE(parsed.isObject());

    auto* root = parsed.getDynamicObject();
    auto* slots = root->getProperty("presetSlots").getDynamicObject();
    ASSERT_NE(slots, nullptr);
    auto* slotA = slots->getProperty("A").getDynamicObject();
    ASSERT_NE(slotA, nullptr);
    EXPECT_EQ(slotA->getProperty("name").toString(), juce::String("RecoveredA"));
}

TEST_F(SettingsExporterTest, FullBackupCarriesRecordingFolderThroughAppSettingsHook) {
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager presetManager(engine);
    ControlMappingStore controlStore;

    const auto expectedFolder = tempDir_.getChildFile("Recordings").getFullPathName();
    presetManager.onExportAppSettings = [&](juce::DynamicObject& root) {
        root.setProperty("recordingFolder", expectedFolder);
    };

    const auto json = SettingsExporter::exportFullBackup(
        presetManager, controlStore, true);
    auto parsed = juce::JSON::parse(json);
    ASSERT_TRUE(parsed.isObject());
    auto* audio = parsed.getDynamicObject()
                      ->getProperty("audioSettings").getDynamicObject();
    ASSERT_NE(audio, nullptr);
    EXPECT_EQ(audio->getProperty("recordingFolder").toString(), expectedFolder);

    juce::String restoredFolder;
    presetManager.onImportAppSettings = [&](const juce::DynamicObject& root) {
        restoredFolder = root.getProperty("recordingFolder").toString();
        return true;
    };
#if JUCE_WINDOWS
    std::atomic<bool> completed{false};
    bool restored = false;
    SettingsExporter::importFullBackupAsync(
        json, presetManager, controlStore, true,
        [&](bool ok) {
            restored = ok;
            completed.store(true, std::memory_order_release);
        });
    ASSERT_TRUE(pumpMessagesUntil(completed));
    EXPECT_TRUE(restored);
#else
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#endif
    EXPECT_EQ(restoredFolder, expectedFolder);
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
