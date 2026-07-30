// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include "UI/UpdateChecker.h"
#include "UI/UpdateScript.h"

#if JUCE_WINDOWS
#include <Windows.h>
#endif

namespace {

TEST(UpdateScriptValidationTest, StrictReleaseVersionRejectsShellAndPartialInput)
{
    std::array<int, 3> components{};
    juce::String canonical;

    EXPECT_TRUE(directpipe::update_detail::parseStrictReleaseVersion(
        "v4.2.0", components, canonical));
    EXPECT_EQ((std::array<int, 3>{4, 2, 0}), components);
    EXPECT_EQ(juce::String("4.2.0"), canonical);

    EXPECT_FALSE(directpipe::update_detail::parseStrictReleaseVersion(
        "v4.2.0&whoami", components, canonical));
    EXPECT_FALSE(directpipe::update_detail::parseStrictReleaseVersion(
        "4.2", components, canonical));
    EXPECT_FALSE(directpipe::update_detail::parseStrictReleaseVersion(
        "4..0", components, canonical));
    EXPECT_FALSE(directpipe::update_detail::parseStrictReleaseVersion(
        "4.2.0.1", components, canonical));
}

TEST(UpdateChecksumValidationTest, RequiresChecksumForCurrentReleaseLine)
{
    EXPECT_FALSE(directpipe::update_detail::releaseRequiresChecksum("4.1.2"));
    EXPECT_TRUE(directpipe::update_detail::releaseRequiresChecksum("v4.2.0"));
    EXPECT_TRUE(directpipe::update_detail::releaseRequiresChecksum("4.2.1"));
    EXPECT_FALSE(directpipe::update_detail::releaseRequiresChecksum("not-a-version"));
}

TEST(UpdateChecksumValidationTest, MatchesExactAssetAndRejectsMalformedHash)
{
    const juce::String validHash(juce::String::repeatedString("ab", 32));
    juce::String expected;
    const auto content = validHash + "  DirectPipe-v4.2.1-Windows.zip\n"
                       + juce::String::repeatedString("cd", 32)
                       + "  DirectPipe-v4.2.1-Linux.tar.gz\n";

    EXPECT_TRUE(directpipe::update_detail::parseExpectedSha256(
        content, "DirectPipe-v4.2.1-Windows.zip", expected));
    EXPECT_EQ(validHash, expected);
    EXPECT_FALSE(directpipe::update_detail::parseExpectedSha256(
        content, "DirectPipe-v4.2.1-Windows", expected));
    EXPECT_FALSE(directpipe::update_detail::parseExpectedSha256(
        "xyz  DirectPipe-v4.2.1-Windows.zip", "DirectPipe-v4.2.1-Windows.zip", expected));
}

#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
TEST(UpdateCheckerLifecycleTest, RepeatedChecksDoNotReplaceJoinableThread)
{
    directpipe::UpdateChecker checker;
    std::promise<void> releaseFirst;
    auto releaseSignal = releaseFirst.get_future().share();

    ASSERT_TRUE(checker.startUpdateCheckWorkerForTest([releaseSignal] {
        releaseSignal.wait();
    }));
    EXPECT_TRUE(checker.isUpdateCheckInProgressForTest());
    EXPECT_FALSE(checker.startUpdateCheckWorkerForTest([] {}));

    releaseFirst.set_value();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (checker.isUpdateCheckInProgressForTest()
           && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    EXPECT_FALSE(checker.isUpdateCheckInProgressForTest());
    checker.reapFinishedUpdateCheckThreadForTest();
    EXPECT_TRUE(checker.startUpdateCheckWorkerForTest([] {}));
}
#endif

#if JUCE_WINDOWS && defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
TEST(UpdateCheckerLifecycleTest, RepeatedCheckDropsQueuedResultFromPriorGeneration)
{
    ASSERT_NE(juce::MessageManager::getInstance(), nullptr);

    directpipe::UpdateChecker checker;
    int callbackCount = 0;
    juce::String callbackVersion;
    checker.onUpdateAvailable = [&](const juce::String& version,
                                    const juce::String&) {
        ++callbackCount;
        callbackVersion = version;
    };

    const auto priorGeneration = checker.beginUpdateCheckRequestForTest();
    ASSERT_TRUE(checker.postUpdateAvailableForTest(
        priorGeneration, "4.2.1", "https://example.invalid/old.zip"));

    // Start the next request before the first request's queued UI result runs.
    const auto currentGeneration = checker.beginUpdateCheckRequestForTest();
    ASSERT_NE(priorGeneration, currentGeneration);
    ASSERT_TRUE(checker.postUpdateAvailableForTest(
        currentGeneration, "4.2.2", "https://example.invalid/new.zip"));

    auto sentinelDispatched = std::make_shared<std::atomic<bool>>(false);
    ASSERT_TRUE(juce::MessageManager::callAsync([sentinelDispatched] {
        sentinelDispatched->store(true, std::memory_order_release);
    }));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!sentinelDispatched->load(std::memory_order_acquire)
           && std::chrono::steady_clock::now() < deadline) {
        MSG message;
        bool dispatchedMessage = false;
        while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessage(&message);
            dispatchedMessage = true;
        }
        if (!dispatchedMessage)
            std::this_thread::yield();
    }

