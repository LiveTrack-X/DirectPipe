// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "UI/UpdateScript.h"
#include "UI/UpdateStartupSupport.h"

#if JUCE_WINDOWS
#include <windows.h>
#endif

using namespace directpipe::update_detail;

namespace {

juce::File fixtureRoot()
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("DirectPipe-startup-decision-test");
}

juce::String installedExe(const char* folder = "install-a")
{
    return fixtureRoot().getChildFile(folder).getChildFile("DirectPipe.exe").getFullPathName();
}

juce::var receipt(bool success = false, const juce::String& version = "4.5.0")
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("currentExePath", installedExe());
    root->setProperty("expectedVersion", version);
    root->setProperty("success", success);
    root->setProperty("inProgress", false);
    root->setProperty("stage", success ? "complete" : "replace");
    root->setProperty("error", success ? "" : "The installed file was locked.");
    return juce::var(root.release());
}

} // namespace

TEST(HostUpdateStartupTest, DifferentInstallFoldersHaveIndependentReceiptFiles)
{
    const auto appData = fixtureRoot().getChildFile("app-data");
    const auto first = hostUpdateResultFileForInstall(appData, installedExe());
    const auto second = hostUpdateResultFileForInstall(appData, installedExe("install-b"));
    EXPECT_NE(first, second);
    EXPECT_EQ(first.getParentDirectory(), appData.getChildFile("DirectPipe/update-results"));
    EXPECT_EQ(second.getParentDirectory(), first.getParentDirectory());
}

TEST(HostUpdateStartupTest, ReceiptIdentityFollowsFilesystemPathCaseRules)
{
    const auto appData = fixtureRoot().getChildFile("app-data");
    const auto first = hostUpdateResultFileForInstall(appData, installedExe());
    const auto changedCase = hostUpdateResultFileForInstall(appData, installedExe().toUpperCase());
#if JUCE_WINDOWS
    EXPECT_EQ(first, changedCase);
#else
    EXPECT_NE(first, changedCase);
#endif
}

TEST(HostUpdateStartupTest, FailureIsShownOnceWhileRecoveryFilesRemainRetained)
{
    auto result = receipt();
    auto decision = classifyHostUpdateStartupReceipt(result, installedExe(), "4.4.0");
    EXPECT_TRUE(decision.matchesInstall);
    EXPECT_FALSE(decision.stale);
    EXPECT_TRUE(decision.retainRecoveryFiles);
    EXPECT_TRUE(decision.showAttention);

    result.getDynamicObject()->setProperty("attentionAcknowledged", true);
    decision = classifyHostUpdateStartupReceipt(result, installedExe(), "4.4.0");
    EXPECT_TRUE(decision.matchesInstall);
    EXPECT_TRUE(decision.retainRecoveryFiles);
    EXPECT_FALSE(decision.showAttention);
}

TEST(HostUpdateStartupTest, CommittedInstallWithRelaunchErrorNeedsAttention)
{
    auto result = receipt(true);
    result.getDynamicObject()->setProperty("relaunchError", "The desktop shell was unavailable.");
    const auto decision = classifyHostUpdateStartupReceipt(result, installedExe(), "4.5.0");
    EXPECT_TRUE(decision.matchesInstall);
    EXPECT_TRUE(decision.retainRecoveryFiles);
    EXPECT_TRUE(decision.showAttention);
}

TEST(HostUpdateStartupTest, NewerManualInstallSupersedesOlderFailureReceipt)
{
    const auto decision = classifyHostUpdateStartupReceipt(receipt(false, "4.4.0"), installedExe(), "4.5.0");
    EXPECT_TRUE(decision.matchesInstall);
    EXPECT_TRUE(decision.stale);
    EXPECT_FALSE(decision.retainRecoveryFiles);
    EXPECT_FALSE(decision.showAttention);
}

