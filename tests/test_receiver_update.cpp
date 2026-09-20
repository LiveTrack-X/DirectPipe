// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#include <JuceHeader.h>
#include <gtest/gtest.h>

#include "UI/ReceiverUpdateSupport.h"
#include "UI/UpdateScript.h"

#include <algorithm>
#include <cstring>

#if JUCE_WINDOWS
#include <Windows.h>
#endif

namespace {
using namespace directpipe::update_detail;

// Discovery and installer fixtures use private UUID temp trees. Compiled binaries
// are copied as file-format evidence; generated scripts replace only fixture
// destinations. Historical version literals exercise ordering, not release state.
// These checks do not launch OBS, request UAC, install into VST folders, or prove
// that a real audio application reloads the new Receiver successfully.

class ReceiverDiscoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("DirectPipe receiver discovery " + juce::Uuid().toString());
        ASSERT_TRUE(root.createDirectory());
    }
    void TearDown() override { root.deleteRecursively(); }
    juce::File root;
};

TEST(ReceiverVersionPolicyTest, OnlyStrictlyOlderKnownVersionsAreEligible)
{
    EXPECT_TRUE(receiverNeedsUpdate("4.2.9", "v4.3.0"));
    EXPECT_TRUE(receiverNeedsUpdate("4.3.9", "4.3.10"));
    EXPECT_FALSE(receiverNeedsUpdate("4.3.0", "4.3.0"));
    EXPECT_FALSE(receiverNeedsUpdate("4.4.0", "4.3.0"));
    EXPECT_FALSE(receiverNeedsUpdate("unknown", "4.3.0"));
    EXPECT_FALSE(receiverNeedsUpdate("4.2.0", "4.3.0; exit"));
}

TEST_F(ReceiverDiscoveryTest, FindsCustomVendorLocationsDeduplicatesAndSkipsBuildArtifacts)
{
    for (const auto* relative : { "Vendor/DirectPipe Receiver.dll", "build/DirectPipe Receiver.dll",
                                  "build-local/DirectPipe Receiver.dll", "dist/DirectPipe Receiver.dll",
                                  ".dp-s-fixture/DirectPipe Receiver.dll", ".dp-p-fixture/DirectPipe Receiver.dll" }) {
        const auto file = root.getChildFile(relative);
        ASSERT_TRUE(file.getParentDirectory().createDirectory());
        ASSERT_TRUE(file.replaceWithText("fixture"));
    }
    ASSERT_TRUE(root.getChildFile("Vendor/DirectPipe Receiver.vst3/Contents/x86_64-win").createDirectory());
    int inspected = 0;
    const auto result = discoverReceiversInDirectories({root, root.getChildFile("Vendor")},
        [&](const juce::File& path, ReceiverInstallTarget& target, juce::String&) {
            ++inspected;
            target.installPath = path.getFullPathName();
            target.installedVersion = "4.2.0";
            return true;
        });
    EXPECT_EQ(2, inspected);
    EXPECT_EQ(2u, result.targets.size());
    EXPECT_TRUE(result.warnings.isEmpty());
}

TEST_F(ReceiverDiscoveryTest, ReportsUninspectableCandidateWithoutInventingInstallation)
{
    ASSERT_TRUE(root.getChildFile("DirectPipe Receiver.dll").replaceWithText("not a plug-in"));
    const auto result = discoverReceiversInDirectories({root});
    EXPECT_TRUE(result.targets.empty());
    EXPECT_FALSE(result.warnings.isEmpty());
}

TEST_F(ReceiverDiscoveryTest, DepthLimitReportsThatSearchWasIncomplete)
{
    ASSERT_TRUE(root.getChildFile("a/b/c/d/e/f/g/h").createDirectory());
    const auto result = discoverReceiversInDirectories({root});
    EXPECT_TRUE(result.targets.empty());
    EXPECT_FALSE(result.warnings.isEmpty());
}

