// tests/test_preset_manager.cpp
#include <JuceHeader.h>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#include "UI/PresetManager.h"
#include "UI/PresetSlotBar.h"
#include "Util/AtomicFileIO.h"

#if JUCE_WINDOWS
#include <windows.h>
#endif

using namespace directpipe;

namespace {

juce::MemoryBlock blockFromText(const char* text)
{
    return juce::MemoryBlock(text, std::strlen(text));
}

juce::String blockToString(const juce::MemoryBlock& block)
{
    return juce::String::fromUTF8(static_cast<const char*>(block.getData()),
                                  static_cast<int>(block.getSize()));
}

juce::PluginDescription makeDescription(const juce::String& name,
                                        const juce::String& path,
                                        int uniqueId)
{
    juce::PluginDescription desc;
    desc.name = name;
    desc.fileOrIdentifier = path;
    desc.uniqueId = uniqueId;
    desc.pluginFormatName = "VST3";
    return desc;
}

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

} // namespace

// Test at JSON/file level — no AudioEngine needed for most tests
class PresetManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("dp_preset_test_" +
                juce::String(juce::Random::getSystemRandom().nextInt()));
        tempDir_.createDirectory();
    }

    void TearDown() override {
        tempDir_.deleteRecursively();
    }

    juce::String makeChainJSON(int numPlugins, const std::vector<bool>& bypassed = {})
    {
        auto root = std::make_unique<juce::DynamicObject>();
        root->setProperty("version", 4);
        root->setProperty("type", "chain");

        juce::Array<juce::var> plugins;
        for (int i = 0; i < numPlugins; ++i) {
            auto p = new juce::DynamicObject();
            p->setProperty("name", "Plugin" + juce::String(i));
            p->setProperty("path", "/fake/path/plugin" + juce::String(i));
            bool bp = (i < static_cast<int>(bypassed.size())) ? bypassed[static_cast<size_t>(i)] : false;
            p->setProperty("bypassed", bp);
            juce::MemoryBlock mb("test_state", 10);
            p->setProperty("state", mb.toBase64Encoding());
            plugins.add(juce::var(p));
        }
        root->setProperty("plugins", plugins);

        return juce::JSON::toString(juce::var(root.release()), true);
    }

    juce::String makeSettingsJSON(const juce::String& chainJSON = {})
    {
        auto root = std::make_unique<juce::DynamicObject>();
        root->setProperty("version", 4);
        root->setProperty("deviceType", "Windows Audio");
        root->setProperty("sampleRate", 48000.0);
        root->setProperty("bufferSize", 480);
        root->setProperty("activeSlot", 0);

        if (chainJSON.isNotEmpty()) {
            auto chainParsed = juce::JSON::parse(chainJSON);
            if (auto* chainObj = chainParsed.getDynamicObject()) {
                if (chainObj->hasProperty("plugins"))
                    root->setProperty("plugins", chainObj->getProperty("plugins"));
            }
        }

        return juce::JSON::toString(juce::var(root.release()), true);
    }

    juce::String makeBuiltinChainJSON()
    {
        auto root = std::make_unique<juce::DynamicObject>();
        root->setProperty("version", 4);
        root->setProperty("type", "chain");

        juce::Array<juce::var> plugins;
        auto p = new juce::DynamicObject();
        p->setProperty("name", "Filter");
        p->setProperty("type", "builtin_filter");
        p->setProperty("bypassed", false);
        plugins.add(juce::var(p));
        root->setProperty("plugins", plugins);

        return juce::JSON::toString(juce::var(root.release()), true);
    }

    juce::File tempDir_;
};

class PresetManagerPortableTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto exeDir = juce::File::getSpecialLocation(
            juce::File::currentExecutableFile).getParentDirectory();
        portableFlag_ = exeDir.getChildFile("portable.flag");
        hadPortableFlag_ = portableFlag_.existsAsFile();
        if (!hadPortableFlag_)
            ASSERT_TRUE(portableFlag_.create());

        configDir_ = exeDir.getChildFile("config");
        configDir_.deleteRecursively();
        ASSERT_TRUE(configDir_.createDirectory());
    }

    void TearDown() override {
        configDir_.deleteRecursively();
        if (!hadPortableFlag_)
            portableFlag_.deleteFile();
    }

    juce::File portableFlag_;
    juce::File configDir_;
    bool hadPortableFlag_ = false;
};

TEST_F(PresetManagerTest, SaveLoadRoundtrip) {
    auto json = makeSettingsJSON(makeChainJSON(2, {false, true}));
    auto file = tempDir_.getChildFile("test.dppreset");
    EXPECT_TRUE(file.replaceWithText(json));

    auto loaded = file.loadFileAsString();
    auto parsed = juce::JSON::parse(loaded);
    ASSERT_TRUE(parsed.isObject());

    auto* root = parsed.getDynamicObject();
    EXPECT_EQ(static_cast<int>(root->getProperty("version")), 4);
    EXPECT_EQ(static_cast<double>(root->getProperty("sampleRate")), 48000.0);
    EXPECT_EQ(static_cast<int>(root->getProperty("bufferSize")), 480);

    auto* plugins = root->getProperty("plugins").getArray();
    ASSERT_NE(plugins, nullptr);
    EXPECT_EQ(plugins->size(), 2);
}

TEST_F(PresetManagerTest, BypassStateRoundtrip) {
    auto json = makeChainJSON(3, {true, false, true});
    auto file = tempDir_.getChildFile("chain.dppreset");
    file.replaceWithText(json);

    auto loaded = juce::JSON::parse(file.loadFileAsString());
    auto* plugins = loaded.getDynamicObject()->getProperty("plugins").getArray();
    ASSERT_EQ(plugins->size(), 3);

    EXPECT_TRUE(static_cast<bool>((*plugins)[0].getDynamicObject()->getProperty("bypassed")));
    EXPECT_FALSE(static_cast<bool>((*plugins)[1].getDynamicObject()->getProperty("bypassed")));
    EXPECT_TRUE(static_cast<bool>((*plugins)[2].getDynamicObject()->getProperty("bypassed")));
}

TEST_F(PresetManagerTest, BypassAcrossSlotSwitch) {
    auto slotAJson = makeChainJSON(1, {true});
    auto slotBJson = makeChainJSON(1, {false});
    auto slotA = tempDir_.getChildFile("slot_0.dppreset");
    auto slotB = tempDir_.getChildFile("slot_1.dppreset");

    slotA.replaceWithText(slotAJson);
    slotB.replaceWithText(slotBJson);

    auto loadedA1 = juce::JSON::parse(slotA.loadFileAsString());
    auto* pluginsA1 = loadedA1.getDynamicObject()->getProperty("plugins").getArray();
    EXPECT_TRUE(static_cast<bool>((*pluginsA1)[0].getDynamicObject()->getProperty("bypassed")));

    auto loadedB = juce::JSON::parse(slotB.loadFileAsString());
    auto* pluginsB = loadedB.getDynamicObject()->getProperty("plugins").getArray();
    EXPECT_FALSE(static_cast<bool>((*pluginsB)[0].getDynamicObject()->getProperty("bypassed")));

    auto loadedA2 = juce::JSON::parse(slotA.loadFileAsString());
    auto* pluginsA2 = loadedA2.getDynamicObject()->getProperty("plugins").getArray();
    EXPECT_TRUE(static_cast<bool>((*pluginsA2)[0].getDynamicObject()->getProperty("bypassed")));
}