TEST(HostUpdateStartupTest, SameVersionFailureIsNotDiscardedAsStale)
{
    const auto decision = classifyHostUpdateStartupReceipt(receipt(), installedExe(), "4.5.0");
    EXPECT_TRUE(decision.matchesInstall);
    EXPECT_FALSE(decision.stale);
    EXPECT_TRUE(decision.retainRecoveryFiles);
    EXPECT_TRUE(decision.showAttention);
}

TEST(HostUpdateStartupTest, PendingOperationRetainsRecoveryEvenWhenSuccessWasSet)
{
    auto result = receipt(true);
    result.getDynamicObject()->setProperty("inProgress", true);
    const auto decision = classifyHostUpdateStartupReceipt(result, installedExe(), "4.5.0");
    EXPECT_TRUE(decision.retainRecoveryFiles);
    EXPECT_TRUE(decision.showAttention);
}

TEST(HostUpdateStartupTest, ForeignInstallCannotSuppressThisInstallCleanup)
{
    const auto decision = classifyHostUpdateStartupReceipt(receipt(), installedExe("install-b"), "4.4.0");
    EXPECT_FALSE(decision.matchesInstall);
    EXPECT_FALSE(decision.retainRecoveryFiles);
    EXPECT_FALSE(decision.showAttention);
}

TEST(HostUpdateStartupTest, RelativeOrMissingReceiptIdentityIsIgnored)
{
    auto result = receipt();
    result.getDynamicObject()->setProperty("currentExePath", "../DirectPipe.exe");
    EXPECT_FALSE(classifyHostUpdateStartupReceipt(result, installedExe(), "4.4.0").matchesInstall);
    EXPECT_FALSE(classifyHostUpdateStartupReceipt({}, installedExe(), "4.4.0").matchesInstall);
}

TEST(HostUpdateStartupTest, SuccessfulInstallNeedsNoAttention)
{
    const auto decision = classifyHostUpdateStartupReceipt(receipt(true), installedExe(), "4.5.0");
    EXPECT_TRUE(decision.matchesInstall);
    EXPECT_FALSE(decision.retainRecoveryFiles);
    EXPECT_FALSE(decision.showAttention);
}

TEST(HostUpdateStartupTest, UnknownVersionCannotDiscardFailureEvidence)
{
    const auto decision = classifyHostUpdateStartupReceipt(receipt(false, "unknown"), installedExe(), "4.5.0");
    EXPECT_TRUE(decision.matchesInstall);
    EXPECT_FALSE(decision.stale);
    EXPECT_TRUE(decision.retainRecoveryFiles);
    EXPECT_TRUE(decision.showAttention);
}

namespace {

struct StartupFiles {
    juce::File directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("DirectPipe-startup-" + juce::Uuid().toString());
    juce::File current = directory.getChildFile("install/DirectPipe.exe");
    juce::File backup = current.getSiblingFile("DirectPipe_backup.exe");
    juce::File staged = current.getSiblingFile("DirectPipe_update.exe");
    juce::File result = hostUpdateResultFileForInstall(directory.getChildFile("app-data"), current.getFullPathName());
    juce::String attempt { "1234567890abcdef1234567890abcdef" };

    ~StartupFiles()
    {
        if (directory.isAChildOf(juce::File::getSpecialLocation(juce::File::tempDirectory)))
            directory.deleteRecursively();
    }

    bool prepare()
    {
        return current.getParentDirectory().createDirectory().wasOk()
            && result.getParentDirectory().createDirectory().wasOk()
            && current.replaceWithText("current-host-4.5.0")
            && backup.replaceWithText("old-host-4.3.0")
            && staged.replaceWithText("staged-host-4.5.0");
    }

    juce::File expectedAck() const
    {
        return result.getSiblingFile(result.getFileNameWithoutExtension() + "." + attempt + ".startup.json");
    }