    ASSERT_TRUE(sentinelDispatched->load(std::memory_order_acquire));
    EXPECT_EQ(callbackCount, 1);
    EXPECT_EQ(callbackVersion, "4.2.2");
    EXPECT_TRUE(checker.isUpdateAvailable());
    EXPECT_EQ(checker.getLatestVersion(), "4.2.2");
    EXPECT_EQ(checker.getCheckStatus(), directpipe::UpdateCheckStatus::UpdateAvailable);
}
#endif

#if JUCE_WINDOWS

TEST(UpdateCheckerLifecycleTest, ReturnedWorkerIsReapedAndCanBeRetried)
{
    directpipe::UpdateChecker checker;
    std::promise<void> releaseFirstAttempt;
    auto releaseSignal = releaseFirstAttempt.get_future().share();

    const bool firstStarted = checker.startDownloadWorkerForTest(
        [releaseSignal] { releaseSignal.wait(); });
    if (!firstStarted) {
        ADD_FAILURE() << "initial worker did not start";
        return;
    }

    EXPECT_TRUE(checker.isDownloadInProgressForTest());
    EXPECT_FALSE(checker.startDownloadWorkerForTest([] {}));

    std::atomic<bool> retried{false};
    auto concurrentRetry = std::async(std::launch::async, [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
            if (checker.startDownloadWorkerForTest([&retried] {
                    retried.store(true, std::memory_order_release);
                })) {
                return true;
            }
            std::this_thread::yield();
        }
        return false;
    });

    // Race retries directly against the first worker's completion publication.
    // The lifecycle mutex must join any old thread before assigning the new one.
    releaseFirstAttempt.set_value();
    EXPECT_TRUE(concurrentRetry.get());

    const auto retryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < retryDeadline
           && checker.isDownloadInProgressForTest()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(retried.load(std::memory_order_acquire));
    EXPECT_FALSE(checker.isDownloadInProgressForTest());
    checker.reapFinishedDownloadThreadForTest();
    EXPECT_FALSE(checker.isDownloadThreadFinishedForTest());
    EXPECT_FALSE(checker.hasJoinableDownloadThreadForTest());
}

TEST(UpdateScriptValidationTest, WaitScriptTargetsOnlyLaunchingProcessId)
{
    const auto script = directpipe::update_detail::buildWindowsUpdateWaitScript(424242UL);
    EXPECT_TRUE(script.contains("Get-Process -Id 424242"));
    EXPECT_FALSE(script.containsIgnoreCase("IMAGENAME"));
    EXPECT_FALSE(script.containsIgnoreCase("DirectPipe.exe"));
}

TEST(UpdateScriptValidationTest, CompletionScriptRejectsUnsafeVersion)
{
    EXPECT_TRUE(directpipe::update_detail::buildWindowsUpdateCompletionScript(
        "4.2.0&whoami", "C:\\flag", "C:\\DirectPipe.exe").isEmpty());
}