TEST_F(PresetManagerTest, BypassAcrossSlotSwitchAsync) {
    auto slotA = tempDir_.getChildFile("slot_0.dppreset");
    slotA.replaceWithText(makeChainJSON(2, {true, false}));

    for (int i = 0; i < 5; ++i) {
        auto parsed = juce::JSON::parse(slotA.loadFileAsString());
        auto* plugins = parsed.getDynamicObject()->getProperty("plugins").getArray();
        EXPECT_TRUE(static_cast<bool>((*plugins)[0].getDynamicObject()->getProperty("bypassed")));
        EXPECT_FALSE(static_cast<bool>((*plugins)[1].getDynamicObject()->getProperty("bypassed")));
    }
}

TEST_F(PresetManagerTest, SlotOscillationSafety) {
    auto slotA = tempDir_.getChildFile("slot_0.dppreset");
    auto slotB = tempDir_.getChildFile("slot_1.dppreset");
    auto jsonA = makeChainJSON(1, {true});
    auto jsonB = makeChainJSON(1, {false});

    for (int i = 0; i < 10; ++i) {
        slotA.replaceWithText(jsonA);
        slotB.replaceWithText(jsonB);

        auto parsedA = juce::JSON::parse(slotA.loadFileAsString());
        auto parsedB = juce::JSON::parse(slotB.loadFileAsString());
        ASSERT_TRUE(parsedA.isObject()) << "Iteration " << i << ": slot A corrupt";
        ASSERT_TRUE(parsedB.isObject()) << "Iteration " << i << ": slot B corrupt";
    }
}

TEST_F(PresetManagerTest, CacheInvalidationDuringAutosave) {
    auto file = tempDir_.getChildFile("settings.dppreset");
    auto slotFile = tempDir_.getChildFile("slot_0.dppreset");

    slotFile.replaceWithText(makeChainJSON(1));
    slotFile.deleteFile();
    file.replaceWithText(makeSettingsJSON());

    EXPECT_TRUE(file.existsAsFile());
    EXPECT_FALSE(slotFile.existsAsFile());
    auto parsed = juce::JSON::parse(file.loadFileAsString());
    EXPECT_TRUE(parsed.isObject());
}

TEST_F(PresetManagerTest, CacheHitDuplicatePluginsConsumesFreshStateByIndex) {
    constexpr const char* pluginName = "reafir_standalone";
    constexpr const char* pluginPath = "/fake/reafir_standalone.vst3";

    PluginPreloadCache::CachedSlot cached;
    cached.entries.resize(2);
    for (auto& entry : cached.entries) {
        entry.name = pluginName;
        entry.path = pluginPath;
        entry.hasState = true;
        entry.stateData = blockFromText("cached-stale-state");
    }

    std::vector<PresetManager::TargetPlugin> freshTargets(2);
    for (auto& target : freshTargets) {
        target.name = pluginName;
        target.path = pluginPath;
        target.type = PluginSlot::Type::VST;
        target.hasState = true;
    }
    freshTargets[0].bypassed = true;
    freshTargets[0].stateData = blockFromText("fresh-first-state");
    freshTargets[1].bypassed = false;
    freshTargets[1].stateData = blockFromText("fresh-second-state");

    auto preloaded = PresetManager::buildPreloadedPluginsFromCache(cached, freshTargets);

    ASSERT_EQ(preloaded.size(), 2u);
    EXPECT_TRUE(preloaded[0].request.hasState);
    EXPECT_TRUE(preloaded[1].request.hasState);
    EXPECT_EQ(blockToString(preloaded[0].request.stateData), "fresh-first-state");
    EXPECT_EQ(blockToString(preloaded[1].request.stateData), "fresh-second-state");
    EXPECT_EQ(blockToString(freshTargets[0].stateData), "fresh-first-state");
    EXPECT_EQ(blockToString(freshTargets[1].stateData), "fresh-second-state");
    EXPECT_TRUE(preloaded[0].request.bypassed);
    EXPECT_FALSE(preloaded[1].request.bypassed);
}

TEST_F(PresetManagerTest, CacheRejectsSameNamePathWithDifferentDescription) {
    constexpr const char* pluginName = "same_plugin_name";
    constexpr const char* pluginPath = "/fake/shared-plugin.vst3";

    PluginPreloadCache::CachedSlot cached;
    cached.entries.resize(1);
    cached.entries[0].name = pluginName;
    cached.entries[0].path = pluginPath;
    cached.entries[0].desc = makeDescription(pluginName, pluginPath, 1001);

    std::vector<PresetManager::TargetPlugin> freshTargets(1);
    freshTargets[0].name = pluginName;
    freshTargets[0].path = pluginPath;
    freshTargets[0].type = PluginSlot::Type::VST;
    freshTargets[0].hasDesc = true;
    freshTargets[0].desc = makeDescription(pluginName, pluginPath, 2002);

    EXPECT_FALSE(PresetManager::cachedSlotMatchesTargets(cached, freshTargets));
}

TEST_F(PresetManagerTest, SlotNamingKorean) {
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("version", 4);
    root->setProperty("type", "chain");
    root->setProperty("name", juce::String::fromUTF8("\xea\xb2\x8c\xec\x9e\x84"));
    root->setProperty("plugins", juce::Array<juce::var>());

    auto file = tempDir_.getChildFile("slot_0.dppreset");
    file.replaceWithText(juce::JSON::toString(juce::var(root.release()), true));

    auto parsed = juce::JSON::parse(file.loadFileAsString());
    EXPECT_EQ(parsed.getDynamicObject()->getProperty("name").toString(),
              juce::String::fromUTF8("\xea\xb2\x8c\xec\x9e\x84"));
}

TEST_F(PresetManagerTest, SlotNamingPipeDelimiter) {
    juce::String display = "A|" + juce::String::fromUTF8("\xea\xb2\x8c\xec\x9e\x84");
    auto pipeIdx = display.indexOf("|");
    ASSERT_GT(pipeIdx, 0);
    auto label = display.substring(0, pipeIdx);
    auto name = display.substring(pipeIdx + 1);
    EXPECT_EQ(label, "A");
    EXPECT_EQ(name, juce::String::fromUTF8("\xea\xb2\x8c\xec\x9e\x84"));
}

TEST_F(PresetManagerTest, SlotNamingMaxLength) {
    juce::String longName = "VeryLongSlotName";
    EXPECT_GT(longName.length(), 8);
    auto truncated = longName.substring(0, 6) + "..";
    EXPECT_EQ(truncated.length(), 8);
    EXPECT_TRUE(truncated.endsWith(".."));
}

TEST_F(PresetManagerTest, SlotNamingEmptyName) {
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("version", 4);
    root->setProperty("type", "chain");
    root->setProperty("name", "");
    root->setProperty("plugins", juce::Array<juce::var>());

    auto parsed = juce::JSON::parse(
        juce::JSON::toString(juce::var(root.release()), true));
    auto name = parsed.getDynamicObject()->getProperty("name").toString();
    EXPECT_TRUE(name.isEmpty());
}