    juce::var pendingReceipt() const
    {
        auto root = std::make_unique<juce::DynamicObject>();
        root->setProperty("requireStartupAck", true);
        root->setProperty("startupAttemptId", attempt);
        root->setProperty("startupAckFilePath", expectedAck().getFullPathName());
        root->setProperty("currentExePath", current.getFullPathName());
        root->setProperty("expectedVersion", "4.5.0");
        root->setProperty("filesCommitted", true);
        root->setProperty("inProgress", true);
        root->setProperty("startupReady", false);
        return juce::var(root.release());
    }

    bool saveReceipt(const juce::var& data) const
    {
        return result.replaceWithText(juce::JSON::toString(data, true));
    }

    HostExecutableInspector inspector() const
    {
        return [currentFile = current, backupFile = backup](const juce::File& file,
                    HostExecutableIdentity& identity, juce::String& error) {
            juce::FileInputStream input(file);
            if (!input.openedOk()) { error = "Fixture file is locked"; return false; }
            identity.productName = "DirectPipe";
            identity.fileVersion = file == backupFile ? "4.3.0" : "4.5.0";
            identity.productVersion = identity.fileVersion;
            identity.sha256 = juce::SHA256(input).toHexString();
            return file == currentFile || file == backupFile
                || file.getFileName() == "DirectPipe_update.exe";
        };
    }
};

} // namespace

TEST(StartupUpdateSupportTest, TokenArgumentAcceptsOnlyOneCanonicalAttempt)
{
    const juce::String token = "1234567890abcdef1234567890abcdef";
    juce::String error;
    EXPECT_EQ(startupAttemptFromArguments({ "--directpipe-update-attempt=" + token }, error), token);
    EXPECT_TRUE(error.isEmpty());
    EXPECT_TRUE(startupAttemptFromArguments({ "--directpipe-update-attempt=../../arbitrary" }, error).isEmpty());
    EXPECT_TRUE(error.isNotEmpty());
    EXPECT_TRUE(startupAttemptFromArguments({ "--directpipe-update-attempt=" + token,
        "--directpipe-update-attempt=" + token }, error).isEmpty());
    EXPECT_TRUE(error.isNotEmpty());
}

TEST(StartupUpdateSupportTest, AckPathIsPrivateReceiptSiblingForThisAttempt)
{
    StartupFiles files;
    EXPECT_EQ(startupAckFileForAttempt(files.result, files.attempt), files.expectedAck());
    EXPECT_EQ(startupAckFileForAttempt(files.result, "../../outside"), juce::File{});
}

TEST(StartupUpdateSupportTest, ReadyAckBindsAttemptExecutableVersionAndProcess)
{
    StartupFiles files;
    ASSERT_TRUE(files.prepare());
    ASSERT_TRUE(files.saveReceipt(files.pendingReceipt()));
    juce::String error;
    ASSERT_TRUE(writeHostStartupAcknowledgement(files.result, files.attempt, files.current,
        "4.5.0", 1234, true, "startup ready (tray)", error)) << error.toStdString();
    const auto ack = juce::JSON::parse(files.expectedAck());
    EXPECT_EQ(ack["attemptId"].toString(), files.attempt);
    EXPECT_EQ(ack["currentExePath"].toString(), files.current.getFullPathName());
    EXPECT_EQ(ack["version"].toString(), "4.5.0");
    EXPECT_EQ(static_cast<int>(ack["pid"]), 1234);
    EXPECT_TRUE(static_cast<bool>(ack["ready"]));
    EXPECT_EQ(ack["reason"].toString(), "startup ready (tray)");
    EXPECT_TRUE(files.backup.existsAsFile()) << "The app ACK must not delete installer-owned backups";
}