TEST(CompanionUpdatePlanTest, RejectsUnsafeVersionsMissingIntegrityAndNewerTargets)
{
    WindowsUpdateInstallSpec spec;
    spec.isZip = true;
    spec.skipHostUpdate = true;
    spec.downloadedFilePath = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("DirectPipe.zip").getFullPathName();
    spec.resultFilePath = juce::File(spec.downloadedFilePath).getSiblingFile("result.json").getFullPathName();
    const auto path = juce::File(spec.downloadedFilePath).getSiblingFile("DirectPipe Receiver.dll").getFullPathName();
    spec.receiverTargets = {{ReceiverFormat::vst2, path, path, "4.2.0", false}};
    spec.expectedVersion = "4.3.0";
    EXPECT_TRUE(buildWindowsCompanionInstallPowerShell(spec).isEmpty());
    spec.expectedPackageSha256 = juce::String::repeatedString("ab", 32);
    EXPECT_FALSE(buildWindowsCompanionInstallPowerShell(spec).isEmpty());
    spec.receiverTargets[0].installedVersion = "4.3.0";
    EXPECT_TRUE(buildWindowsCompanionInstallPowerShell(spec).isEmpty());
    spec.receiverTargets[0].installedVersion = "4.4.0";
    EXPECT_TRUE(buildWindowsCompanionInstallPowerShell(spec).isEmpty());
    spec.receiverTargets[0].installedVersion = "4.2.0";
    spec.expectedVersion = "4.3.0\nStart-Process anything";
    EXPECT_TRUE(buildWindowsCompanionInstallPowerShell(spec).isEmpty());
}

#if JUCE_WINDOWS && defined(DIRECTPIPE_TEST_RECEIVER_VST2_PATH) && defined(DIRECTPIPE_TEST_RECEIVER_VST3_PATH) && defined(DIRECTPIPE_TEST_HOST_PATH)

struct PowerShellResult { bool started = false; bool finished = false; int exitCode = -1; juce::String output; };
PowerShellResult runPowerShell(const juce::File& scriptFile, const juce::String& text)
{
    PowerShellResult result;
    if (!scriptFile.replaceWithText(text, false, true)) return result;
    juce::ChildProcess process;
    result.started = process.start(juce::StringArray{"powershell.exe", "-NoProfile", "-NonInteractive",
        "-ExecutionPolicy", "Bypass", "-File", scriptFile.getFullPathName()});
    if (!result.started) return result;
    result.finished = process.waitForProcessToFinish(90000);
    if (!result.finished) { process.kill(); process.waitForProcessToFinish(5000); }
    result.output = process.readAllProcessOutput();
    result.exitCode = process.getExitCode();
    return result;
}

juce::String literal(const juce::File& file) { return "'" + file.getFullPathName().replace("'", "''") + "'"; }

// Mutate only a private fixture resource. No executable code, installed files,
// production IPC, running host or plug-in installation is touched by the tests.
bool makeOlderFixture(const juce::File& file, const juce::String& original, const juce::String& older)
{
    if (original.length() != older.length()) return false;
    const auto path = file.getFullPathName();
    HMODULE module = LoadLibraryExW(path.toWideCharPointer(), nullptr,
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module) return false;
    std::vector<WORD> languages;
    EnumResourceLanguagesW(module, MAKEINTRESOURCEW(16), MAKEINTRESOURCEW(1),
        [](HMODULE, LPCWSTR, LPCWSTR, WORD language, LONG_PTR data) -> BOOL {
            reinterpret_cast<std::vector<WORD>*>(data)->push_back(language); return TRUE;
        }, reinterpret_cast<LONG_PTR>(&languages));
    const WORD language = languages.empty() ? 0 : languages.front();
    const auto resource = FindResourceExW(module, MAKEINTRESOURCEW(16), MAKEINTRESOURCEW(1), language);
    const auto size = resource ? SizeofResource(module, resource) : 0;
    const auto loaded = resource ? LoadResource(module, resource) : nullptr;
    const auto* data = loaded ? static_cast<const unsigned char*>(LockResource(loaded)) : nullptr;
    std::vector<unsigned char> bytes;
    if (data && size) bytes.assign(data, data + size);
    FreeLibrary(module);
    if (bytes.empty()) return false;
    const auto oldText = original.toWideCharPointer();
    const auto newText = older.toWideCharPointer();
    const auto textBytes = static_cast<size_t>(original.length()) * sizeof(wchar_t);
    int stringsChanged = 0;
    for (size_t i = 0; i + textBytes <= bytes.size(); i += 2) {
        if (std::memcmp(bytes.data() + i, oldText, textBytes) == 0) {
            std::memcpy(bytes.data() + i, newText, textBytes); ++stringsChanged;
        }
    }
    std::array<int, 3> version{}; juce::String canonical;
    if (stringsChanged < 2 || !parseStrictReleaseVersion(older, version, canonical)) return false;
    for (size_t i = 0; i + sizeof(VS_FIXEDFILEINFO) <= bytes.size(); i += 4) {
        VS_FIXEDFILEINFO info{}; std::memcpy(&info, bytes.data() + i, sizeof(info));
        if (info.dwSignature != VS_FFI_SIGNATURE) continue;
        info.dwFileVersionMS = info.dwProductVersionMS = MAKELONG(version[1], version[0]);
        info.dwFileVersionLS = info.dwProductVersionLS = MAKELONG(0, version[2]);
        std::memcpy(bytes.data() + i, &info, sizeof(info));
        break;
    }
    HANDLE update = BeginUpdateResourceW(path.toWideCharPointer(), FALSE);
    if (!update) return false;
    const bool wrote = UpdateResourceW(update, MAKEINTRESOURCEW(16), MAKEINTRESOURCEW(1), language,
        bytes.data(), static_cast<DWORD>(bytes.size())) != FALSE;
    return EndUpdateResourceW(update, wrote ? FALSE : TRUE) != FALSE && wrote;
}

class ReceiverTransactionTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("DP O'Brien %$& " + juce::Uuid().toString());
        ASSERT_TRUE(root.createDirectory());
        const juce::File vst2(DIRECTPIPE_TEST_RECEIVER_VST2_PATH), vst3(DIRECTPIPE_TEST_RECEIVER_VST3_PATH), host(DIRECTPIPE_TEST_HOST_PATH);
        ASSERT_TRUE(vst2.existsAsFile()) << vst2.getFullPathName();
        ASSERT_TRUE(vst3.existsAsFile()) << vst3.getFullPathName();
        ASSERT_TRUE(host.existsAsFile()) << host.getFullPathName();
        package = root.getChildFile("package"); ASSERT_TRUE(package.createDirectory());
        ASSERT_TRUE(vst2.copyFileTo(package.getChildFile("DirectPipe Receiver.dll")));
        const auto sourceBundle = vst3.getParentDirectory().getParentDirectory().getParentDirectory();
        ASSERT_TRUE(sourceBundle.copyDirectoryTo(package.getChildFile("DirectPipe Receiver.vst3")));
        ASSERT_TRUE(host.copyFileTo(package.getChildFile("DirectPipe.exe")));
        ReceiverInstallTarget source; juce::String error;
        ASSERT_TRUE(inspectReceiverTarget(package.getChildFile("DirectPipe Receiver.dll"), source, error)) << error;
        version = source.installedVersion;
        std::array<int, 3> parts{}; juce::String canonical;
        ASSERT_TRUE(parseStrictReleaseVersion(version, parts, canonical));
        ASSERT_GT(parts[0], 0);
        older = juce::String(parts[0] - 1) + "." + juce::String(parts[1]) + "." + juce::String(parts[2]);
        ASSERT_EQ(version.length(), older.length());
        installed = root.getChildFile("installed"); ASSERT_TRUE(installed.createDirectory());
        ASSERT_TRUE(package.getChildFile("DirectPipe Receiver.dll").copyFileTo(installed.getChildFile("DirectPipe Receiver.dll")));
        ASSERT_TRUE(package.getChildFile("DirectPipe Receiver.vst3").copyDirectoryTo(installed.getChildFile("DirectPipe Receiver.vst3")));
        ASSERT_TRUE(package.getChildFile("DirectPipe.exe").copyFileTo(installed.getChildFile("DirectPipe.exe")));
        ASSERT_TRUE(makeOlderFixture(installed.getChildFile("DirectPipe Receiver.dll"), version, older));
        ASSERT_TRUE(makeOlderFixture(installed.getChildFile("DirectPipe Receiver.vst3/Contents/x86_64-win/DirectPipe Receiver.vst3"), version, older));
        ASSERT_TRUE(makeOlderFixture(installed.getChildFile("DirectPipe.exe"), version, older));
        spec.currentExePath = installed.getChildFile("DirectPipe.exe").getFullPathName();
        spec.backupExePath = installed.getChildFile("DirectPipe_backup.exe").getFullPathName();
        spec.downloadedFilePath = root.getChildFile("DirectPipe.zip").getFullPathName();
        spec.resultFilePath = root.getChildFile("result.json").getFullPathName();
        spec.updatedFlagPath = root.getChildFile("updated.flag").getFullPathName();
        spec.expectedVersion = version; spec.isZip = true; spec.skipHostUpdate = true;
        for (const auto* name : { "DirectPipe Receiver.dll", "DirectPipe Receiver.vst3" }) {
            ReceiverInstallTarget target;
            ASSERT_TRUE(inspectReceiverTarget(installed.getChildFile(name), target, error)) << error;
            spec.receiverTargets.push_back(target);
        }
        makeArchive();
    }
    void TearDown() override { root.deleteRecursively(); }
    void makeArchive() {
        const juce::File zip(spec.downloadedFilePath);
        ASSERT_TRUE(!zip.exists() || zip.deleteFile());
        const auto run = runPowerShell(root.getChildFile("zip.ps1"),
            "Add-Type -AssemblyName System.IO.Compression.FileSystem\n[IO.Compression.ZipFile]::CreateFromDirectory(" + literal(package) + ", " + literal(zip) + ")\n");
        ASSERT_TRUE(run.started && run.finished); ASSERT_EQ(0, run.exitCode) << run.output;
        juce::FileInputStream stream(zip); ASSERT_TRUE(stream.openedOk());
        spec.expectedPackageSha256 = juce::SHA256(stream).toHexString();
    }
    PowerShellResult install(const juce::String& replacement = {}) {
        auto script = buildWindowsCompanionInstallPowerShell(spec);
        EXPECT_FALSE(script.isEmpty());
        if (replacement.isNotEmpty()) script = script.replace("# DIRECTPIPE_TRANSACTION_INSTALLED", replacement);
        return runPowerShell(root.getChildFile("install.ps1"), script);
    }
    PowerShellResult preflight() {
        auto check = spec;
        check.resultFilePath = root.getChildFile("preflight-result.json").getFullPathName();
        return runPowerShell(root.getChildFile("preflight.ps1"),
            buildWindowsCompanionInstallPowerShell(check, true));
    }
    juce::var preflightResult() { return juce::JSON::parse(root.getChildFile("preflight-result.json").loadFileAsString()); }
    void useModulePathLength(int length) {
        const juce::String relative = "DirectPipe Receiver.vst3/Contents/x86_64-win/DirectPipe Receiver.vst3";
        const auto padding = length - root.getFullPathName().length() - relative.length() - 2;
        ASSERT_GT(padding, 0);
        const auto destination = root.getChildFile(juce::String::repeatedString("d", padding));
        ASSERT_TRUE(installed.moveFileTo(destination));
        installed = destination;
        spec.currentExePath = installed.getChildFile("DirectPipe.exe").getFullPathName();
        spec.backupExePath = installed.getChildFile("DirectPipe_backup.exe").getFullPathName();
        spec.receiverTargets.clear();
        for (const auto* name : { "DirectPipe Receiver.dll", "DirectPipe Receiver.vst3" }) {
            ReceiverInstallTarget target; juce::String error;
            ASSERT_TRUE(inspectReceiverTarget(installed.getChildFile(name), target, error)) << error;
            spec.receiverTargets.push_back(target);
        }
        ASSERT_EQ(length, spec.receiverTargets.back().binaryPath.length());
    }
    juce::File childWithPathLength(const juce::File& parent, int length) {
        const auto count = length - parent.getFullPathName().length() - 1;
        EXPECT_GT(count, 0);
        return parent.getChildFile(juce::String::repeatedString("r", juce::jmax(1, count)));
    }
    juce::var result() { return juce::JSON::parse(juce::File(spec.resultFilePath).loadFileAsString()); }
    juce::String installedVersion(const juce::String& name) {
        ReceiverInstallTarget target; juce::String error;
        EXPECT_TRUE(inspectReceiverTarget(installed.getChildFile(name), target, error)) << error;
        return target.installedVersion;
    }
    juce::File root, package, installed;
    juce::String version, older;
    WindowsUpdateInstallSpec spec;
};

