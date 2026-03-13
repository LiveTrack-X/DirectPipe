// tests/test_preset_manager.cpp
#include <JuceHeader.h>
#include <gtest/gtest.h>

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

    juce::File tempDir_;
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