TEST(StartupUpdateSupportTest, IncompleteRestoreWritesNegativeAckWithReason)
{
    StartupFiles files;
    ASSERT_TRUE(files.prepare());
    ASSERT_TRUE(files.saveReceipt(files.pendingReceipt()));
    juce::String error;
    ASSERT_TRUE(writeHostStartupAcknowledgement(files.result, files.attempt, files.current,
        "4.5.0", 1234, false, "Initial plug-in restore was incomplete", error));
    const auto ack = juce::JSON::parse(files.expectedAck());
    EXPECT_FALSE(static_cast<bool>(ack["ready"]));
    EXPECT_TRUE(ack["reason"].toString().contains("incomplete"));
    EXPECT_TRUE(files.backup.existsAsFile());
}

TEST(StartupUpdateSupportTest, LongValidAckPathCommitsWithoutExpandingTemporaryBasename)
{
    StartupFiles files;
    const auto shortReceipt = files.directory.getChildFile("receipt.json");
    const int padding = 248 - startupAckFileForAttempt(shortReceipt, files.attempt).getFullPathName().length() - 1;
    ASSERT_GT(padding, 0);
    files.result = files.directory.getChildFile(juce::String::repeatedString("p", padding)).getChildFile("receipt.json");
    ASSERT_EQ(files.expectedAck().getFullPathName().length(), 248);
    ASSERT_TRUE(files.prepare());
    ASSERT_TRUE(files.saveReceipt(files.pendingReceipt()));
    juce::String error;
    ASSERT_TRUE(writeHostStartupAcknowledgement(files.result, files.attempt, files.current,
        "4.5.0", 1234, true, "startup ready", error)) << error.toStdString();
    const auto written = files.expectedAck().loadFileAsString();
    const auto ack = juce::JSON::parse(written);
    EXPECT_EQ(ack["attemptId"].toString(), files.attempt);
    EXPECT_TRUE(static_cast<bool>(ack["ready"]));
    EXPECT_FALSE(writeHostStartupAcknowledgement(files.result, files.attempt, files.current,
        "4.5.0", 1234, false, "cannot replace evidence", error));
    EXPECT_EQ(files.expectedAck().loadFileAsString(), written);
    EXPECT_TRUE(files.backup.existsAsFile());
}

#if JUCE_WINDOWS && defined(DIRECTPIPE_TEST_STARTUP_PROBE_PATH)
namespace {
class ScopedStartupProbeTemp {
public:
    explicit ScopedStartupProbeTemp(const juce::String& temporary) : temp_(read(L"TEMP")), tmp_(read(L"TMP"))
    {
        changed_ = SetEnvironmentVariableW(L"TEMP", temporary.toWideCharPointer()) != FALSE
            && SetEnvironmentVariableW(L"TMP", temporary.toWideCharPointer()) != FALSE;
    }
    ~ScopedStartupProbeTemp()
    {
        SetEnvironmentVariableW(L"TEMP", temp_.empty() ? nullptr : temp_.c_str());
        SetEnvironmentVariableW(L"TMP", tmp_.empty() ? nullptr : tmp_.c_str());
    }
    bool changed() const { return changed_; }
private:
    static std::wstring read(const wchar_t* name)
    {
        const auto length = GetEnvironmentVariableW(name, nullptr, 0);
        if (!length) return {};
        std::vector<wchar_t> value(length);
        GetEnvironmentVariableW(name, value.data(), length);
        return value.data();
    }
    std::wstring temp_, tmp_;
    bool changed_ = false;
};

juce::String longStartupProbeDirectory(const juce::File& directory)
{
    std::vector<wchar_t> value(32768);
    const auto length = GetLongPathNameW(directory.getFullPathName().toWideCharPointer(), value.data(), static_cast<DWORD>(value.size()));
    return length > 0 && length < value.size() ? juce::String(value.data()) : juce::String{};
}

bool configureOwnedStartupProbe(const juce::File& executable, const juce::String& ack)
{
    const auto ini = executable.getSiblingFile("startup-probe.ini").getFullPathName();
    return WritePrivateProfileStringW(L"StartupProbe", L"AckPath", ack.toWideCharPointer(), ini.toWideCharPointer()) != FALSE
        && WritePrivateProfileStringW(L"StartupProbe", L"LifetimeMs", L"0", ini.toWideCharPointer()) != FALSE;
}
}