TEST(UpdateScriptValidationTest, InstallScriptRejectsUnsafeExpectedVersion)
{
    directpipe::update_detail::WindowsUpdateInstallSpec spec {
        "C:\\DirectPipe.exe",
        "C:\\DirectPipe_update.zip",
        "C:\\DirectPipe_update.exe",
        "C:\\DirectPipe_backup.exe",
        "C:\\_update",
        "4.2.7&whoami",
        true,
    };
    EXPECT_TRUE(
        directpipe::update_detail::buildWindowsUpdateInstallScript(spec).isEmpty());
}

juce::String windowsPath(const juce::File& file)
{
    return file.getFullPathName().replace("/", "\\");
}

struct ScriptRunResult {
    bool started = false;
    bool finished = false;
    int exitCode = -1;
    juce::String output;
};

ScriptRunResult runInstallScript(const juce::String& installScript,
                                 const juce::File& batchFile)
{
    EXPECT_TRUE(batchFile.replaceWithText(
        "@echo off\r\n" + installScript + "exit /b 0\r\n"));

    juce::ChildProcess process;
    const auto command = "cmd.exe /d /c \"\"" + batchFile.getFullPathName() + "\"\"";
    ScriptRunResult result;
    result.started = process.start(command);
    if (!result.started)
        return result;

    result.finished = process.waitForProcessToFinish(15000);
    result.output = process.readAllProcessOutput();
    result.exitCode = process.getExitCode();
    return result;
}

class UpdateScriptTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        testDir_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getNonexistentChildFile("DirectPipe Updater Test", {}, true);
        ASSERT_TRUE(testDir_.createDirectory());
    }

    void TearDown() override
    {
        testDir_.deleteRecursively();
    }

    directpipe::update_detail::WindowsUpdateInstallSpec makeSpec(bool isZip) const
    {
        return {
            windowsPath(testDir_.getChildFile("DirectPipe.exe")),
            windowsPath(testDir_.getChildFile(isZip ? "DirectPipe_update.zip"
                                                    : "DirectPipe_update.exe")),
            windowsPath(testDir_.getChildFile("DirectPipe_staged.exe")),
            windowsPath(testDir_.getChildFile("DirectPipe_backup.exe")),
            windowsPath(testDir_.getChildFile("_update")),
            expectedReleaseVersion(),
            isZip,
        };
    }

    juce::File versionedTestExecutable() const
    {
        return juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    }

    juce::String expectedReleaseVersion() const
    {
        // juce::File::getVersion() exposes the four-part numeric resource
        // (for example, 4.2.7.0), while PowerShell's FileVersion and
        // ProductVersion properties expose the release identity (4.2.7).
        const auto resourceVersion = versionedTestExecutable().getVersion();
        juce::StringArray components;
        components.addTokens(resourceVersion, ".", "");
        EXPECT_GE(components.size(), 3);
        if (components.size() < 3)
            return {};

        return components[0] + "." + components[1] + "." + components[2];
    }

    bool copyVersionedTestExecutableTo(const juce::File& destination) const
    {
        const auto source = versionedTestExecutable();
        EXPECT_TRUE(source.existsAsFile());
        EXPECT_FALSE(source.getVersion().isEmpty());
        return source.copyFileTo(destination);
    }

    juce::File testDir_;
};

TEST_F(UpdateScriptTest, MissingStandaloneUpdatePreservesOriginalExecutable)
{
    const auto currentExe = testDir_.getChildFile("DirectPipe.exe");
    ASSERT_TRUE(currentExe.replaceWithText("known-good executable"));

    const auto script = directpipe::update_detail::buildWindowsUpdateInstallScript(
        makeSpec(false));
    const auto result = runInstallScript(script, testDir_.getChildFile("install.bat"));

    ASSERT_TRUE(result.started);
    ASSERT_TRUE(result.finished) << result.output;
    EXPECT_NE(result.exitCode, 0) << result.output;
    ASSERT_TRUE(currentExe.existsAsFile()) << result.output;
    EXPECT_EQ(currentExe.loadFileAsString(), "known-good executable");
}