TEST_F(PresetManagerTest, CorruptJsonRecovery) {
    auto file = tempDir_.getChildFile("corrupt.dppreset");
    file.replaceWithText("{invalid json content here!@#$");

    auto content = file.loadFileAsString();
    auto parsed = juce::JSON::parse(content);
    EXPECT_FALSE(parsed.isObject());
}

TEST_F(PresetManagerTest, EmptyFileRecovery) {
    auto file = tempDir_.getChildFile("empty.dppreset");
    file.create();

    auto content = file.loadFileAsString();
    EXPECT_TRUE(content.isEmpty());
    auto parsed = juce::JSON::parse(content);
    EXPECT_FALSE(parsed.isObject());
}

TEST_F(PresetManagerTest, MissingKeysRecovery) {
    juce::String json = R"({"version": 4})";
    auto parsed = juce::JSON::parse(json);
    ASSERT_TRUE(parsed.isObject());

    auto* root = parsed.getDynamicObject();
    EXPECT_FALSE(root->hasProperty("plugins"));
    EXPECT_FALSE(root->hasProperty("type"));
    auto plugins = root->getProperty("plugins");
    EXPECT_TRUE(plugins.isVoid());
}

TEST_F(PresetManagerTest, NonContiguousChannelMaskJsonRoundtrip) {
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("version", 4);
    juce::Array<juce::var> inMask;
    inMask.add(0);
    inMask.add(2);
    juce::Array<juce::var> outMask;
    outMask.add(1);
    outMask.add(3);
    root->setProperty("inputChannelMask", inMask);
    root->setProperty("outputChannelMask", outMask);

    auto parsed = juce::JSON::parse(juce::JSON::toString(juce::var(root.release()), true));
    ASSERT_TRUE(parsed.isObject());
    auto* obj = parsed.getDynamicObject();
    ASSERT_TRUE(obj->hasProperty("inputChannelMask"));
    ASSERT_TRUE(obj->hasProperty("outputChannelMask"));
    auto* in = obj->getProperty("inputChannelMask").getArray();
    auto* out = obj->getProperty("outputChannelMask").getArray();
    ASSERT_NE(in, nullptr);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(static_cast<int>((*in)[0]), 0);
    EXPECT_EQ(static_cast<int>((*in)[1]), 2);
    EXPECT_EQ(static_cast<int>((*out)[0]), 1);
    EXPECT_EQ(static_cast<int>((*out)[1]), 3);
}

TEST_F(PresetManagerTest, LegacyMonoSingleChannelMaskExpandsOnlyWhenPairExists) {
    juce::BigInteger firstPair;
    firstPair.setBit(0);
    firstPair = PresetManager::migrateLegacyMonoChannelMaskForTest(
        firstPair, 2);
    EXPECT_TRUE(firstPair[0]);
    EXPECT_TRUE(firstPair[1]);
    EXPECT_EQ(firstPair.countNumberOfSetBits(), 2);

    juce::BigInteger laterPair;
    laterPair.setBit(2);
    laterPair = PresetManager::migrateLegacyMonoChannelMaskForTest(
        laterPair, 4);
    EXPECT_TRUE(laterPair[2]);
    EXPECT_TRUE(laterPair[3]);
    EXPECT_EQ(laterPair.countNumberOfSetBits(), 2);

    juce::BigInteger singleChannelDevice;
    singleChannelDevice.setBit(0);
    singleChannelDevice =
        PresetManager::migrateLegacyMonoChannelMaskForTest(
            singleChannelDevice, 1);
    EXPECT_TRUE(singleChannelDevice[0]);
    EXPECT_EQ(singleChannelDevice.countNumberOfSetBits(), 1);

    juce::BigInteger unknownDeviceLayout;
    unknownDeviceLayout.setBit(0);
    unknownDeviceLayout =
        PresetManager::migrateLegacyMonoChannelMaskForTest(
            unknownDeviceLayout, 0);
    EXPECT_TRUE(unknownDeviceLayout[0]);
    EXPECT_TRUE(unknownDeviceLayout[1]);
    EXPECT_EQ(unknownDeviceLayout.countNumberOfSetBits(), 2);

    juce::BigInteger oddTailWithoutPartner;
    oddTailWithoutPartner.setBit(2);
    oddTailWithoutPartner =
        PresetManager::migrateLegacyMonoChannelMaskForTest(
            oddTailWithoutPartner, 3);
    EXPECT_TRUE(oddTailWithoutPartner[0]);
    EXPECT_TRUE(oddTailWithoutPartner[1]);
    EXPECT_FALSE(oddTailWithoutPartner[2]);
    EXPECT_EQ(oddTailWithoutPartner.countNumberOfSetBits(), 2);

    juce::BigInteger existingNonContiguousSelection;
    existingNonContiguousSelection.setBit(0);
    existingNonContiguousSelection.setBit(2);
    const auto unchanged =
        PresetManager::migrateLegacyMonoChannelMaskForTest(
            existingNonContiguousSelection, 4);
    EXPECT_EQ(unchanged, existingNonContiguousSelection);
}

TEST_F(PresetManagerTest, ImportMigratesBoundedLegacyMonoPairWhenChannelNamesAreUnavailable) {
    AudioEngine targetEngine;
    PresetManager targetManager(targetEngine);

    juce::BigInteger currentInputChannels;
    currentInputChannels.setBit(0);
    currentInputChannels.setBit(1);
    juce::BigInteger currentOutputChannels = currentInputChannels;

    int applyCount = 0;
    juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
    targetManager.configureChannelMaskRestoreForTest(
        0,
        0,
        currentInputChannels,
        currentOutputChannels,
        [&](const juce::AudioDeviceManager::AudioDeviceSetup& setup,
            const juce::String&) {
            ++applyCount;
            appliedSetup = setup;
            return ActionResult::ok();
        });

    const juce::String json = R"({
        "version": 4,
        "channelMode": 1,
        "inputChannelMask": [2, 999],
        "outputChannelMask": [2, 999]
    })";

    ASSERT_TRUE(targetManager.importFromJSON(json));
    ASSERT_EQ(applyCount, 1);
    EXPECT_FALSE(appliedSetup.useDefaultInputChannels);
    EXPECT_FALSE(appliedSetup.useDefaultOutputChannels);
    EXPECT_TRUE(appliedSetup.inputChannels[2]);
    EXPECT_TRUE(appliedSetup.inputChannels[3]);
    EXPECT_EQ(appliedSetup.inputChannels.countNumberOfSetBits(), 2);
    EXPECT_TRUE(appliedSetup.outputChannels[2]);
    EXPECT_TRUE(appliedSetup.outputChannels[3]);
    EXPECT_EQ(appliedSetup.outputChannels.countNumberOfSetBits(), 2);
}

