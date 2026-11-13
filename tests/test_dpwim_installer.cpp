#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "UI/DpwimInstaller.h"
#include "UI/PluginScanner.h"
#include <atomic>
#include <thread>
#if JUCE_WINDOWS
#include <windows.h>
#endif

using namespace directpipe::dpwim;
namespace {
const juce::String module = "DirectPipe Windows Input Mixer.vst3";
const juce::String binary = module + "/Contents/x86_64-win/DirectPipe Windows Input Mixer.vst3";
const BundleInspector fixtureInspector = [](const juce::File& bundle, const juce::String&) {
    return bundle.getChildFile("Contents/x86_64-win/DirectPipe Windows Input Mixer.vst3").existsAsFile()
        ? juce::Result::ok() : juce::Result::fail("missing binary");
};
juce::MemoryBlock zipOf(const std::vector<std::pair<juce::String, juce::String>>& files)
{
    juce::ZipFile::Builder builder;
    for (const auto& item : files) {
        const auto utf8 = item.second.toRawUTF8();
        builder.addEntry(new juce::MemoryInputStream(utf8, item.second.getNumBytesAsUTF8(), true), 6, item.first, juce::Time::getCurrentTime());
    }
    juce::MemoryOutputStream stream;
    EXPECT_TRUE(builder.writeToStream(stream, nullptr));
    return stream.getMemoryBlock();
}
Release releaseFor(const juce::MemoryBlock& zip, const juce::String& version = "0.3.4")
{
    Release r; r.version = version; r.assetName = "DPWIM-v" + version + "-Windows-x64-VST3.zip";
    r.url = "https://github.com/LiveTrack-X/DPWIM/releases/download/v" + version + "/" + r.assetName;
    r.sha256 = juce::SHA256(zip).toHexString(); return r;
}
juce::var releaseJson(const Release& r, int size = 100)
{
    auto root = new juce::DynamicObject(); root->setProperty("tag_name", "v" + r.version);
    root->setProperty("draft", false); root->setProperty("prerelease", false);
    auto asset = new juce::DynamicObject(); asset->setProperty("name", r.assetName);
    asset->setProperty("browser_download_url", r.url); asset->setProperty("digest", "sha256:" + r.sha256);
    asset->setProperty("size", size); juce::Array<juce::var> assets{juce::var(asset)};
    root->setProperty("assets", assets); return juce::var(root);
}
class DpwimInstallerTest : public ::testing::Test {
protected:
    void SetUp() override {
        juce::MessageManager::getInstance();
        root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("dpwi-" + juce::Uuid().toString());
        ASSERT_TRUE(root.createDirectory());
    }
    void TearDown() override { root.deleteRecursively(); }
    juce::File root;
    std::atomic<bool> cancel{false};
};
}

TEST_F(DpwimInstallerTest, StableReleaseRequiresExactWindowsAssetAndDigest)
{
    const auto zip = zipOf({{binary, "one"}}); const auto expected = releaseFor(zip);
    Release parsed; EXPECT_TRUE(parseRelease(releaseJson(expected), parsed));
    EXPECT_EQ(parsed.url, expected.url); EXPECT_EQ(parsed.sha256, expected.sha256);
    for (const auto& badVersion : {"../0.3.4", "0.3.4-beta", "00.3.4", "0.3", "0.3.70000"}) EXPECT_FALSE(validVersion(badVersion));
    EXPECT_LT(compareVersions("0.9.9", "0.10.0"), 0);
    auto bad = releaseJson(expected); bad.getDynamicObject()->setProperty("prerelease", true);
    EXPECT_FALSE(parseRelease(bad, parsed));
    for (const auto& url : {"http://github.com/a.zip", "https://example.org/a.zip", "https://github.com/Other/DPWIM/a.zip"}) {
        auto r = expected; r.url = url; EXPECT_FALSE(parseRelease(releaseJson(r), parsed));
    }
    auto r = expected; r.sha256 = "bad"; EXPECT_FALSE(parseRelease(releaseJson(r), parsed));
    EXPECT_FALSE(parseRelease(releaseJson(expected, 40 * 1024 * 1024), parsed));
}