TEST_F(ReceiverTransactionTest, PathPreflightDoesNotWaitForOrReplaceRunningHost)
{
    spec.skipHostUpdate = false;
    spec.processId = GetCurrentProcessId(); // Only the running test process; preflight must not wait.
    spec.relaunchHostAfterUpdate = true;    // Must not launch the copied fixture host, either.
    const auto hostHash = juce::SHA256(juce::File(spec.currentExePath)).toHexString();
    const auto run = preflight();
    ASSERT_TRUE(run.started && run.finished); ASSERT_EQ(0, run.exitCode) << run.output;
    EXPECT_TRUE(static_cast<bool>(preflightResult()["success"]));
    EXPECT_FALSE(static_cast<bool>(preflightResult()["hostUpdated"]));
    EXPECT_EQ(hostHash, juce::SHA256(juce::File(spec.currentExePath)).toHexString());
    EXPECT_EQ(older, installedVersion("DirectPipe Receiver.vst3"));
    EXPECT_FALSE(juce::File(spec.backupExePath).exists());
    EXPECT_FALSE(juce::File(spec.updatedFlagPath).exists());
    EXPECT_FALSE(juce::File(spec.resultFilePath).exists());
    EXPECT_TRUE(installed.findChildFiles(juce::File::findFilesAndDirectories, false, ".dp-*").isEmpty());
    EXPECT_TRUE(root.findChildFiles(juce::File::findDirectories, false, "directpipe-stage-*").isEmpty());
}