TEST(StartupUpdateSupportTest, ProbeAcceptsShortAliasInsideCanonicalTemporaryDirectory)
{
    StartupFiles files;
    ASSERT_TRUE(files.prepare());
    const auto longDirectory = longStartupProbeDirectory(files.directory);
    ASSERT_TRUE(longDirectory.isNotEmpty());
    std::vector<wchar_t> alias(32768);
    const auto aliasLength = GetShortPathNameW(longDirectory.toWideCharPointer(), alias.data(), static_cast<DWORD>(alias.size()));
    ASSERT_GT(aliasLength, 0u);
    const juce::String shortDirectory(alias.data());
    if (shortDirectory.equalsIgnoreCase(longDirectory))
        GTEST_SKIP() << "This test volume does not provide a distinct 8.3 directory alias";
    const auto executable = files.directory.getChildFile("owned-startup-probe.exe");
    ASSERT_TRUE(juce::File(DIRECTPIPE_TEST_STARTUP_PROBE_PATH).copyFileTo(executable));
    const auto acknowledgement = juce::File(shortDirectory).getChildFile("alias.startup.json");
    ASSERT_TRUE(configureOwnedStartupProbe(executable, acknowledgement.getFullPathName()));
    ScopedStartupProbeTemp temporary(longDirectory);
    ASSERT_TRUE(temporary.changed());
    juce::ChildProcess child;
    ASSERT_TRUE(child.start(juce::StringArray { executable.getFullPathName(), "--directpipe-update-attempt=" + files.attempt }));
    ASSERT_TRUE(child.waitForProcessToFinish(5000));
    EXPECT_EQ(child.getExitCode(), 0);
    const auto ack = juce::JSON::parse(acknowledgement);
    EXPECT_EQ(ack["attemptId"].toString(), files.attempt);
    EXPECT_TRUE(static_cast<bool>(ack["ready"]));
    EXPECT_TRUE(executable.getSiblingFile("startup-probe.diagnostic.txt").loadFileAsString().contains("exit=0"));
}

TEST(StartupUpdateSupportTest, ProbeRejectsTemporaryPrefixSiblingAndRecordsOwnedDiagnostic)
{
    StartupFiles files;
    ASSERT_TRUE(files.prepare());
    const auto allowed = files.directory.getChildFile("allowed");
    const auto outside = files.directory.getChildFile("allowed-other/outside.startup.json");
    ASSERT_TRUE(allowed.createDirectory());
    ASSERT_TRUE(outside.getParentDirectory().createDirectory());
    ASSERT_TRUE(outside.replaceWithText("outside marker"));
    const auto executable = allowed.getChildFile("owned-startup-probe.exe");
    ASSERT_TRUE(juce::File(DIRECTPIPE_TEST_STARTUP_PROBE_PATH).copyFileTo(executable));
    ASSERT_TRUE(configureOwnedStartupProbe(executable, outside.getFullPathName()));
    const auto longAllowed = longStartupProbeDirectory(allowed);
    ASSERT_TRUE(longAllowed.isNotEmpty());
    ScopedStartupProbeTemp temporary(longAllowed);
    ASSERT_TRUE(temporary.changed());
    juce::ChildProcess child;
    ASSERT_TRUE(child.start(juce::StringArray { executable.getFullPathName(), "--directpipe-update-attempt=" + files.attempt }));
    ASSERT_TRUE(child.waitForProcessToFinish(5000));
    EXPECT_EQ(child.getExitCode(), 3);
    EXPECT_EQ(outside.loadFileAsString(), "outside marker");
    EXPECT_TRUE(executable.getSiblingFile("startup-probe.diagnostic.txt").loadFileAsString().contains("reason=ack-outside-temp"));
}
#endif