TEST_F(DpwimInstallerTest, InstallAndUpdateKeepOldFilesAndPublishManifestLast)
{
    auto zip = zipOf({{binary, "old"}, {"LICENSE", "license"}}); auto r = releaseFor(zip);
    const auto old = installArchive(root, r, zip, cancel, fixtureInspector);
    ASSERT_TRUE(old.success) << old.message; EXPECT_TRUE(old.bundle.isDirectory());
    ASSERT_TRUE(installedCopy(root, fixtureInspector).success);
    const auto oldManifest = root.getChildFile("installed.json").loadFileAsString();
    zip = zipOf({{binary, "new"}}); r = releaseFor(zip, "0.3.5");
    const auto rejecting = [](const juce::File&, const juce::String&) { return juce::Result::fail("version mismatch"); };
    EXPECT_FALSE(installArchive(root, r, zip, cancel, rejecting).success);
    EXPECT_EQ(root.getChildFile("installed.json").loadFileAsString(), oldManifest);
    const auto newer = installArchive(root, r, zip, cancel, fixtureInspector);
    ASSERT_TRUE(newer.success) << newer.message; EXPECT_NE(newer.bundle, old.bundle);
    EXPECT_EQ(old.bundle.getChildFile("Contents/x86_64-win/DirectPipe Windows Input Mixer.vst3").loadFileAsString(), "old");
    EXPECT_EQ(installedCopy(root, fixtureInspector).version, "0.3.5");
}

TEST_F(DpwimInstallerTest, BadChecksumTraversalAndCaseDuplicatesCannotChangePreviousInstall)
{
    const auto good = zipOf({{binary, "good"}});
    ASSERT_TRUE(installArchive(root, releaseFor(good), good, cancel, fixtureInspector).success);
    const auto before = root.getChildFile("installed.json").loadFileAsString();
    auto wrong = releaseFor(good); wrong.sha256 = juce::String::repeatedString("0", 64);
    EXPECT_FALSE(installArchive(root, wrong, good, cancel, fixtureInspector).success);
    for (const auto& entry : {"../escape.dll", "C:/escape.dll", "DirectPipe Windows Input Mixer.vst3/../escape", "DirectPipe Windows Input Mixer.vst3/CON", "DirectPipe Windows Input Mixer.vst3/name:ads", "DirectPipe Windows Input Mixer.vst3/name. "}) {
        const auto zip = zipOf({{binary, "evil"}, {entry, "bad"}});
        EXPECT_FALSE(installArchive(root, releaseFor(zip), zip, cancel, fixtureInspector).success) << entry;
    }
    const auto duplicate = zipOf({{binary, "one"}, {binary.toUpperCase(), "two"}});
    EXPECT_FALSE(installArchive(root, releaseFor(duplicate), duplicate, cancel, fixtureInspector).success);
    EXPECT_EQ(root.getChildFile("installed.json").loadFileAsString(), before);
    EXPECT_FALSE(root.getSiblingFile("escape.dll").exists());
    cancel.store(true);
    EXPECT_FALSE(installArchive(root, releaseFor(good), good, cancel, fixtureInspector).success);
    EXPECT_EQ(root.getChildFile("installed.json").loadFileAsString(), before);
}

TEST_F(DpwimInstallerTest, EqualAndNewerInstallAvoidDownloadsAndDowngrades)
{
    const auto zip = zipOf({{binary, "current"}}); const auto r = releaseFor(zip, "0.3.5");
    ASSERT_TRUE(installArchive(root, r, zip, cancel, fixtureInspector).success);
    const auto before = root.getChildFile("installed.json").loadFileAsString();
    for (const auto& offered : {"0.3.5", "0.3.4"}) {
        int calls = 0; auto latest = releaseFor(zip, offered);
        Fetch fetch = [&](const juce::String& url, juce::MemoryBlock& bytes, int64_t, const std::atomic<bool>&) {
            ++calls; EXPECT_TRUE(url.endsWith("/releases/latest"));
            const auto json = juce::JSON::toString(releaseJson(latest));
            bytes.replaceAll(json.toRawUTF8(), json.getNumBytesAsUTF8()); return juce::Result::ok();
        };
        const auto result = installLatest(root, fetch, cancel, fixtureInspector);
        EXPECT_TRUE(result.success); EXPECT_TRUE(result.unchanged); EXPECT_EQ(calls, 1);
        EXPECT_EQ(root.getChildFile("installed.json").loadFileAsString(), before);
    }
}