TEST_F(PresetManagerTest, ImportKeepsNativeInputForReportedSingleChannelDevice) {
    AudioEngine targetEngine;
    PresetManager targetManager(targetEngine);

    juce::BigInteger currentInputChannels;
    currentInputChannels.setBit(0);
    juce::BigInteger currentOutputChannels;
    currentOutputChannels.setBit(0);
    currentOutputChannels.setBit(1);

    int applyCount = 0;
    juce::AudioDeviceManager::AudioDeviceSetup appliedSetup;
    targetManager.configureChannelMaskRestoreForTest(
        1,
        2,
        currentInputChannels,
        currentOutputChannels,
        [&](const juce::AudioDeviceManager::AudioDeviceSetup& setup,
            const juce::String&) {
            ++applyCount;
            appliedSetup = setup;
            return ActionResult::ok();
        });

    const juce::String json = R"({
        "version": 4,
        "channelMode": 1,
        "inputChannelMask": [0],
        "outputChannelMask": [0]
    })";

    ASSERT_TRUE(targetManager.importFromJSON(json));
    ASSERT_EQ(applyCount, 1);
    EXPECT_TRUE(appliedSetup.inputChannels[0]);
    EXPECT_EQ(appliedSetup.inputChannels.countNumberOfSetBits(), 1);
    EXPECT_TRUE(appliedSetup.outputChannels[0]);
    EXPECT_TRUE(appliedSetup.outputChannels[1]);
    EXPECT_EQ(appliedSetup.outputChannels.countNumberOfSetBits(), 2);
}

TEST_F(PresetManagerTest, ImportFallsBackToDriverDefaultsWhenUnknownInputIsActuallyMono) {
    AudioEngine targetEngine;
    PresetManager targetManager(targetEngine);

    juce::BigInteger currentInputChannels;
    currentInputChannels.setBit(0);
    juce::BigInteger currentOutputChannels;
    currentOutputChannels.setBit(0);
    currentOutputChannels.setBit(1);

    std::vector<juce::AudioDeviceManager::AudioDeviceSetup> attempts;
    targetManager.configureChannelMaskRestoreForTest(
        0,
        2,
        currentInputChannels,
        currentOutputChannels,
        [&](const juce::AudioDeviceManager::AudioDeviceSetup& setup,
            const juce::String&) {
            attempts.push_back(setup);
            if (attempts.size() == 1)
                return ActionResult::fail(
                    "test one-channel device rejects expanded input pair");
            return ActionResult::ok();
        });

    const juce::String json = R"({
        "version": 4,
        "channelMode": 1,
        "inputChannelMask": [0],
        "outputChannelMask": [0]
    })";

    ASSERT_TRUE(targetManager.importFromJSON(json));
    ASSERT_EQ(attempts.size(), 2u);
    EXPECT_FALSE(attempts[0].useDefaultInputChannels);
    EXPECT_EQ(attempts[0].inputChannels.countNumberOfSetBits(), 2);
    EXPECT_TRUE(attempts[0].inputChannels[0]);
    EXPECT_TRUE(attempts[0].inputChannels[1]);
    EXPECT_TRUE(attempts[1].useDefaultInputChannels);
    EXPECT_TRUE(attempts[1].inputChannels.isZero());
    EXPECT_TRUE(attempts[1].useDefaultOutputChannels);
    EXPECT_TRUE(attempts[1].outputChannels.isZero());
}

TEST_F(PresetManagerTest, InvalidMaskJsonRequiresSafeFallbackPath) {
    juce::String json = R"({
        "version": 4,
        "deviceType": "ASIO",
        "inputChannelMask": [999, -1],
        "outputChannelMask": [9999]
    })";
    auto parsed = juce::JSON::parse(json);
    ASSERT_TRUE(parsed.isObject());
    auto* obj = parsed.getDynamicObject();
    ASSERT_TRUE(obj->hasProperty("inputChannelMask"));
    ASSERT_TRUE(obj->hasProperty("outputChannelMask"));
}

TEST_F(PresetManagerTest, LegacyJsonWithoutChannelMaskStillLoads) {
    auto parsed = juce::JSON::parse(makeSettingsJSON(makeChainJSON(1, {false})));
    ASSERT_TRUE(parsed.isObject());
    auto* obj = parsed.getDynamicObject();
    EXPECT_FALSE(obj->hasProperty("inputChannelMask"));
    EXPECT_FALSE(obj->hasProperty("outputChannelMask"));
}

TEST_F(PresetManagerTest, SafetyLimiterHeadroomFieldRoundtrip) {
    auto limiter = std::make_unique<juce::DynamicObject>();
    limiter->setProperty("enabled", true);
    limiter->setProperty("ceiling_dB", -0.3);
    limiter->setProperty("headroom_enabled", true);
    limiter->setProperty("headroom_dB", -0.3);

    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("version", 4);
    root->setProperty("safetyLimiter", juce::var(limiter.release()));

    auto parsed = juce::JSON::parse(juce::JSON::toString(juce::var(root.release()), true));
    ASSERT_TRUE(parsed.isObject());
    auto* obj = parsed.getDynamicObject();
    ASSERT_NE(obj, nullptr);
    ASSERT_TRUE(obj->hasProperty("safetyLimiter"));
    auto* limiterObj = obj->getProperty("safetyLimiter").getDynamicObject();
    ASSERT_NE(limiterObj, nullptr);
    ASSERT_TRUE(limiterObj->hasProperty("headroom_enabled"));
    EXPECT_TRUE(static_cast<bool>(limiterObj->getProperty("headroom_enabled")));
    ASSERT_TRUE(limiterObj->hasProperty("headroom_dB"));
    EXPECT_NEAR(static_cast<double>(limiterObj->getProperty("headroom_dB")), -0.3, 0.0001);
}

TEST_F(PresetManagerTest, SafetyLimiterHeadroomExportImportRoundtrip) {
    AudioEngine sourceEngine;
    PresetManager sourceManager(sourceEngine);
    sourceEngine.setSafetyHeadroomEnabled(false);
    sourceEngine.setSafetyHeadroomdB(-1.7f);

    auto json = sourceManager.exportToJSON();
    auto parsed = juce::JSON::parse(json);
    ASSERT_TRUE(parsed.isObject());
    auto* limiterObj = parsed.getDynamicObject()->getProperty("safetyLimiter").getDynamicObject();
    ASSERT_NE(limiterObj, nullptr);
    EXPECT_FALSE(static_cast<bool>(limiterObj->getProperty("headroom_enabled")));
    EXPECT_NEAR(static_cast<double>(limiterObj->getProperty("headroom_dB")), -1.7, 0.0001);

    AudioEngine targetEngine;
    PresetManager targetManager(targetEngine);
    ASSERT_TRUE(targetManager.importFromJSON(json));
    EXPECT_FALSE(targetEngine.isSafetyHeadroomEnabled());
    EXPECT_NEAR(targetEngine.getSafetyHeadroomdB(), -1.7f, 0.001f);
}

TEST_F(PresetManagerTest, ImportChainReportsMissingVstPluginFailure) {
    AudioEngine targetEngine;
    targetEngine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager targetManager(targetEngine);

    EXPECT_FALSE(targetManager.importChainFromJSON(makeChainJSON(1)));
    EXPECT_EQ(targetEngine.getVSTChain().getPluginCount(), 0);
}