TEST_F(UpdateScriptTest, MissingCurrentExecutableKeepsExistingBackup)
{
    const auto updateExe = testDir_.getChildFile("DirectPipe_update.exe");
    const auto backupExe = testDir_.getChildFile("DirectPipe_backup.exe");
    ASSERT_TRUE(copyVersionedTestExecutableTo(updateExe));
    ASSERT_TRUE(backupExe.replaceWithText("previous known-good executable"));

    const auto script = directpipe::update_detail::buildWindowsUpdateInstallScript(
        makeSpec(false));
    const auto result = runInstallScript(script, testDir_.getChildFile("install.bat"));

    ASSERT_TRUE(result.started);
    ASSERT_TRUE(result.finished) << result.output;
    EXPECT_NE(result.exitCode, 0) << result.output;
    ASSERT_TRUE(backupExe.existsAsFile()) << result.output;
    EXPECT_EQ(backupExe.loadFileAsString(), "previous known-good executable");
}

TEST_F(UpdateScriptTest, SuccessfulStandaloneUpdateKeepsBackupForNextRotation)
{
    const auto currentExe = testDir_.getChildFile("DirectPipe.exe");
    const auto updateExe = testDir_.getChildFile("DirectPipe_update.exe");
    const auto backupExe = testDir_.getChildFile("DirectPipe_backup.exe");
    ASSERT_TRUE(currentExe.replaceWithText("known-good executable"));
    ASSERT_TRUE(copyVersionedTestExecutableTo(updateExe));
    const auto expectedVersion = updateExe.getVersion();

    const auto script = directpipe::update_detail::buildWindowsUpdateInstallScript(
        makeSpec(false));
    const auto result = runInstallScript(script, testDir_.getChildFile("install.bat"));

    ASSERT_TRUE(result.started);
    ASSERT_TRUE(result.finished) << result.output;
    EXPECT_EQ(result.exitCode, 0) << result.output;
    ASSERT_TRUE(currentExe.existsAsFile()) << result.output;
    EXPECT_TRUE(currentExe.getVersion().startsWith(expectedVersion));
    ASSERT_TRUE(backupExe.existsAsFile()) << result.output;
    EXPECT_EQ(backupExe.loadFileAsString(), "known-good executable");
}

TEST_F(UpdateScriptTest, LiteralPercentInInstallPathIsNotEnvironmentExpanded)
{
    ASSERT_TRUE(SetEnvironmentVariableW(L"DP_UPDATER_PATH", L"expanded"));
    const auto literalDir = testDir_.getChildFile("%DP_UPDATER_PATH%");
    ASSERT_TRUE(literalDir.createDirectory());

    const auto currentExe = literalDir.getChildFile("DirectPipe.exe");
    const auto updateExe = literalDir.getChildFile("DirectPipe_update.exe");
    const auto backupExe = literalDir.getChildFile("DirectPipe_backup.exe");
    ASSERT_TRUE(currentExe.replaceWithText("known-good executable"));
    ASSERT_TRUE(copyVersionedTestExecutableTo(updateExe));
    const auto expectedVersion = expectedReleaseVersion();

    const directpipe::update_detail::WindowsUpdateInstallSpec spec {
        windowsPath(currentExe),
        windowsPath(updateExe),
        windowsPath(literalDir.getChildFile("DirectPipe_staged.exe")),
        windowsPath(backupExe),
        windowsPath(literalDir.getChildFile("_update")),
        expectedVersion,
        false,
    };
    const auto script = directpipe::update_detail::buildWindowsUpdateInstallScript(spec);
    const auto result = runInstallScript(script, testDir_.getChildFile("percent-install.bat"));
    SetEnvironmentVariableW(L"DP_UPDATER_PATH", nullptr);

    ASSERT_TRUE(result.started);
    ASSERT_TRUE(result.finished) << result.output;
    EXPECT_EQ(result.exitCode, 0) << result.output;
    EXPECT_TRUE(currentExe.getVersion().startsWith(expectedVersion));
    EXPECT_EQ(backupExe.loadFileAsString(), "known-good executable");

    const auto harmlessExe = juce::File(
        juce::SystemStats::getEnvironmentVariable("SystemRoot", "C:\\Windows"))
                                 .getChildFile("System32")
                                 .getChildFile("where.exe");
    ASSERT_TRUE(harmlessExe.copyFileTo(currentExe));
    const auto updatedFlag = literalDir.getChildFile("updated.flag");
    const auto completionScript =
        directpipe::update_detail::buildWindowsUpdateCompletionScript(
            "9.8.7", windowsPath(updatedFlag), windowsPath(currentExe));
    EXPECT_TRUE(completionScript.contains("%%DP_UPDATER_PATH%%"));

    const auto completionResult = runInstallScript(
        completionScript, testDir_.getChildFile("percent-completion.bat"));
    ASSERT_TRUE(completionResult.started);
    ASSERT_TRUE(completionResult.finished) << completionResult.output;
    EXPECT_EQ(completionResult.exitCode, 0) << completionResult.output;
    ASSERT_TRUE(updatedFlag.existsAsFile()) << completionResult.output;
    EXPECT_EQ(updatedFlag.loadFileAsString().trim(), "9.8.7");
}