TEST_F(DpwimInstallerTest, NewerInstallPublishedDuringDownloadCannotBeDowngraded)
{
    const auto originalZip = zipOf({{binary, "original"}});
    ASSERT_TRUE(installArchive(root, releaseFor(originalZip, "0.3.3"), originalZip, cancel, fixtureInspector).success);
    const auto offeredZip = zipOf({{binary, "offered"}});
    const auto offered = releaseFor(offeredZip, "0.3.4");
    const auto newerZip = zipOf({{binary, "newer"}});
    juce::String newerManifest;
    int calls = 0;
    Fetch fetch = [&](const juce::String& url, juce::MemoryBlock& bytes, int64_t, const std::atomic<bool>&) {
        ++calls;
        if (url.endsWith("/releases/latest")) {
            const auto json = juce::JSON::toString(releaseJson(offered));
            bytes.replaceAll(json.toRawUTF8(), json.getNumBytesAsUTF8());
        } else {
            EXPECT_EQ(url, offered.url);
            const auto newer = installArchive(root, releaseFor(newerZip, "0.3.5"), newerZip, cancel, fixtureInspector);
            EXPECT_TRUE(newer.success) << newer.message;
            if (!newer.success) return juce::Result::fail("Fixture failed to publish the newer installation");
            newerManifest = root.getChildFile("installed.json").loadFileAsString();
            bytes = offeredZip;
        }
        return juce::Result::ok();
    };
    const auto result = installLatest(root, fetch, cancel, fixtureInspector);
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_TRUE(result.unchanged);
    EXPECT_EQ(result.version, "0.3.5");
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(installedCopy(root, fixtureInspector).version, "0.3.5");
    EXPECT_EQ(root.getChildFile("installed.json").loadFileAsString(), newerManifest);
}

TEST_F(DpwimInstallerTest, TargetedScanRetainsExternalCacheAndRetiresOnlySameManagedIdentity)
{
    const auto makeDescription = [](const juce::String& path, const juce::String& name, int identity) {
        juce::PluginDescription description;
        description.name = name;
        description.manufacturerName = "LiveTrack";
        description.pluginFormatName = "VST3";
        description.fileOrIdentifier = path;
        description.uniqueId = identity;
        description.deprecatedUid = identity;
        return description;
    };
    const auto name = juce::String("DirectPipe Windows Input Mixer");
    const auto old = makeDescription(root.getChildFile("v0.3.3-old").getChildFile(module).getFullPathName(), name, 123);
    const auto fresh = makeDescription(root.getChildFile("v0.3.4-new").getChildFile(module).getFullPathName(), name, 123);
    const auto newerCached = makeDescription(root.getChildFile("v0.3.5-cached").getChildFile(module).getFullPathName(), name, 123);
    const auto external = makeDescription(root.getParentDirectory().getChildFile("external-dpwim.vst3").getFullPathName(), name, 123);
    const auto unrelated = makeDescription(root.getChildFile("some-other-plugin.vst3").getFullPathName(), "External EQ", 456);
    const auto otherIdentity = makeDescription(root.getChildFile("v0.2.0-other").getChildFile(module).getFullPathName(), name, 789);
    juce::KnownPluginList cached, discovered;
    for (const auto& description : {old, external, unrelated, otherIdentity, newerCached}) cached.addType(description);
    cached.addToBlacklist("keep-existing-blacklist");
    discovered.addType(fresh);
    const auto resultXml = discovered.createXml();
    ASSERT_NE(resultXml, nullptr);

    // Intermediate targeted output cannot erase either existing cache or old versions.
    directpipe::plugin_scan::applyResults(cached, *resultXml, true);
    EXPECT_EQ(cached.getNumTypes(), 6);
    EXPECT_TRUE(cached.getTypeForFile(old.fileOrIdentifier));
    // Successful final output prunes the matching managed cache entry only.
    directpipe::plugin_scan::applyResults(cached, *resultXml, true, root);
    EXPECT_EQ(cached.getNumTypes(), 5);
    EXPECT_FALSE(cached.getTypeForFile(old.fileOrIdentifier));
    EXPECT_TRUE(cached.getTypeForFile(fresh.fileOrIdentifier));
    EXPECT_TRUE(cached.getTypeForFile(external.fileOrIdentifier));
    EXPECT_TRUE(cached.getTypeForFile(unrelated.fileOrIdentifier));
    EXPECT_TRUE(cached.getTypeForFile(otherIdentity.fileOrIdentifier));
    EXPECT_TRUE(cached.getTypeForFile(newerCached.fileOrIdentifier));
    EXPECT_TRUE(cached.getBlacklistedFiles().contains("keep-existing-blacklist"));
    juce::KnownPluginList reloaded;
    const auto saved = cached.createXml();
    reloaded.recreateFromXml(*saved);
    EXPECT_EQ(reloaded.getNumTypes(), 5);
    EXPECT_TRUE(reloaded.getTypeForFile(external.fileOrIdentifier));
    // An explicit full rescan keeps the original replacement behavior.
    directpipe::plugin_scan::applyResults(reloaded, *resultXml, false);
    EXPECT_EQ(reloaded.getNumTypes(), 1);
    EXPECT_TRUE(reloaded.getTypeForFile(fresh.fileOrIdentifier));
}