TEST_F(ReceiverTransactionTest, LongVst3PathUpdatesAndRotatesExistingBackup)
{
    useModulePathLength(232); ASSERT_FALSE(HasFatalFailure());
    spec.skipHostUpdate = false;
    const auto bundle = installed.getChildFile("DirectPipe Receiver.vst3");
    const auto backup = installed.getChildFile("DirectPipe Receiver.vst3.directpipe-backup");
    ASSERT_TRUE(bundle.copyDirectoryTo(backup));
    ASSERT_TRUE(backup.getChildFile("prior.txt").replaceWithText("previous backup"));
    ASSERT_TRUE(bundle.getChildFile("old.txt").replaceWithText("current bundle"));
    const auto checked = preflight();
    ASSERT_TRUE(checked.started && checked.finished); ASSERT_EQ(0, checked.exitCode) << checked.output;
    const auto run = install();
    ASSERT_TRUE(run.started && run.finished); ASSERT_EQ(0, run.exitCode) << run.output;
    EXPECT_TRUE(static_cast<bool>(result()["success"]));
    EXPECT_TRUE(static_cast<bool>(result()["hostUpdated"]));
    EXPECT_EQ(version, installedVersion("DirectPipe Receiver.vst3"));
    EXPECT_EQ(juce::String("current bundle"), backup.getChildFile("old.txt").loadFileAsString());
    EXPECT_FALSE(backup.getChildFile("prior.txt").exists());
    EXPECT_TRUE(installed.findChildFiles(juce::File::findFilesAndDirectories, false, ".dp-*").isEmpty());
}

TEST_F(ReceiverTransactionTest, UnsupportedGeneratedPathFailsBeforeHostWaitOrWrites)
{
    useModulePathLength(246); ASSERT_FALSE(HasFatalFailure());
    spec.skipHostUpdate = false;
    spec.processId = GetCurrentProcessId();
    const auto hostHash = juce::SHA256(juce::File(spec.currentExePath)).toHexString();
    const auto checked = preflight();
    ASSERT_TRUE(checked.started && checked.finished); EXPECT_NE(0, checked.exitCode) << checked.output;
    EXPECT_TRUE(preflightResult()["error"].toString().contains("Update path is too long")) << checked.output;
    // The actual installer repeats preflight before reaching its host wait.
    const auto run = install();
    ASSERT_TRUE(run.started && run.finished); EXPECT_NE(0, run.exitCode) << run.output;
    EXPECT_TRUE(result()["error"].toString().contains("Update path is too long")) << run.output;
    EXPECT_EQ(hostHash, juce::SHA256(juce::File(spec.currentExePath)).toHexString());
    EXPECT_EQ(older, installedVersion("DirectPipe Receiver.vst3"));
    EXPECT_FALSE(juce::File(spec.backupExePath).exists());
    EXPECT_FALSE(juce::File(spec.updatedFlagPath).exists());
    EXPECT_TRUE(installed.findChildFiles(juce::File::findFilesAndDirectories, false, ".dp-*").isEmpty());
}

TEST_F(ReceiverTransactionTest, PreflightChecksEveryCandidateBundleResourcePath)
{
    useModulePathLength(232); ASSERT_FALSE(HasFatalFailure());
    const auto destinationBundle = installed.getChildFile("DirectPipe Receiver.vst3");
    const auto tooLong = childWithPathLength(destinationBundle, 260);
    const auto resource = package.getChildFile("DirectPipe Receiver.vst3").getChildFile(tooLong.getFileName());
    ASSERT_TRUE(resource.replaceWithText("candidate resource with a long destination"));
    makeArchive(); ASSERT_FALSE(HasFatalFailure());
    const auto run = preflight();
    ASSERT_TRUE(run.started && run.finished); EXPECT_NE(0, run.exitCode) << run.output;
    EXPECT_TRUE(preflightResult()["error"].toString().contains("Update path is too long")) << run.output;
    EXPECT_EQ(older, installedVersion("DirectPipe Receiver.vst3"));
    EXPECT_FALSE(juce::File(spec.resultFilePath).exists());
}