TEST_F(UpdateScriptTest, FailedReplacementMoveRollsBackOriginalExecutable)
{
    const auto currentExe = testDir_.getChildFile("DirectPipe.exe");
    const auto updateExe = testDir_.getChildFile("DirectPipe_update.exe");
    ASSERT_TRUE(currentExe.replaceWithText("known-good executable"));
    ASSERT_TRUE(copyVersionedTestExecutableTo(updateExe));

    const auto lockedUpdate = CreateFileW(
        updateExe.getFullPathName().toWideCharPointer(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    ASSERT_NE(lockedUpdate, INVALID_HANDLE_VALUE);

    const auto script = directpipe::update_detail::buildWindowsUpdateInstallScript(
        makeSpec(false));
    const auto result = runInstallScript(script, testDir_.getChildFile("install.bat"));
    CloseHandle(lockedUpdate);

    ASSERT_TRUE(result.started);
    ASSERT_TRUE(result.finished) << result.output;
    EXPECT_NE(result.exitCode, 0) << result.output;
    ASSERT_TRUE(currentExe.existsAsFile()) << result.output;
    EXPECT_EQ(currentExe.loadFileAsString(), "known-good executable");
}

TEST_F(UpdateScriptTest, FailedZipExtractionPreservesOriginalExecutable)
{
    const auto currentExe = testDir_.getChildFile("DirectPipe.exe");
    ASSERT_TRUE(currentExe.replaceWithText("known-good executable"));

    const auto script = directpipe::update_detail::buildWindowsUpdateInstallScript(
        makeSpec(true));
    const auto result = runInstallScript(script, testDir_.getChildFile("install.bat"));

    ASSERT_TRUE(result.started);
    ASSERT_TRUE(result.finished) << result.output;
    EXPECT_NE(result.exitCode, 0) << result.output;
    ASSERT_TRUE(currentExe.existsAsFile()) << result.output;
    EXPECT_EQ(currentExe.loadFileAsString(), "known-good executable");
}

TEST_F(UpdateScriptTest, UndeletableOldExtractionDirectoryAbortsBeforeInstall)
{
    const auto currentExe = testDir_.getChildFile("DirectPipe.exe");
    const auto sourceExe = testDir_.getChildFile("archive-source.exe");
    const auto updateZip = testDir_.getChildFile("DirectPipe_update.zip");
    const auto updateDir = testDir_.getChildFile("_update");
    const auto staleExe = updateDir.getChildFile("DirectPipe.exe");
    ASSERT_TRUE(currentExe.replaceWithText("known-good executable"));
    ASSERT_TRUE(sourceExe.replaceWithText("new executable from zip"));
    ASSERT_TRUE(updateDir.createDirectory());
    ASSERT_TRUE(staleExe.replaceWithText("stale executable"));

    {
        juce::ZipFile::Builder zipBuilder;
        zipBuilder.addFile(sourceExe, 9, "nested/DirectPipe.exe");
        juce::FileOutputStream zipOutput(updateZip);
        ASSERT_TRUE(zipOutput.openedOk());
        ASSERT_TRUE(zipBuilder.writeToStream(zipOutput, nullptr));
        zipOutput.flush();
    }

    const auto lockedStale = CreateFileW(
        staleExe.getFullPathName().toWideCharPointer(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    ASSERT_NE(lockedStale, INVALID_HANDLE_VALUE);

    const auto script = directpipe::update_detail::buildWindowsUpdateInstallScript(
        makeSpec(true));
    const auto result = runInstallScript(script, testDir_.getChildFile("install.bat"));
    CloseHandle(lockedStale);

    ASSERT_TRUE(result.started);
    ASSERT_TRUE(result.finished) << result.output;
    EXPECT_NE(result.exitCode, 0) << result.output;
    ASSERT_TRUE(currentExe.existsAsFile()) << result.output;
    EXPECT_EQ(currentExe.loadFileAsString(), "known-good executable");
}

TEST_F(UpdateScriptTest, SuccessfulZipUpdateStagesBeforeReplacingExecutable)
{
    const auto currentExe = testDir_.getChildFile("DirectPipe.exe");
    const auto sourceExe = testDir_.getChildFile("archive-source.exe");
    const auto updateZip = testDir_.getChildFile("DirectPipe_update.zip");
    const auto backupExe = testDir_.getChildFile("DirectPipe_backup.exe");
    ASSERT_TRUE(currentExe.replaceWithText("known-good executable"));
    ASSERT_TRUE(copyVersionedTestExecutableTo(sourceExe));
    const auto expectedVersion = sourceExe.getVersion();

    {
        juce::ZipFile::Builder zipBuilder;
        zipBuilder.addFile(sourceExe, 9, "nested/DirectPipe.exe");
        juce::FileOutputStream zipOutput(updateZip);
        ASSERT_TRUE(zipOutput.openedOk());
        ASSERT_TRUE(zipBuilder.writeToStream(zipOutput, nullptr));
        zipOutput.flush();
    }

    const auto script = directpipe::update_detail::buildWindowsUpdateInstallScript(
        makeSpec(true));
    const auto result = runInstallScript(script, testDir_.getChildFile("install.bat"));

    ASSERT_TRUE(result.started);
    ASSERT_TRUE(result.finished) << result.output;
    EXPECT_EQ(result.exitCode, 0) << result.output;
    ASSERT_TRUE(currentExe.existsAsFile()) << result.output;
    EXPECT_TRUE(currentExe.getVersion().startsWith(expectedVersion));
    ASSERT_TRUE(backupExe.existsAsFile()) << result.output;
    EXPECT_EQ(backupExe.loadFileAsString(), "known-good executable");
}

TEST_F(UpdateScriptTest, ZipWithoutExecutablePreservesOriginalExecutable)
{
    const auto currentExe = testDir_.getChildFile("DirectPipe.exe");
    const auto sourceText = testDir_.getChildFile("readme.txt");
    const auto updateZip = testDir_.getChildFile("DirectPipe_update.zip");
    ASSERT_TRUE(currentExe.replaceWithText("known-good executable"));
    ASSERT_TRUE(sourceText.replaceWithText("no executable in this archive"));

    {
        juce::ZipFile::Builder zipBuilder;
        zipBuilder.addFile(sourceText, 9, "readme.txt");
        juce::FileOutputStream zipOutput(updateZip);
        ASSERT_TRUE(zipOutput.openedOk());
        ASSERT_TRUE(zipBuilder.writeToStream(zipOutput, nullptr));
        zipOutput.flush();
    }

    const auto script = directpipe::update_detail::buildWindowsUpdateInstallScript(
        makeSpec(true));
    const auto result = runInstallScript(script, testDir_.getChildFile("install.bat"));

    ASSERT_TRUE(result.started);
    ASSERT_TRUE(result.finished) << result.output;
    EXPECT_NE(result.exitCode, 0) << result.output;
    EXPECT_EQ(currentExe.loadFileAsString(), "known-good executable");
    EXPECT_FALSE(testDir_.getChildFile("DirectPipe_backup.exe").existsAsFile());
}

TEST_F(UpdateScriptTest, ZipWithMultipleExecutablesPreservesOriginalExecutable)
{
    const auto currentExe = testDir_.getChildFile("DirectPipe.exe");
    const auto sourceExe = testDir_.getChildFile("source.exe");
    const auto updateZip = testDir_.getChildFile("DirectPipe_update.zip");
    ASSERT_TRUE(currentExe.replaceWithText("known-good executable"));
    ASSERT_TRUE(copyVersionedTestExecutableTo(sourceExe));

    {
        juce::ZipFile::Builder zipBuilder;
        zipBuilder.addFile(sourceExe, 9, "first/DirectPipe.exe");
        zipBuilder.addFile(sourceExe, 9, "second/DirectPipe.exe");
        juce::FileOutputStream zipOutput(updateZip);
        ASSERT_TRUE(zipOutput.openedOk());
        ASSERT_TRUE(zipBuilder.writeToStream(zipOutput, nullptr));
        zipOutput.flush();
    }

    const auto script = directpipe::update_detail::buildWindowsUpdateInstallScript(
        makeSpec(true));
    const auto result = runInstallScript(script, testDir_.getChildFile("install.bat"));

    ASSERT_TRUE(result.started);
    ASSERT_TRUE(result.finished) << result.output;
    EXPECT_NE(result.exitCode, 0) << result.output;
    EXPECT_EQ(currentExe.loadFileAsString(), "known-good executable");
    EXPECT_FALSE(testDir_.getChildFile("DirectPipe_backup.exe").existsAsFile());
}

TEST_F(UpdateScriptTest, ZipWithWrongExecutableVersionPreservesOriginalExecutable)
{
    const auto currentExe = testDir_.getChildFile("DirectPipe.exe");
    const auto sourceExe = testDir_.getChildFile("source.exe");
    const auto updateZip = testDir_.getChildFile("DirectPipe_update.zip");
    ASSERT_TRUE(currentExe.replaceWithText("known-good executable"));
    ASSERT_TRUE(copyVersionedTestExecutableTo(sourceExe));

    {
        juce::ZipFile::Builder zipBuilder;
        zipBuilder.addFile(sourceExe, 9, "DirectPipe.exe");
        juce::FileOutputStream zipOutput(updateZip);
        ASSERT_TRUE(zipOutput.openedOk());
        ASSERT_TRUE(zipBuilder.writeToStream(zipOutput, nullptr));
        zipOutput.flush();
    }

    auto spec = makeSpec(true);
    spec.expectedVersion = "0.0.0";
    ASSERT_NE(sourceExe.getVersion(), spec.expectedVersion);
    const auto script =
        directpipe::update_detail::buildWindowsUpdateInstallScript(spec);
    const auto result = runInstallScript(script, testDir_.getChildFile("install.bat"));

    ASSERT_TRUE(result.started);
    ASSERT_TRUE(result.finished) << result.output;
    EXPECT_NE(result.exitCode, 0) << result.output;
    EXPECT_EQ(currentExe.loadFileAsString(), "known-good executable");
    EXPECT_FALSE(testDir_.getChildFile("DirectPipe_backup.exe").existsAsFile());
}

TEST_F(UpdateScriptTest, StartupCleanupPreservesBackupAndRemovesTransientFiles)
{
    const auto batch = testDir_.getChildFile("_update.bat");
    const auto updateDir = testDir_.getChildFile("_update");
    const auto zip = testDir_.getChildFile("DirectPipe_update.zip");
    const auto staged = testDir_.getChildFile("DirectPipe_update.exe");
    const auto backup = testDir_.getChildFile("DirectPipe_backup.exe");
    const auto flag = testDir_.getChildFile("_updated.flag");

    ASSERT_TRUE(batch.replaceWithText("temporary"));
    ASSERT_TRUE(updateDir.createDirectory());
    ASSERT_TRUE(updateDir.getChildFile("stale.tmp").replaceWithText("temporary"));
    ASSERT_TRUE(zip.replaceWithText("temporary"));
    ASSERT_TRUE(staged.replaceWithText("temporary"));
    ASSERT_TRUE(backup.replaceWithText("known-good executable"));
    ASSERT_TRUE(flag.replaceWithText("4.2.7"));

    directpipe::update_detail::cleanupTransientUpdateFiles(testDir_);

    EXPECT_FALSE(batch.exists());
    EXPECT_FALSE(updateDir.exists());
    EXPECT_FALSE(zip.exists());
    EXPECT_FALSE(staged.exists());
    EXPECT_TRUE(backup.existsAsFile());
    EXPECT_EQ(backup.loadFileAsString(), "known-good executable");
    EXPECT_TRUE(flag.existsAsFile());
}

#else

TEST(UpdateScriptTest, WindowsOnly)
{
    GTEST_SKIP() << "Windows updater scripts are only exercised on Windows";
}

#endif

} // namespace