TEST(StartupUpdateSupportTest, MissingWrongOrForeignAttemptCannotWriteAck)
{
    StartupFiles files;
    ASSERT_TRUE(files.prepare());
    ASSERT_TRUE(files.saveReceipt(files.pendingReceipt()));
    juce::String error;
    EXPECT_FALSE(writeHostStartupAcknowledgement(files.result, {}, files.current,
        "4.5.0", 1234, true, {}, error));
    EXPECT_FALSE(writeHostStartupAcknowledgement(files.result, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", files.current,
        "4.5.0", 1234, true, {}, error));
    EXPECT_FALSE(writeHostStartupAcknowledgement(files.result, files.attempt, files.current.getSiblingFile("Other.exe"),
        "4.5.0", 1234, true, {}, error));
    EXPECT_FALSE(files.expectedAck().existsAsFile());
}

TEST(StartupUpdateSupportTest, WrongVersionOrArbitraryAckDestinationIsRejected)
{
    StartupFiles files;
    ASSERT_TRUE(files.prepare());
    ASSERT_TRUE(files.saveReceipt(files.pendingReceipt()));
    juce::String error;
    EXPECT_FALSE(writeHostStartupAcknowledgement(files.result, files.attempt, files.current,
        "4.4.0", 1234, true, {}, error));
    auto changed = files.pendingReceipt();
    const auto outside = files.directory.getChildFile("outside.json");
    changed.getDynamicObject()->setProperty("startupAckFilePath", outside.getFullPathName());
    ASSERT_TRUE(files.saveReceipt(changed));
    EXPECT_FALSE(writeHostStartupAcknowledgement(files.result, files.attempt, files.current,
        "4.5.0", 1234, true, {}, error));
    EXPECT_FALSE(outside.exists());
    EXPECT_FALSE(files.expectedAck().exists());
}

TEST(StartupUpdateSupportTest, MatchingLegacyFlagCleansOnlyVerifiedManagedExeNames)
{
    StartupFiles files;
    ASSERT_TRUE(files.prepare());
    const auto manual = files.current.getSiblingFile("DirectPipe_backup_20261111.exe");
    const auto settings = files.current.getSiblingFile("settings.dppreset");
    ASSERT_TRUE(manual.replaceWithText("dated-manual-original"));
    ASSERT_TRUE(settings.replaceWithText("original-settings"));
    const auto result = cleanupLegacyUpdateExecutables(files.current, "4.5.0", "4.5.0",
        files.result, {}, true, files.inspector());
    EXPECT_TRUE(result.complete) << result.warning.toStdString();
    EXPECT_FALSE(files.backup.exists());
    EXPECT_FALSE(files.staged.exists());
    EXPECT_EQ(files.current.loadFileAsString(), "current-host-4.5.0");
    EXPECT_EQ(manual.loadFileAsString(), "dated-manual-original");
    EXPECT_EQ(settings.loadFileAsString(), "original-settings");
}

TEST(StartupUpdateSupportTest, NoLegacyFlagNeverDeletesManuallyKeptKnownBackup)
{
    StartupFiles files;
    ASSERT_TRUE(files.prepare());
    const auto result = cleanupLegacyUpdateExecutables(files.current, "4.5.0", {},
        files.result, {}, true, files.inspector());
    EXPECT_FALSE(result.attempted);
    EXPECT_TRUE(files.backup.existsAsFile());
    EXPECT_TRUE(files.staged.existsAsFile());
}

TEST(StartupUpdateSupportTest, PendingReceiptCannotFallBackToLegacyWhenCliTokenIsMissing)
{
    StartupFiles files;
    ASSERT_TRUE(files.prepare());
    ASSERT_TRUE(files.saveReceipt(files.pendingReceipt()));
    const auto result = cleanupLegacyUpdateExecutables(files.current, "4.5.0", "4.5.0",
        files.result, {}, true, files.inspector());
    EXPECT_FALSE(result.complete);
    EXPECT_TRUE(result.warning.isNotEmpty());
    EXPECT_TRUE(files.backup.existsAsFile());
    EXPECT_TRUE(files.staged.existsAsFile());
}