TEST_F(ReceiverTransactionTest, PreflightChecksCurrentResourcesAtTheirFutureBackupPaths)
{
    useModulePathLength(232); ASSERT_FALSE(HasFatalFailure());
    const auto checked = preflight();
    ASSERT_TRUE(checked.started && checked.finished); ASSERT_EQ(0, checked.exitCode) << checked.output;
    const auto resource = childWithPathLength(installed.getChildFile("DirectPipe Receiver.vst3"), 242);
    ASSERT_TRUE(resource.replaceWithText("old resource would reach 260 in backup"));
    spec.skipHostUpdate = false;
    spec.processId = GetCurrentProcessId();
    const auto hostHash = juce::SHA256(juce::File(spec.currentExePath)).toHexString();
    const auto run = install(); // Resource appeared after the UI preflight, before helper launch.
    ASSERT_TRUE(run.started && run.finished); EXPECT_NE(0, run.exitCode) << run.output;
    EXPECT_TRUE(result()["error"].toString().contains("Update path is too long")) << run.output;
    EXPECT_EQ(hostHash, juce::SHA256(juce::File(spec.currentExePath)).toHexString());
    EXPECT_EQ(juce::String("old resource would reach 260 in backup"), resource.loadFileAsString());
    EXPECT_EQ(older, installedVersion("DirectPipe Receiver.vst3"));
    EXPECT_FALSE(juce::File(spec.backupExePath).exists());
}