TEST_F(PresetManagerTest, ImportFromJSONReportsIncompletePluginChain) {
    AudioEngine targetEngine;
    targetEngine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager targetManager(targetEngine);

    EXPECT_FALSE(targetManager.importFromJSON(makeSettingsJSON(makeChainJSON(1))));
    EXPECT_EQ(targetEngine.getVSTChain().getPluginCount(), 0);
}

TEST_F(PresetManagerTest, ImportFromJSONRejectsMalformedPluginChainBeforeApplyingState) {
    AudioEngine targetEngine;
    PresetManager targetManager(targetEngine);
    targetManager.setActiveSlot(3);

    juce::String nonArrayPlugins = R"({
        "version": 4,
        "activeSlot": 1,
        "plugins": "not an array"
    })";
    EXPECT_FALSE(targetManager.importFromJSON(nonArrayPlugins));
    EXPECT_EQ(targetManager.getActiveSlot(), 3);

    juce::String malformedEntry = R"({
        "version": 4,
        "activeSlot": 1,
        "plugins": ["not an object"]
    })";
    EXPECT_FALSE(targetManager.importFromJSON(malformedEntry));
    EXPECT_EQ(targetManager.getActiveSlot(), 3);

    targetEngine.setInputGain(0.6f);
    juce::String emptyPluginObject = R"({
        "version": 4,
        "activeSlot": 1,
        "inputGain": 1.7,
        "plugins": [{}]
    })";
    EXPECT_FALSE(targetManager.importFromJSON(emptyPluginObject));
    EXPECT_EQ(targetManager.getActiveSlot(), 3);
    EXPECT_FLOAT_EQ(targetEngine.getInputGain(), 0.6f);
}

TEST_F(PresetManagerTest, StructuralValidationRejectsMalformedFileWithoutImporting) {
    AudioEngine engine;
    PresetManager manager(engine);
    auto file = tempDir_.getChildFile("candidate.dppreset");

    ASSERT_TRUE(file.replaceWithText(R"({"version":4,"plugins":[{}]})"));
    EXPECT_FALSE(manager.isPresetFileStructurallyValid(file));

    ASSERT_TRUE(file.replaceWithText(makeBuiltinChainJSON()));
    EXPECT_TRUE(manager.isPresetFileStructurallyValid(file));
}

TEST_F(PresetManagerTest, InvalidPluginStateFallsBackToStructurallyValidBackup) {
    auto file = tempDir_.getChildFile("invalid-state.dppreset");
    const juce::String invalidPrimary = R"({
        "version": 4,
        "plugins": [{
            "name": "CorruptState",
            "path": "/tmp/corrupt-state.vst3",
            "state": "1.!!!!"
        }]
    })";
    const auto validBackup = makeBuiltinChainJSON();

    ASSERT_TRUE(file.replaceWithText(invalidPrimary));
    ASSERT_TRUE(file.getSiblingFile(file.getFileName() + ".bak")
                    .replaceWithText(validBackup));

    const auto recovered = PresetManager::loadPresetJSONWithBackupFallback(file, true);
    EXPECT_EQ(recovered, validBackup);
    EXPECT_EQ(file.loadFileAsString(), validBackup);
}

TEST_F(PresetManagerTest, ValidBackupLoadsWhenPrimaryRepairFails) {
    auto blockedPrimary = tempDir_.getChildFile("blocked-primary.dppreset");
    ASSERT_TRUE(blockedPrimary.createDirectory());
    ASSERT_TRUE(blockedPrimary.getChildFile("write-blocker").replaceWithText("blocked"));

    const auto validBackup = makeBuiltinChainJSON();
    ASSERT_TRUE(blockedPrimary.getSiblingFile(blockedPrimary.getFileName() + ".bak")
                    .replaceWithText(validBackup));

    EXPECT_EQ(PresetManager::loadPresetJSONWithBackupFallback(blockedPrimary, true),
              validBackup);
    EXPECT_TRUE(blockedPrimary.isDirectory());
}

TEST_F(PresetManagerTest, ImportChainAcceptsBuiltinOnlyChain) {
    AudioEngine targetEngine;
    targetEngine.getVSTChain().prepareToPlay(48000.0, 512);
    PresetManager targetManager(targetEngine);

    EXPECT_TRUE(targetManager.importChainFromJSON(makeBuiltinChainJSON()));
    EXPECT_EQ(targetEngine.getVSTChain().getPluginCount(), 1);
    auto* slot = targetEngine.getVSTChain().getPluginSlot(0);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->type, PluginSlot::Type::BuiltinFilter);
}

TEST_F(PresetManagerTest, AsioRestoreDevicePrefersInputForMismatchedDuplexNames) {
    EXPECT_EQ(PresetManager::selectAsioRestoreDeviceForTest("FL Studio ASIO", "Realtek ASIO"),
              "FL Studio ASIO");
    EXPECT_EQ(PresetManager::selectAsioRestoreDeviceForTest({}, "Realtek ASIO"),
              "Realtek ASIO");
}

TEST_F(PresetManagerTest, AsioRestoreDeviceMustMatchBeforeApplyingAsioDetails) {
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = "FL Studio ASIO";
    setup.outputDeviceName = "FL Studio ASIO";

    EXPECT_FALSE(PresetManager::isAsioRestoreDeviceActiveForTest("Realtek ASIO", setup));
    EXPECT_TRUE(PresetManager::isAsioRestoreDeviceActiveForTest("FL Studio ASIO", setup));
    EXPECT_TRUE(PresetManager::isAsioRestoreDeviceActiveForTest({}, setup));

    setup.outputDeviceName = "Realtek ASIO";
    EXPECT_FALSE(PresetManager::isAsioRestoreDeviceActiveForTest("FL Studio ASIO", setup));

    setup.inputDeviceName = "Realtek ASIO";
    setup.outputDeviceName = "FL Studio ASIO";
    EXPECT_FALSE(PresetManager::isAsioRestoreDeviceActiveForTest("FL Studio ASIO", setup));
}

TEST_F(PresetManagerTest, ImportRemembersUnavailableStartupDevicesForRetry) {
    AudioEngine targetEngine;
    PresetManager targetManager(targetEngine);
    juce::String surfacedWarning;
    targetManager.onImportWarning = [&](const juce::String& warning) {
        surfacedWarning = warning;
    };

    juce::String json = R"({
        "version": 4,
        "inputDevice": "Boot Mic",
        "outputDevice": "Boot Speakers",
        "outputMuted": false
    })";

    ASSERT_TRUE(targetManager.importFromJSON(json));
    EXPECT_EQ(targetEngine.getDesiredInputDevice(), "Boot Mic");
    EXPECT_EQ(targetEngine.getDesiredOutputDevice(), "Boot Speakers");
    EXPECT_TRUE(targetEngine.isDeviceLost());
    EXPECT_TRUE(targetEngine.isInputDeviceLost());
    EXPECT_TRUE(targetEngine.isOutputAutoMuted());
    EXPECT_TRUE(surfacedWarning.containsIgnoreCase("could not be restored"));
}