TEST(StartupUpdateSupportTest, StaleFlagOrIncompleteRestoreRetainsBackup)
{
    StartupFiles files;
    ASSERT_TRUE(files.prepare());
    auto result = cleanupLegacyUpdateExecutables(files.current, "4.5.0", "4.4.0",
        files.result, {}, true, files.inspector());
    EXPECT_FALSE(result.complete);
    EXPECT_TRUE(result.warning.isNotEmpty());
    result = cleanupLegacyUpdateExecutables(files.current, "4.5.0", "4.5.0",
        files.result, {}, false, files.inspector());
    EXPECT_FALSE(result.complete);
    EXPECT_TRUE(result.warning.isNotEmpty());
    EXPECT_TRUE(files.backup.existsAsFile());
}

TEST(StartupUpdateSupportTest, WrongBackupIdentityPreventsLegacyDeletion)
{
    StartupFiles files;
    ASSERT_TRUE(files.prepare());
    const auto validInspector = files.inspector();
    const auto wrongInspector = [&](const juce::File& file, HostExecutableIdentity& identity, juce::String& error) {
        if (!validInspector(file, identity, error)) return false;
        if (file == files.backup) identity.productName = "Another application";
        return true;
    };
    const auto result = cleanupLegacyUpdateExecutables(files.current, "4.5.0", "4.5.0",
        files.result, {}, true, wrongInspector);
    EXPECT_FALSE(result.complete);
    EXPECT_TRUE(result.warning.isNotEmpty());
    EXPECT_TRUE(files.backup.existsAsFile());
    EXPECT_TRUE(files.staged.existsAsFile());
}

TEST(StartupUpdateSupportTest, LockedLegacyBackupIsRetainedAndReported)
{
#if JUCE_WINDOWS
    StartupFiles files;
    ASSERT_TRUE(files.prepare());
    HANDLE lock = ::CreateFileW(files.backup.getFullPathName().toWideCharPointer(), GENERIC_READ,
        0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(lock, INVALID_HANDLE_VALUE);
    const auto result = cleanupLegacyUpdateExecutables(files.current, "4.5.0", "4.5.0",
        files.result, {}, true, files.inspector());
    ::CloseHandle(lock);
    EXPECT_FALSE(result.complete);
    EXPECT_TRUE(result.warning.isNotEmpty());
    EXPECT_TRUE(files.backup.existsAsFile());
#else
    GTEST_SKIP() << "Windows exclusive-file lock fixture";
#endif
}

TEST(StartupUpdateSupportTest, NativeInspectorReadsBuiltProbeMetadataWithoutExecutingIt)
{
#if JUCE_WINDOWS && defined(DIRECTPIPE_TEST_STARTUP_PROBE_PATH)
    const juce::File probe(DIRECTPIPE_TEST_STARTUP_PROBE_PATH);
    ASSERT_TRUE(probe.existsAsFile());
    HostExecutableIdentity identity;
    juce::String error;
    ASSERT_TRUE(inspectHostExecutableForCleanup(probe, identity, error)) << error.toStdString();
    EXPECT_EQ(identity.productName, "DirectPipe");
    EXPECT_EQ(identity.fileVersion, juce::String(ProjectInfo::versionString));
    EXPECT_EQ(identity.productVersion, identity.fileVersion);
    juce::FileInputStream input(probe);
    ASSERT_TRUE(input.openedOk());
    EXPECT_EQ(identity.sha256, juce::SHA256(input).toHexString());
#else
    GTEST_SKIP() << "Windows native executable metadata fixture";
#endif
}