TEST_F(DpwimInstallerTest, FailedNetworkAndInvalidManifestLeaveExistingFilesUntouched)
{
    const auto zip = zipOf({{binary, "good"}});
    ASSERT_TRUE(installArchive(root, releaseFor(zip), zip, cancel, fixtureInspector).success);
    const auto before = root.getChildFile("installed.json").loadFileAsString();
    Fetch noNetwork = [](const juce::String&, juce::MemoryBlock&, int64_t, const std::atomic<bool>&) { return juce::Result::fail("offline"); };
    EXPECT_FALSE(installLatest(root, noNetwork, cancel).success);
    EXPECT_EQ(root.getChildFile("installed.json").loadFileAsString(), before);
    ASSERT_TRUE(root.getChildFile("installed.json").replaceWithText(R"({"version":"0.3.4","directory":"../../outside"})"));
    EXPECT_FALSE(installedCopy(root, fixtureInspector).success);
    EXPECT_FALSE(inspectBundle(root, "0.3.4"));
}

TEST_F(DpwimInstallerTest, DestroyingOwnerCancelsWorkerWithoutCallingDeadUi)
{
    auto finished = std::make_shared<std::atomic<bool>>(false);
    auto started = std::make_shared<std::atomic<bool>>(false);
    std::atomic<int> callbacks{0};
    Fetch fetch = [finished, started](const juce::String&, juce::MemoryBlock&, int64_t, const std::atomic<bool>& cancelled) {
        started->store(true);
        while (!cancelled.load()) std::this_thread::yield();
        finished->store(true); return juce::Result::fail("cancelled");
    };
    {
        Installer installer;
        ASSERT_TRUE(installer.start(root, [&](Outcome) { ++callbacks; }, fetch));
        EXPECT_FALSE(installer.start(root, [](Outcome) {}, fetch));
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
        while (!started->load() && juce::Time::getMillisecondCounterHiRes() < deadline) std::this_thread::yield();
        ASSERT_TRUE(started->load());
    }
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
    while (!finished->load() && juce::Time::getMillisecondCounterHiRes() < deadline) std::this_thread::yield();
    EXPECT_TRUE(finished->load()); EXPECT_EQ(callbacks.load(), 0);
}

TEST_F(DpwimInstallerTest, PublishedArchivePassesRealIdentityAndInstallation)
{
    const auto archivePath = juce::SystemStats::getEnvironmentVariable("DIRECTPIPE_DPWIM_TEST_ARCHIVE", {});
    if (archivePath.isEmpty()) GTEST_SKIP() << "Optional published DPWIM ZIP fixture not provided";
    juce::MemoryBlock bytes; ASSERT_TRUE(juce::File(archivePath).loadFileAsData(bytes));
    ASSERT_EQ(juce::SHA256(bytes).toHexString(), "b695f2af9625beca43f83e25c4496c575b76c85dc4c0c85cb3a855b9a9531e6c");
    const auto result = installArchive(root, releaseFor(bytes), bytes, cancel);
    ASSERT_TRUE(result.success) << result.message;
    const auto installed = installedCopy(root);
    EXPECT_TRUE(installed.success); EXPECT_EQ(installed.version, "0.3.4");
}

TEST_F(DpwimInstallerTest, OnlineStableReleaseInstallsIntoOwnedFixtureOnly)
{
    if (juce::SystemStats::getEnvironmentVariable("DIRECTPIPE_DPWIM_ONLINE_SMOKE", {}) != "1")
        GTEST_SKIP() << "Online installer smoke is explicitly opt-in (enabled in Windows CI)";
    const auto result = installLatest(root, fetchHttps, cancel);
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_TRUE(validVersion(result.version));
    EXPECT_TRUE(installedCopy(root).success);
    EXPECT_TRUE(result.bundle.isAChildOf(root));
}