TEST_F(PresetManagerTest, LegacySafetyLimiterJsonWithoutHeadroomStillLoads) {
    juce::String json = R"({
        "version": 4,
        "safetyLimiter": {
            "enabled": true,
            "ceiling_dB": -0.3
        }
    })";
    auto parsed = juce::JSON::parse(json);
    ASSERT_TRUE(parsed.isObject());
    auto* obj = parsed.getDynamicObject();
    ASSERT_NE(obj, nullptr);
    auto* limiterObj = obj->getProperty("safetyLimiter").getDynamicObject();
    ASSERT_NE(limiterObj, nullptr);
    EXPECT_FALSE(limiterObj->hasProperty("headroom_enabled"));
    EXPECT_FALSE(limiterObj->hasProperty("headroom_dB"));

    AudioEngine targetEngine;
    PresetManager targetManager(targetEngine);
    targetEngine.setSafetyHeadroomEnabled(false);
    targetEngine.setSafetyHeadroomdB(-2.0f);

    ASSERT_TRUE(targetManager.importFromJSON(json));
    EXPECT_TRUE(targetEngine.isSafetyHeadroomEnabled());
    EXPECT_NEAR(targetEngine.getSafetyHeadroomdB(), -0.3f, 0.001f);
}

TEST_F(PresetManagerTest, SelfHealingFromSlotFile) {
    auto settings = tempDir_.getChildFile("settings.dppreset");
    auto slot0 = tempDir_.getChildFile("slot_0.dppreset");

    settings.replaceWithText(makeSettingsJSON());
    slot0.replaceWithText(makeChainJSON(2, {true, false}));

    auto slotParsed = juce::JSON::parse(slot0.loadFileAsString());
    auto* plugins = slotParsed.getDynamicObject()->getProperty("plugins").getArray();
    ASSERT_NE(plugins, nullptr);
    EXPECT_EQ(plugins->size(), 2);

    auto settingsParsed = juce::JSON::parse(settings.loadFileAsString());
    auto settingsPlugins = settingsParsed.getDynamicObject()->getProperty("plugins");
    EXPECT_TRUE(settingsPlugins.isVoid() || settingsPlugins.getArray() == nullptr
                || settingsPlugins.getArray()->isEmpty());
}

TEST_F(PresetManagerTest, PluginStateBase64Roundtrip) {
    juce::MemoryBlock original("Hello DirectPipe Plugin State!", 30);
    auto encoded = original.toBase64Encoding();
    EXPECT_FALSE(encoded.isEmpty());

    juce::MemoryBlock decoded;
    decoded.fromBase64Encoding(encoded);
    EXPECT_EQ(original, decoded);
}

TEST_F(PresetManagerTest, MultiplePluginChainRoundtrip) {
    auto json = makeChainJSON(3, {true, false, true});
    auto file = tempDir_.getChildFile("chain.dppreset");
    file.replaceWithText(json);

    auto parsed = juce::JSON::parse(file.loadFileAsString());
    auto* plugins = parsed.getDynamicObject()->getProperty("plugins").getArray();
    ASSERT_EQ(plugins->size(), 3);

    for (int i = 0; i < 3; ++i) {
        auto* p = (*plugins)[i].getDynamicObject();
        EXPECT_EQ(p->getProperty("name").toString(), "Plugin" + juce::String(i));
        EXPECT_EQ(p->getProperty("path").toString(),
                  "/fake/path/plugin" + juce::String(i));
    }
    EXPECT_TRUE(static_cast<bool>((*plugins)[0].getDynamicObject()->getProperty("bypassed")));
    EXPECT_FALSE(static_cast<bool>((*plugins)[1].getDynamicObject()->getProperty("bypassed")));
    EXPECT_TRUE(static_cast<bool>((*plugins)[2].getDynamicObject()->getProperty("bypassed")));
}

// ─── Auto Slot Constants ───

TEST_F(PresetManagerPortableTest, CopySlotPreservesDestinationWhenSourceIsCorrupt) {
    AudioEngine engine;
    PresetManager manager(engine);

    auto source = PresetManager::getSlotFile(0);
    auto destination = PresetManager::getSlotFile(1);
    const juce::String destinationJson =
        R"({"version":4,"name":"KeepMe","plugins":[]})";

    ASSERT_TRUE(source.replaceWithText("CORRUPTED"));
    ASSERT_TRUE(destination.replaceWithText(destinationJson));

    EXPECT_FALSE(manager.copySlot(0, 1));
    EXPECT_EQ(destination.loadFileAsString(), destinationJson);
}

TEST_F(PresetManagerPortableTest, CopySlotPreservesDestinationWhenSourceIsParseableButInvalid) {
    AudioEngine engine;
    PresetManager manager(engine);

    const auto source = PresetManager::getSlotFile(0);
    const auto destination = PresetManager::getSlotFile(1);
    const juce::String destinationJson = R"({"version":4,"type":"chain","plugins":[]})";

    ASSERT_TRUE(source.replaceWithText("{}"));
    ASSERT_TRUE(destination.replaceWithText(destinationJson));

    EXPECT_FALSE(manager.copySlot(0, 1));
    EXPECT_EQ(destination.loadFileAsString(), destinationJson);
}

TEST_F(PresetManagerPortableTest, CopySlotUsesStructurallyValidBackup) {
    AudioEngine engine;
    PresetManager manager(engine);

    const auto source = PresetManager::getSlotFile(0);
    const auto backup = source.getSiblingFile(source.getFileName() + ".bak");
    const auto destination = PresetManager::getSlotFile(1);
    const juce::String validBackup = R"({"version":4,"type":"chain","plugins":[]})";

    ASSERT_TRUE(source.replaceWithText("{}"));
    ASSERT_TRUE(backup.replaceWithText(validBackup));

    EXPECT_TRUE(manager.copySlot(0, 1));
    EXPECT_EQ(destination.loadFileAsString(), validBackup);
    EXPECT_EQ(source.loadFileAsString(), validBackup);

    // A crash during the old numeric-name era can leave only .bak or
    // .backup. Both families must migrate to the canonical letter name so
    // load/copy/full-backup callers can resolve the occupied slot.
    const auto slotsDir = configDir_.getChildFile("Slots");
    const auto legacyC = slotsDir.getChildFile("slot_67.dppreset");
    const auto legacyCBackup = legacyC.getSiblingFile(legacyC.getFileName() + ".bak");
    ASSERT_TRUE(legacyCBackup.replaceWithText(validBackup));
    const auto canonicalC = PresetManager::getSlotFile(2);
    EXPECT_EQ(canonicalC.loadFileAsString(), validBackup);
    EXPECT_FALSE(legacyCBackup.existsAsFile());

    const auto legacyD = slotsDir.getChildFile("slot_68.dppreset");
    const auto legacyDBackup = legacyD.withFileExtension(legacyD.getFileExtension() + ".backup");
    ASSERT_TRUE(legacyDBackup.replaceWithText(validBackup));
    const auto canonicalD = PresetManager::getSlotFile(3);
    EXPECT_EQ(canonicalD.loadFileAsString(), validBackup);
    EXPECT_FALSE(legacyDBackup.existsAsFile());
}