TEST_F(ReceiverTransactionTest, PreflightChecksAllExistingBackupDescendants)
{
    useModulePathLength(232); ASSERT_FALSE(HasFatalFailure());
    const auto backup = installed.getChildFile("DirectPipe Receiver.vst3.directpipe-backup");
    ASSERT_TRUE(backup.createDirectory());
    const auto resource = childWithPathLength(backup, 260);
    // Explicit extended path creates only this private fixture. Normal Get-ChildItem
    // can enumerate it; the updater must reject it before a Restart Manager call.
    const auto extended = "\\\\?\\" + resource.getFullPathName();
    HANDLE file = CreateFileW(extended.toWideCharPointer(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(INVALID_HANDLE_VALUE, file);
    CloseHandle(file);
    const auto run = preflight();
    const bool removed = DeleteFileW(extended.toWideCharPointer()) != FALSE;
    EXPECT_TRUE(removed);
    ASSERT_TRUE(run.started && run.finished); EXPECT_NE(0, run.exitCode) << run.output;
    EXPECT_TRUE(preflightResult()["error"].toString().contains("Update path is too long")) << run.output;
    EXPECT_EQ(older, installedVersion("DirectPipe Receiver.vst3"));
    EXPECT_FALSE(juce::File(spec.resultFilePath).exists());
}

TEST_F(ReceiverTransactionTest, DiscoveryValidatesProductAndActualFormatExports)
{
    const auto discovered = discoverReceiversInDirectories({ installed });
    ASSERT_EQ(2u, discovered.targets.size()); EXPECT_TRUE(discovered.warnings.isEmpty());
    ASSERT_TRUE(package.getChildFile("DirectPipe.exe").copyFileTo(installed.getChildFile("DirectPipe Receiver.dll")));
    ReceiverInstallTarget target; juce::String error;
    EXPECT_FALSE(inspectReceiverTarget(installed.getChildFile("DirectPipe Receiver.dll"), target, error));
}

TEST_F(ReceiverTransactionTest, ReceiverOnlyUpdatesFullBundleAndKeepsHostAndBackups)
{
    ASSERT_TRUE(installed.getChildFile("DirectPipe Receiver.vst3/old-file.txt").replaceWithText("obsolete"));
    ASSERT_TRUE(package.getChildFile("DirectPipe Receiver.vst3/new-file.txt").replaceWithText("new resource"));
    makeArchive();
    const auto hostHash = juce::SHA256(juce::File(spec.currentExePath)).toHexString();
    const auto run = install(); ASSERT_TRUE(run.started && run.finished); ASSERT_EQ(0, run.exitCode) << run.output;
    EXPECT_TRUE(static_cast<bool>(result()["success"])); EXPECT_FALSE(static_cast<bool>(result()["hostUpdated"]));
    EXPECT_EQ(version, installedVersion("DirectPipe Receiver.dll"));
    EXPECT_EQ(version, installedVersion("DirectPipe Receiver.vst3"));
    EXPECT_TRUE(installed.getChildFile("DirectPipe Receiver.vst3/new-file.txt").existsAsFile());
    EXPECT_FALSE(installed.getChildFile("DirectPipe Receiver.vst3/old-file.txt").exists());
    EXPECT_TRUE(installed.getChildFile("DirectPipe Receiver.vst3.directpipe-backup/old-file.txt").existsAsFile());
    EXPECT_EQ(hostHash, juce::SHA256(juce::File(spec.currentExePath)).toHexString());
    EXPECT_FALSE(juce::File(spec.updatedFlagPath).exists());
}

TEST_F(ReceiverTransactionTest, PartialInstallFailureRestoresEveryTargetAndPreviousBackup)
{
    spec.skipHostUpdate = false;
    ASSERT_TRUE(juce::File(spec.backupExePath).replaceWithText("prior known good backup"));
    const auto hostHash = juce::SHA256(juce::File(spec.currentExePath)).toHexString();
    const auto run = install("if ($entry.format -eq 'vst2') { throw 'Injected fixture failure after a host and Receiver replacement' }");
    ASSERT_TRUE(run.started && run.finished); EXPECT_NE(0, run.exitCode) << run.output;
    EXPECT_FALSE(static_cast<bool>(result()["success"])); EXPECT_TRUE(static_cast<bool>(result()["rollbackSucceeded"]));
    EXPECT_TRUE(result()["error"].toString().contains("Injected fixture failure")) << run.output;
    EXPECT_EQ(older, installedVersion("DirectPipe Receiver.dll"));
    EXPECT_EQ(older, installedVersion("DirectPipe Receiver.vst3"));
    EXPECT_EQ(hostHash, juce::SHA256(juce::File(spec.currentExePath)).toHexString());
    EXPECT_EQ(juce::String("prior known good backup"), juce::File(spec.backupExePath).loadFileAsString());
    EXPECT_FALSE(juce::File(spec.updatedFlagPath).exists());
}

TEST_F(ReceiverTransactionTest, PairedUpdateCommitsHostAndReceiversTogether)
{
    spec.skipHostUpdate = false;
    const auto run = install(); ASSERT_TRUE(run.started && run.finished); ASSERT_EQ(0, run.exitCode) << run.output;
    EXPECT_TRUE(static_cast<bool>(result()["success"])); EXPECT_TRUE(static_cast<bool>(result()["hostUpdated"]));
    EXPECT_EQ(version, juce::File(spec.updatedFlagPath).loadFileAsString().trim());
    EXPECT_TRUE(juce::File(spec.backupExePath).existsAsFile());
    EXPECT_EQ(version, installedVersion("DirectPipe Receiver.dll"));
    EXPECT_EQ(version, installedVersion("DirectPipe Receiver.vst3"));
}

TEST_F(ReceiverTransactionTest, FailureAfterVst3SwapRestoresCompleteBundleAndEarlierTargets)
{
    spec.skipHostUpdate = false;
    ASSERT_TRUE(juce::File(spec.backupExePath).replaceWithText("prior host backup"));
    ASSERT_TRUE(installed.getChildFile("DirectPipe Receiver.vst3/old-resource.txt").replaceWithText("original bundle resource"));
    const auto oldBundleBackup = installed.getChildFile("DirectPipe Receiver.vst3.directpipe-backup");
    ASSERT_TRUE(oldBundleBackup.createDirectory());
    ASSERT_TRUE(oldBundleBackup.getChildFile("previous-backup.txt").replaceWithText("previous bundle backup"));
    ASSERT_TRUE(package.getChildFile("DirectPipe Receiver.vst3/new-resource.txt").replaceWithText("candidate resource"));
    makeArchive();
    const auto hostHash = juce::SHA256(juce::File(spec.currentExePath)).toHexString();
    const auto run = install("if ($entry.format -eq 'vst3') { throw 'Injected fixture failure after complete VST3 replacement' }");
    ASSERT_TRUE(run.started && run.finished); EXPECT_NE(0, run.exitCode) << run.output;
    EXPECT_FALSE(static_cast<bool>(result()["success"])); EXPECT_TRUE(static_cast<bool>(result()["rollbackSucceeded"]));
    EXPECT_TRUE(result()["error"].toString().contains("Injected fixture failure after complete VST3")) << run.output;
    EXPECT_EQ(older, installedVersion("DirectPipe Receiver.dll"));
    EXPECT_EQ(older, installedVersion("DirectPipe Receiver.vst3"));
    EXPECT_EQ(juce::String("original bundle resource"), installed.getChildFile("DirectPipe Receiver.vst3/old-resource.txt").loadFileAsString());
    EXPECT_FALSE(installed.getChildFile("DirectPipe Receiver.vst3/new-resource.txt").exists());
    EXPECT_EQ(juce::String("previous bundle backup"), oldBundleBackup.getChildFile("previous-backup.txt").loadFileAsString());
    EXPECT_EQ(hostHash, juce::SHA256(juce::File(spec.currentExePath)).toHexString());
    EXPECT_EQ(juce::String("prior host backup"), juce::File(spec.backupExePath).loadFileAsString());
    EXPECT_FALSE(juce::File(spec.updatedFlagPath).exists());
}

TEST_F(ReceiverTransactionTest, ConcurrentReplacementBeforeLockCannotOverwriteNewerBytes)
{
    auto script = buildWindowsCompanionInstallPowerShell(spec);
    script = script.replace("# DIRECTPIPE_TRANSACTION_BEFORE_LOCK",
        "Copy-Item -LiteralPath " + literal(package.getChildFile("DirectPipe Receiver.dll"))
        + " -Destination " + literal(installed.getChildFile("DirectPipe Receiver.dll")) + " -Force");
    const auto run = runPowerShell(root.getChildFile("install.ps1"), script);
    ASSERT_TRUE(run.started && run.finished); EXPECT_NE(0, run.exitCode) << run.output;
    EXPECT_FALSE(static_cast<bool>(result()["success"]));
    EXPECT_TRUE(result()["error"].toString().contains("changed between verification")) << run.output;
    EXPECT_EQ(version, installedVersion("DirectPipe Receiver.dll"));
    EXPECT_EQ(older, installedVersion("DirectPipe Receiver.vst3"));
}

TEST_F(ReceiverTransactionTest, StagedTamperingBeforeLockCannotInstallUnverifiedBytes)
{
    auto script = buildWindowsCompanionInstallPowerShell(spec);
    script = script.replace("# DIRECTPIPE_TRANSACTION_BEFORE_LOCK",
        "[IO.File]::AppendAllText($entries[0].stage, 'tampered fixture')");
    const auto run = runPowerShell(root.getChildFile("install.ps1"), script);
    ASSERT_TRUE(run.started && run.finished); EXPECT_NE(0, run.exitCode) << run.output;
    EXPECT_FALSE(static_cast<bool>(result()["success"]));
    EXPECT_TRUE(result()["error"].toString().contains("changed between verification")) << run.output;
    EXPECT_EQ(older, installedVersion("DirectPipe Receiver.dll"));
}

TEST_F(ReceiverTransactionTest, ExactFileUseBlocksInspectionAndInstallation)
{
    const auto path = installed.getChildFile("DirectPipe Receiver.dll").getFullPathName();
    HANDLE held = CreateFileW(path.toWideCharPointer(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(INVALID_HANDLE_VALUE, held);
    const auto use = queryReceiverUse(spec.receiverTargets);
    EXPECT_TRUE(use.busy || !use.inspectable);
    const auto run = install();
    CloseHandle(held);
    ASSERT_TRUE(run.started && run.finished); EXPECT_NE(0, run.exitCode) << run.output;
    EXPECT_FALSE(static_cast<bool>(result()["success"]));
    EXPECT_EQ(older, installedVersion("DirectPipe Receiver.dll"));
}

TEST_F(ReceiverTransactionTest, ChangedVersionSincePreviewIsNeverDowngraded)
{
    ASSERT_TRUE(package.getChildFile("DirectPipe Receiver.dll").copyFileTo(installed.getChildFile("DirectPipe Receiver.dll")));
    const auto run = install(); ASSERT_TRUE(run.started && run.finished); EXPECT_NE(0, run.exitCode) << run.output;
    EXPECT_FALSE(static_cast<bool>(result()["success"]));
    EXPECT_EQ(version, installedVersion("DirectPipe Receiver.dll"));
    EXPECT_EQ(older, installedVersion("DirectPipe Receiver.vst3"));
}

TEST_F(ReceiverTransactionTest, HashMismatchAndMissingPackageFormatPreserveInstallations)
{
    spec.expectedPackageSha256 = juce::String::repeatedString("00", 32);
    auto run = install(); ASSERT_TRUE(run.started && run.finished); EXPECT_NE(0, run.exitCode) << run.output;
    EXPECT_EQ(older, installedVersion("DirectPipe Receiver.dll"));
    ASSERT_TRUE(package.getChildFile("DirectPipe Receiver.vst3").deleteRecursively()); makeArchive();
    run = install(); ASSERT_TRUE(run.started && run.finished); EXPECT_NE(0, run.exitCode) << run.output;
    EXPECT_FALSE(static_cast<bool>(result()["success"])); EXPECT_EQ(older, installedVersion("DirectPipe Receiver.dll"));
}

#endif
} // namespace