TEST_F(PresetManagerPortableTest, SlotNameCacheChangesOnlyAfterDurableWrite) {
    const auto slot = PresetManager::getSlotFile(0);
    ASSERT_TRUE(slot.replaceWithText(
        R"({"version":4,"type":"chain","name":"Original","plugins":[]})"));

    AudioEngine engine;
    PresetManager manager(engine);
    ASSERT_EQ(manager.getSlotName(0), juce::String("Original"));

    const auto blockedTemp = slot.getSiblingFile(slot.getFileName() + ".tmp");
    ASSERT_TRUE(blockedTemp.createDirectory());
    ASSERT_TRUE(blockedTemp.getChildFile("write-blocker").replaceWithText("blocked"));

    EXPECT_FALSE(manager.setSlotName(0, "Unsaved"));
    EXPECT_EQ(manager.getSlotName(0), juce::String("Original"));
    auto persisted = juce::JSON::parse(slot.loadFileAsString());
    ASSERT_TRUE(persisted.isObject());
    EXPECT_EQ(persisted.getDynamicObject()->getProperty("name").toString(),
              juce::String("Original"));
}

TEST_F(PresetManagerPortableTest, ActiveSlotImportRollsBackFileAndRuntimeWhenPluginCannotLoad) {
#if ! JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    juce::MessageManager::getInstance();
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    ASSERT_TRUE(engine.getVSTChain()
                    .addBuiltinProcessor(PluginSlot::Type::BuiltinFilter).success);

    PresetManager manager(engine);
    ASSERT_TRUE(manager.saveSlot(0));
    const auto slot = PresetManager::getSlotFile(0);
    const auto originalSlot = slot.loadFileAsString();
    ASSERT_EQ(manager.getActiveSlot(), 0);
    ASSERT_EQ(engine.getVSTChain().getPluginCount(), 1);

    const auto imported = configDir_.getChildFile("missing-plugin.dpchain");
    ASSERT_TRUE(imported.replaceWithText(R"({
        "version": 4,
        "type": "chain",
        "name": "Broken",
        "plugins": [{
            "name": "Definitely Missing Plugin",
            "path": "C:/DirectPipe/tests/definitely-missing.vst3",
            "bypassed": false
        }]
    })"));

    std::atomic<bool> completed{false};
    bool importedOk = true;
    manager.importSlotFromFileAsync(0, imported, [&](bool ok) {
        importedOk = ok;
        completed.store(true, std::memory_order_release);
    });

    ASSERT_TRUE(pumpMessagesUntil(completed));
    EXPECT_FALSE(importedOk);
    EXPECT_EQ(slot.loadFileAsString(), originalSlot);
    EXPECT_EQ(manager.getActiveSlot(), 0);
    EXPECT_EQ(engine.getVSTChain().getPluginCount(), 1);
    auto* restored = engine.getVSTChain().getPluginSlot(0);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->type, PluginSlot::Type::BuiltinFilter);
#endif
}

TEST_F(PresetManagerPortableTest, CopyToActiveKeepsFileAndRuntimeWhenSourcePluginCannotPrepare) {
#if ! JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    juce::MessageManager::getInstance();
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    ASSERT_TRUE(engine.getVSTChain()
                    .addBuiltinProcessor(PluginSlot::Type::BuiltinFilter).success);

    PresetManager manager(engine);
    ASSERT_TRUE(manager.saveSlot(1));
    const auto destination = PresetManager::getSlotFile(1);
    const auto originalDestination = destination.loadFileAsString();

    const auto source = PresetManager::getSlotFile(0);
    ASSERT_TRUE(source.replaceWithText(R"({
        "version":4,
        "type":"chain",
        "name":"Unavailable",
        "plugins":[{
            "name":"Definitely Missing Plugin",
            "path":"C:/DirectPipe/tests/definitely-missing.vst3",
            "bypassed":false
        }]
    })"));

    std::atomic<bool> completed{false};
    bool copyOk = true;
    manager.copySlotToActiveAsync(0, 1, [&](bool ok) {
        copyOk = ok;
        completed.store(true, std::memory_order_release);
    });

    ASSERT_TRUE(pumpMessagesUntil(completed));
    EXPECT_FALSE(copyOk);
    EXPECT_EQ(destination.loadFileAsString(), originalDestination);
    EXPECT_EQ(manager.getActiveSlot(), 1);
    ASSERT_EQ(engine.getVSTChain().getPluginCount(), 1);
    auto* current = engine.getVSTChain().getPluginSlot(0);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->type, PluginSlot::Type::BuiltinFilter);
#endif
}

TEST_F(PresetManagerPortableTest, ActiveImportCommitsFileOnlyAfterPreparedChainCanSwap) {
#if ! JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    juce::MessageManager::getInstance();
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    ASSERT_TRUE(engine.getVSTChain()
                    .addBuiltinProcessor(PluginSlot::Type::BuiltinFilter).success);

    PresetManager manager(engine);
    ASSERT_TRUE(manager.saveSlot(0));
    const auto destination = PresetManager::getSlotFile(0);

    const auto imported = configDir_.getChildFile("prepared-builtin.dpchain");
    ASSERT_TRUE(imported.replaceWithText(R"({
        "version":4,
        "type":"chain",
        "name":"Prepared",
        "plugins":[{
            "name":"Auto Gain",
            "type":"builtin_auto_gain",
            "bypassed":false
        }]
    })"));

    std::atomic<bool> completed{false};
    bool importedOk = false;
    manager.importSlotFromFileAsync(0, imported, [&](bool ok) {
        importedOk = ok;
        completed.store(true, std::memory_order_release);
    });

    ASSERT_TRUE(pumpMessagesUntil(completed));
    EXPECT_TRUE(importedOk);
    EXPECT_EQ(manager.getActiveSlot(), 0);
    EXPECT_EQ(manager.getSlotName(0), juce::String("Prepared"));
    ASSERT_EQ(engine.getVSTChain().getPluginCount(), 1);
    auto* current = engine.getVSTChain().getPluginSlot(0);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->type, PluginSlot::Type::BuiltinAutoGain);
    EXPECT_EQ(destination.loadFileAsString(), imported.loadFileAsString());
#endif
}

TEST_F(PresetManagerPortableTest, TransactionalPresetLoadFailureKeepsSettingsAndChain) {
#if ! JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    juce::MessageManager::getInstance();
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    ASSERT_TRUE(engine.getVSTChain()
                    .addBuiltinProcessor(PluginSlot::Type::BuiltinFilter).success);
    engine.setInputGain(0.25f);
    PresetManager manager(engine);

    const juce::String target = R"({
        "version":4,
        "inputGain":0.75,
        "plugins":[{
            "name":"Definitely Missing Plugin",
            "path":"C:\\DirectPipe-Test-Missing\\missing.vst3",
            "bypassed":false
        }]
    })";

    std::atomic<bool> completed{false};
    bool loaded = true;
    manager.importFromJSONTransactionalAsync(
        target, nullptr, nullptr,
        [&](bool ok) {
            loaded = ok;
            completed.store(true, std::memory_order_release);
        });

    ASSERT_TRUE(pumpMessagesUntil(completed));
    EXPECT_FALSE(loaded);
    EXPECT_FLOAT_EQ(engine.getInputGain(), 0.25f);
    ASSERT_EQ(engine.getVSTChain().getPluginCount(), 1);
    auto* current = engine.getVSTChain().getPluginSlot(0);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->type, PluginSlot::Type::BuiltinFilter);
#endif
}

TEST_F(PresetManagerPortableTest, TransactionalPresetLoadCommitsCompleteBuiltinChain) {
#if ! JUCE_WINDOWS
    GTEST_SKIP() << "Deterministic JUCE message-queue pumping is Windows-only";
#else
    juce::MessageManager::getInstance();
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    ASSERT_TRUE(engine.getVSTChain()
                    .addBuiltinProcessor(PluginSlot::Type::BuiltinFilter).success);
    engine.setInputGain(0.25f);
    PresetManager manager(engine);

    const juce::String target = R"({
        "version":4,
        "inputGain":0.75,
        "plugins":[{
            "name":"Auto Gain",
            "type":"builtin_auto_gain",
            "bypassed":false
        }]
    })";

    std::atomic<bool> completed{false};
    bool loaded = false;
    manager.importFromJSONTransactionalAsync(
        target, nullptr, nullptr,
        [&](bool ok) {
            loaded = ok;
            completed.store(true, std::memory_order_release);
        });

    ASSERT_TRUE(pumpMessagesUntil(completed));
    EXPECT_TRUE(loaded);
    EXPECT_FLOAT_EQ(engine.getInputGain(), 0.75f);
    ASSERT_EQ(engine.getVSTChain().getPluginCount(), 1);
    auto* current = engine.getVSTChain().getPluginSlot(0);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->type, PluginSlot::Type::BuiltinAutoGain);
#endif
}

TEST_F(PresetManagerPortableTest, AsyncSlotLoadRejectsMalformedEntriesAtomically) {
    AudioEngine engine;
    engine.getVSTChain().prepareToPlay(48000.0, 512);
    auto addResult = engine.getVSTChain().addBuiltinProcessor(PluginSlot::Type::BuiltinFilter);
    ASSERT_TRUE(addResult.success);

    PresetManager manager(engine);
    manager.setActiveSlot(2);

    auto slot = PresetManager::getSlotFile(0);
    ASSERT_TRUE(slot.replaceWithText(R"({
        "version": 4,
        "name": "Malformed",
        "plugins": [
            {"name":"Filter","type":"builtin_filter","bypassed":false},
            "junk"
        ]
    })"));

    bool callbackCalled = false;
    bool callbackResult = true;
    manager.loadSlotAsync(0, [&](bool ok) {
        callbackCalled = true;
        callbackResult = ok;
    });

    EXPECT_TRUE(callbackCalled);
    EXPECT_FALSE(callbackResult);
    EXPECT_EQ(manager.getActiveSlot(), 2);
    EXPECT_EQ(engine.getVSTChain().getPluginCount(), 1);
}

#if JUCE_WINDOWS
TEST_F(PresetManagerPortableTest, DeleteSlotKeepsStateWhenFileDeletionFails) {
    auto slot = PresetManager::getSlotFile(0);
    ASSERT_TRUE(slot.replaceWithText(R"({"version":4,"plugins":[]})"));

    AudioEngine engine;
    PresetManager manager(engine);
    ASSERT_TRUE(manager.isSlotOccupied(0));

    const auto path = slot.getFullPathName();
    auto lockedFile = CreateFileW(path.toWideCharPointer(),
                                  GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
    ASSERT_NE(lockedFile, INVALID_HANDLE_VALUE) << GetLastError();

    EXPECT_FALSE(manager.deleteSlot(0));
    EXPECT_TRUE(slot.existsAsFile());
    EXPECT_TRUE(manager.isSlotOccupied(0));

    CloseHandle(lockedFile);
}
#endif

TEST(PresetSlotBarConstants, AutoSlotIndex) {
    EXPECT_EQ(PresetSlotBar::kAutoSlotIndex, 5);
    EXPECT_EQ(PresetSlotBar::kNumPresetSlots, 5);  // A-E only (Auto is separate)
}

TEST(PresetManagerConstants, NumSlotsIncludesAuto) {
    EXPECT_EQ(PresetManager::kNumSlots, 6);  // A-E (0-4) + Auto (5)
}

// ─── Atomic File IO ───

TEST_F(PresetManagerTest, AtomicWriteFileCreatesFile) {
    auto target = tempDir_.getChildFile("test.json");
    EXPECT_TRUE(atomicWriteFile(target, R"({"version":4})"));
    EXPECT_TRUE(target.existsAsFile());
    EXPECT_EQ(target.loadFileAsString().trimEnd(), R"({"version":4})");
}

TEST_F(PresetManagerTest, AtomicWriteFileCreatesBackup) {
    auto target = tempDir_.getChildFile("test.json");
    EXPECT_TRUE(atomicWriteFile(target, R"({"version":1})"));
    auto bak = tempDir_.getChildFile("test.json.bak");
    EXPECT_FALSE(bak.existsAsFile());

    EXPECT_TRUE(atomicWriteFile(target, R"({"version":2})"));
    EXPECT_TRUE(bak.existsAsFile());
    EXPECT_EQ(bak.loadFileAsString().trimEnd(), R"({"version":1})");
    EXPECT_EQ(target.loadFileAsString().trimEnd(), R"({"version":2})");
}

TEST_F(PresetManagerTest, AtomicWriteNoTmpLeftOver) {
    auto target = tempDir_.getChildFile("test.json");
    EXPECT_TRUE(atomicWriteFile(target, R"({"ok":true})"));
    auto tmp = tempDir_.getChildFile("test.json.tmp");
    EXPECT_FALSE(tmp.existsAsFile());
}

TEST_F(PresetManagerTest, BackupFallbackRecovery) {
    auto target = tempDir_.getChildFile("test.json");
    auto bak = tempDir_.getChildFile("test.json.bak");

    bak.replaceWithText(R"({"version":4,"type":"chain","plugins":[]})");
    target.replaceWithText("CORRUPTED{{{");

    auto content = loadFileWithBackupFallback(target);
    EXPECT_FALSE(content.isEmpty());
    auto parsed = juce::JSON::parse(content);
    EXPECT_TRUE(parsed.isObject());
    EXPECT_EQ(static_cast<int>(parsed.getDynamicObject()->getProperty("version")), 4);
}

TEST_F(PresetManagerTest, BackupFallbackBothCorrupt) {
    auto target = tempDir_.getChildFile("test.json");
    auto bak = tempDir_.getChildFile("test.json.bak");

    target.replaceWithText("CORRUPTED");
    bak.replaceWithText("ALSO_CORRUPTED");

    auto content = loadFileWithBackupFallback(target);
    EXPECT_TRUE(content.isEmpty());
}

TEST_F(PresetManagerTest, BackupFallbackMainValid) {
    auto target = tempDir_.getChildFile("test.json");
    target.replaceWithText(R"({"version":4,"type":"chain","plugins":[]})");

    auto content = loadFileWithBackupFallback(target);
    EXPECT_FALSE(content.isEmpty());
    EXPECT_TRUE(juce::JSON::parse(content).isObject());
}
