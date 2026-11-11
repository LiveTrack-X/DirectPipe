// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <string>
#include <iostream>

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

// Only a newly-created, hidden sleep process is owned/terminated by this fixture.
// The test never uses a running DirectPipe or audio-application PID.
class OwnedUpdaterSleepProcess {
public:
    ~OwnedUpdaterSleepProcess() { stop(); }
    bool start()
    {
        const auto executable = juce::File(juce::SystemStats::getEnvironmentVariable(
            "SystemRoot", "C:\\Windows")).getChildFile("System32/WindowsPowerShell/v1.0/powershell.exe");
        const auto text = "\"" + executable.getFullPathName()
            + "\" -NoProfile -NonInteractive -Command \"Start-Sleep -Seconds 30\"";
        std::wstring command(text.toWideCharPointer());
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        if (!CreateProcessW(executable.getFullPathName().toWideCharPointer(), command.data(),
                            nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                            &startup, &process_))
            return false;
        CloseHandle(process_.hThread); process_.hThread = nullptr;
        return true;
    }
    unsigned long id() const { return process_.dwProcessId; }
    bool running() const
    {
        DWORD code = 0;
        return process_.hProcess && GetExitCodeProcess(process_.hProcess, &code)
            && code == STILL_ACTIVE;
    }
    void stop()
    {
        if (!process_.hProcess) return;
        if (running()) TerminateProcess(process_.hProcess, 0);
        WaitForSingleObject(process_.hProcess, 5000);
        CloseHandle(process_.hProcess); process_.hProcess = nullptr;
    }
private:
    PROCESS_INFORMATION process_{};
};

class ScopedUpdaterTestEnvironment {
public:
    ScopedUpdaterTestEnvironment(const wchar_t* name, const wchar_t* value) : name_(name)
    {
        const auto length = GetEnvironmentVariableW(name, nullptr, 0);
        existed_ = length > 0;
        if (existed_) {
            std::vector<wchar_t> text(length);
            GetEnvironmentVariableW(name, text.data(), length);
            previous_ = text.data();
        }
        changed_ = SetEnvironmentVariableW(name, value) != FALSE;
    }
    ~ScopedUpdaterTestEnvironment()
    {
        SetEnvironmentVariableW(name_.c_str(), existed_ ? previous_.c_str() : nullptr);
    }
    bool changed() const { return changed_; }
private:
    std::wstring name_, previous_;
    bool existed_ = false, changed_ = false;
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

    // A fresh GitHub Windows runner may spend tens of seconds on its first
    // PowerShell/VersionInfo launch (including platform scanning). The CTest
    // case has a 120-second ceiling; keep this helper below that while avoiding
    // a false product failure from cold process startup.
    result.finished = process.waitForProcessToFinish(60000);
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
        if (HasFailure()) {
            std::cerr << "Retained isolated updater fixture: " << testDir_.getFullPathName() << std::endl;
            return;
        }
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

    directpipe::update_detail::WindowsUpdateInstallSpec diagnosticSpec() const
    {
        auto spec = makeSpec(false);
        spec.resultFilePath = windowsPath(testDir_.getChildFile("result.json"));
        spec.logFilePath = windowsPath(testDir_.getChildFile("update.log"));
        spec.previousBackupExePath = windowsPath(testDir_.getChildFile("previous-backup.exe"));
        spec.updatedFlagPath = windowsPath(testDir_.getChildFile("updated.flag"));
        return spec;
    }

    juce::File testDir_;
};

#if defined(DIRECTPIPE_TEST_STARTUP_PROBE_PATH)
// Only a copied no-audio WinExe fixture may be observed here. The real app and
// arbitrary ACK PIDs are never waited on, stopped, or launched by these tests.
class OwnedStartupProbeObservation {
public:
    explicit OwnedStartupProbeObservation(juce::File executable) : executable_(std::move(executable)) {}
    ~OwnedStartupProbeObservation()
    {
        const auto pidFile = executable_.getSiblingFile("startup-probe.pid");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!pidFile.existsAsFile() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const DWORD pid = static_cast<DWORD>(pidFile.loadFileAsString().getIntValue());
        if (!pid) return;
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
        if (!process) return;
        wchar_t name[32768]{}; DWORD length = 32768;
        if (QueryFullProcessImageNameW(process, 0, name, &length)
            && juce::String(name).equalsIgnoreCase(executable_.getFullPathName()))
            EXPECT_EQ(WAIT_OBJECT_0, WaitForSingleObject(process, 6500));
        CloseHandle(process);
    }
private:
    juce::File executable_;
};

bool writeStartupProbeConfiguration(const juce::File& installedExecutable,
                                   const juce::String& ackPath, const juce::String& mode,
                                   const juce::String& version, unsigned long wrongPid = 0)
{
    const auto ini = installedExecutable.getSiblingFile("startup-probe.ini").getFullPathName();
    const auto set = [&](const wchar_t* key, const juce::String& value) {
        return WritePrivateProfileStringW(L"StartupProbe", key, value.toWideCharPointer(), ini.toWideCharPointer()) != FALSE;
    };
    return set(L"AckPath", ackPath) && set(L"Mode", mode) && set(L"Version", version)
        && set(L"WrongPid", juce::String(wrongPid)) && set(L"LifetimeMs", "4000");
}

class UpdateStartupScriptTest : public UpdateScriptTest {
protected:
    void SetUp() override
    {
        UpdateScriptTest::SetUp();
        spec = diagnosticSpec();
        const juce::File probe(DIRECTPIPE_TEST_STARTUP_PROBE_PATH);
        ASSERT_TRUE(probe.existsAsFile());
        const auto parts = juce::StringArray::fromTokens(probe.getVersion(), ".", "");
        ASSERT_GE(parts.size(), 3);
        spec.expectedVersion = parts[0] + "." + parts[1] + "." + parts[2];
        spec.requireStartupAck = true;
        spec.relaunchHostAfterUpdate = true;
        spec.showInteractiveErrors = false;
        spec.startupAttemptId = juce::Uuid().toString();
        const juce::File receipt(spec.resultFilePath);
        spec.startupAckFilePath = receipt.getSiblingFile(receipt.getFileNameWithoutExtension()
            + "." + spec.startupAttemptId + ".startup.json").getFullPathName();
        spec.startupAckTimeoutMs = 1200;
        ASSERT_TRUE(juce::File(spec.currentExePath).replaceWithText("current known-good executable"));
        ASSERT_TRUE(juce::File(spec.backupExePath).replaceWithText("previous known-good backup"));
        ASSERT_TRUE(probe.copyFileTo(juce::File(spec.downloadedFilePath)));
        candidateHash = juce::SHA256(juce::File(spec.downloadedFilePath)).toHexString();
    }
    ScriptRunResult run(const juce::String& mode)
    {
        EXPECT_TRUE(writeStartupProbeConfiguration(juce::File(spec.currentExePath), spec.startupAckFilePath,
            mode, spec.expectedVersion, GetCurrentProcessId())); // Live but wrong executable identity.
        return runInstallScript(directpipe::update_detail::buildWindowsHostUpdateScript(spec),
            testDir_.getChildFile("startup-update.bat"));
    }
    void expectStartupFailure(const juce::String& mode)
    {
        OwnedStartupProbeObservation observation(juce::File(spec.currentExePath));
        const auto began = std::chrono::steady_clock::now();
        const auto execution = run(mode);
        ASSERT_TRUE(execution.started && execution.finished) << execution.output;
        EXPECT_NE(0, execution.exitCode) << execution.output;
        EXPECT_LT(std::chrono::steady_clock::now() - began, std::chrono::seconds(15));
        const auto receipt = juce::JSON::parse(juce::File(spec.resultFilePath));
        ASSERT_TRUE(receipt.isObject()) << execution.output;
        EXPECT_FALSE(static_cast<bool>(receipt["success"]));
        EXPECT_TRUE(static_cast<bool>(receipt["filesCommitted"]));
        EXPECT_FALSE(static_cast<bool>(receipt["startupReady"]));
        EXPECT_EQ(juce::String("startup-wait"), receipt["stage"].toString());
        EXPECT_TRUE(receipt["error"].toString().isNotEmpty());
        EXPECT_EQ(candidateHash, juce::SHA256(juce::File(spec.currentExePath)).toHexString());
        EXPECT_EQ(juce::String("current known-good executable"), juce::File(spec.backupExePath).loadFileAsString());
        EXPECT_EQ(juce::String("previous known-good backup"), juce::File(spec.previousBackupExePath).loadFileAsString());
        EXPECT_FALSE(juce::File(spec.updatedFlagPath).exists());
    }
    directpipe::update_detail::WindowsUpdateInstallSpec spec;
    juce::String candidateHash;
};

TEST_F(UpdateStartupScriptTest, ActualDesktopLaunchAcknowledgementRemovesOnlyManagedBackups)
{
    const auto manual = testDir_.getChildFile("DirectPipe-4.0.0.manual-backup.exe");
    ASSERT_TRUE(manual.replaceWithText("manual backup stays"));
    OwnedStartupProbeObservation observation(juce::File(spec.currentExePath));
    const auto execution = run("ready");
    ASSERT_TRUE(execution.started && execution.finished) << execution.output;
    EXPECT_EQ(0, execution.exitCode) << execution.output;
    const auto ack = juce::JSON::parse(juce::File(spec.startupAckFilePath));
    EXPECT_EQ(spec.startupAttemptId, ack["attemptId"].toString());
    const auto receipt = juce::JSON::parse(juce::File(spec.resultFilePath));
    EXPECT_TRUE(static_cast<bool>(receipt["success"]));
    EXPECT_TRUE(static_cast<bool>(receipt["filesCommitted"]));
    EXPECT_TRUE(static_cast<bool>(receipt["startupReady"]));
    EXPECT_EQ(spec.startupAttemptId, receipt["startupAttemptId"].toString());
    EXPECT_FALSE(juce::File(spec.backupExePath).exists());
    EXPECT_FALSE(juce::File(spec.previousBackupExePath).exists());
    EXPECT_EQ(juce::String("manual backup stays"), manual.loadFileAsString());
    EXPECT_EQ(candidateHash, juce::SHA256(juce::File(spec.currentExePath)).toHexString());
}

TEST_F(UpdateStartupScriptTest, MissingAcknowledgementRetainsRecoveryWithoutRollingBackRunningCandidate) { expectStartupFailure("no-ack"); }
TEST_F(UpdateStartupScriptTest, StaleAcknowledgementCannotConfirmStartup) { expectStartupFailure("stale-ack"); }
TEST_F(UpdateStartupScriptTest, WrongAcknowledgedExecutableCannotConfirmStartup) { expectStartupFailure("wrong-path"); }
TEST_F(UpdateStartupScriptTest, WrongAcknowledgedVersionCannotConfirmStartup) { expectStartupFailure("wrong-version"); }
TEST_F(UpdateStartupScriptTest, AnotherLiveProcessCannotImpersonateCandidate) { expectStartupFailure("wrong-pid"); }
TEST_F(UpdateStartupScriptTest, ExitedCandidateCannotConfirmStartup) { expectStartupFailure("dead-ack"); }
TEST_F(UpdateStartupScriptTest, NegativeReadinessAcknowledgementRetainsRecovery) { expectStartupFailure("failed"); }

TEST_F(UpdateStartupScriptTest, BlockedReadyReceiptKeepsBothBackupsDespiteValidLiveAcknowledgement)
{
    OwnedStartupProbeObservation observation(juce::File(spec.currentExePath));
    const auto execution = run("block-receipt");
    ASSERT_TRUE(execution.started && execution.finished) << execution.output;
    EXPECT_NE(0, execution.exitCode) << execution.output;
    const auto ack = juce::JSON::parse(juce::File(spec.startupAckFilePath));
    EXPECT_EQ(spec.startupAttemptId, ack["attemptId"].toString());
    EXPECT_TRUE(static_cast<bool>(ack["ready"]));
    EXPECT_TRUE(juce::File(spec.resultFilePath + ".tmp").isDirectory());
    EXPECT_EQ(candidateHash, juce::SHA256(juce::File(spec.currentExePath)).toHexString());
    EXPECT_EQ(juce::String("current known-good executable"), juce::File(spec.backupExePath).loadFileAsString());
    EXPECT_EQ(juce::String("previous known-good backup"), juce::File(spec.previousBackupExePath).loadFileAsString());
    EXPECT_FALSE(juce::File(spec.updatedFlagPath).exists());
    const auto retained = juce::JSON::parse(juce::File(spec.resultFilePath));
    EXPECT_FALSE(static_cast<bool>(retained["startupReady"])); // Last durable state cannot falsely prove readiness.
    EXPECT_TRUE(static_cast<bool>(retained["inProgress"]));
    EXPECT_TRUE(juce::File(spec.logFilePath).loadFileAsString().contains("startup-cleanup"));
}

TEST_F(UpdateStartupScriptTest, RejectsMissingOrMalformedAttemptBeforeGeneratingInstaller)
{
    spec.startupAttemptId = "";
    EXPECT_TRUE(directpipe::update_detail::buildWindowsHostUpdateScript(spec).isEmpty());
    spec.startupAttemptId = juce::String::repeatedString("0", 32);
    EXPECT_TRUE(directpipe::update_detail::buildWindowsHostUpdateScript(spec).isEmpty());
    spec.startupAttemptId = juce::Uuid().toString();
    spec.startupAckFilePath = testDir_.getChildFile("unrelated.json").getFullPathName();
    EXPECT_TRUE(directpipe::update_detail::buildWindowsHostUpdateScript(spec).isEmpty());
}
#endif

TEST_F(UpdateScriptTest, LockedCurrentPreservesPriorBackupAndRecordsFailure)
{
    const auto spec = diagnosticSpec();
    const juce::File current(spec.currentExePath), backup(spec.backupExePath);
    ASSERT_TRUE(current.replaceWithText("current known-good executable"));
    ASSERT_TRUE(backup.replaceWithText("previous known-good backup"));
    ASSERT_TRUE(copyVersionedTestExecutableTo(juce::File(spec.downloadedFilePath)));
    const auto lock = CreateFileW(current.getFullPathName().toWideCharPointer(),
        GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(lock, INVALID_HANDLE_VALUE);
    const auto run = runInstallScript(directpipe::update_detail::buildWindowsHostUpdateScript(spec),
                                      testDir_.getChildFile("locked-current.bat"));
    CloseHandle(lock);
    ASSERT_TRUE(run.started && run.finished) << run.output;
    EXPECT_NE(0, run.exitCode);
    EXPECT_EQ(juce::String("current known-good executable"), current.loadFileAsString());
    EXPECT_EQ(juce::String("previous known-good backup"), backup.loadFileAsString());
    EXPECT_FALSE(juce::File(spec.updatedFlagPath).exists());
    EXPECT_TRUE(juce::File(spec.downloadedFilePath).existsAsFile());
    const auto result = juce::JSON::parse(juce::File(spec.resultFilePath));
    ASSERT_TRUE(result.isObject()) << run.output;
    EXPECT_FALSE(static_cast<bool>(result["success"]));
    EXPECT_EQ(juce::String("move-original"), result["stage"].toString());
    EXPECT_TRUE(static_cast<bool>(result["rollbackSucceeded"]));
    EXPECT_TRUE(result["error"].toString().isNotEmpty());
    EXPECT_TRUE(juce::File(spec.logFilePath).loadFileAsString().contains("move-original"));
}

TEST_F(UpdateScriptTest, ReplacementFailureRestoresCurrentAndPriorBackupWithResult)
{
    const auto spec = diagnosticSpec();
    const juce::File current(spec.currentExePath), backup(spec.backupExePath), candidate(spec.downloadedFilePath);
    ASSERT_TRUE(current.replaceWithText("current known-good executable"));
    ASSERT_TRUE(backup.replaceWithText("previous known-good backup"));
    ASSERT_TRUE(copyVersionedTestExecutableTo(candidate));
    const auto lock = CreateFileW(candidate.getFullPathName().toWideCharPointer(),
        GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(lock, INVALID_HANDLE_VALUE);
    const auto run = runInstallScript(directpipe::update_detail::buildWindowsHostUpdateScript(spec),
                                      testDir_.getChildFile("locked-candidate.bat"));
    CloseHandle(lock);
    ASSERT_TRUE(run.started && run.finished) << run.output;
    EXPECT_NE(0, run.exitCode);
    EXPECT_EQ(juce::String("current known-good executable"), current.loadFileAsString());
    EXPECT_EQ(juce::String("previous known-good backup"), backup.loadFileAsString());
    EXPECT_FALSE(juce::File(spec.updatedFlagPath).exists());
    const auto result = juce::JSON::parse(juce::File(spec.resultFilePath));
    ASSERT_TRUE(result.isObject()) << run.output;
    EXPECT_EQ(juce::String("replace"), result["stage"].toString());
    EXPECT_TRUE(static_cast<bool>(result["rollbackSucceeded"]));
}

TEST_F(UpdateScriptTest, InvalidCandidateRecordsValidationWithoutChangingBackups)
{
    auto spec = diagnosticSpec();
    const juce::File current(spec.currentExePath), backup(spec.backupExePath);
    ASSERT_TRUE(current.replaceWithText("current known-good executable"));
    ASSERT_TRUE(backup.replaceWithText("previous known-good backup"));
    ASSERT_TRUE(copyVersionedTestExecutableTo(juce::File(spec.downloadedFilePath)));
    spec.expectedVersion = "0.0.0";
    const auto run = runInstallScript(directpipe::update_detail::buildWindowsHostUpdateScript(spec),
                                      testDir_.getChildFile("invalid-candidate.bat"));
    ASSERT_TRUE(run.started && run.finished) << run.output;
    EXPECT_NE(0, run.exitCode);
    EXPECT_EQ(juce::String("current known-good executable"), current.loadFileAsString());
    EXPECT_EQ(juce::String("previous known-good backup"), backup.loadFileAsString());
    EXPECT_FALSE(juce::File(spec.updatedFlagPath).exists());
    const auto result = juce::JSON::parse(juce::File(spec.resultFilePath));
    ASSERT_TRUE(result.isObject()) << run.output;
    EXPECT_EQ(juce::String("validate"), result["stage"].toString());
    EXPECT_TRUE(result["error"].toString().containsIgnoreCase("version"));
}

TEST_F(UpdateScriptTest, SuccessfulHostOperationRecordsCommitAndSuccessFlag)
{
    const auto spec = diagnosticSpec();
    const juce::File current(spec.currentExePath), backup(spec.backupExePath), candidate(spec.downloadedFilePath);
    ASSERT_TRUE(current.replaceWithText("current known-good executable"));
    ASSERT_TRUE(backup.replaceWithText("previous known-good backup"));
    ASSERT_TRUE(copyVersionedTestExecutableTo(candidate));
    const auto expectedHash = juce::SHA256(candidate).toHexString();
    const auto run = runInstallScript(directpipe::update_detail::buildWindowsHostUpdateScript(spec),
                                      testDir_.getChildFile("successful-host.bat"));
    ASSERT_TRUE(run.started && run.finished) << run.output;
    EXPECT_EQ(0, run.exitCode) << run.output;
    EXPECT_EQ(expectedHash, juce::SHA256(current).toHexString());
    EXPECT_EQ(juce::String("current known-good executable"), backup.loadFileAsString());
    EXPECT_EQ(spec.expectedVersion, juce::File(spec.updatedFlagPath).loadFileAsString().trim());
    EXPECT_FALSE(juce::File(spec.previousBackupExePath).exists());
    const auto result = juce::JSON::parse(juce::File(spec.resultFilePath));
    ASSERT_TRUE(result.isObject()) << run.output;
    EXPECT_TRUE(static_cast<bool>(result["success"]));
    EXPECT_EQ(juce::String("complete"), result["stage"].toString());
    EXPECT_TRUE(juce::File(spec.logFilePath).loadFileAsString().contains("complete"));
}

TEST_F(UpdateScriptTest, OwnedPidWaitTimesOutWithoutKillingOrInstalling)
{
    auto spec = diagnosticSpec();
    const juce::File current(spec.currentExePath), backup(spec.backupExePath);
    ASSERT_TRUE(current.replaceWithText("current known-good executable"));
    ASSERT_TRUE(backup.replaceWithText("previous known-good backup"));
    ASSERT_TRUE(copyVersionedTestExecutableTo(juce::File(spec.downloadedFilePath)));
    OwnedUpdaterSleepProcess target;
    ASSERT_TRUE(target.start());
    spec.processId = target.id(); spec.processWaitTimeoutMs = 250;
    const auto batch = testDir_.getChildFile("wait-timeout.bat");
    ASSERT_TRUE(batch.replaceWithText("@echo off\r\n"
        + directpipe::update_detail::buildWindowsHostUpdateScript(spec) + "exit /b 0\r\n"));
    juce::ChildProcess installer;
    ASSERT_TRUE(installer.start("cmd.exe /d /c \"\"" + batch.getFullPathName() + "\"\""));
    const bool bounded = installer.waitForProcessToFinish(6000);
    const bool targetWasKeptAlive = target.running();
    target.stop(); // Only the owned sleep fixture, after observing the bounded result.
    if (!bounded) ASSERT_TRUE(installer.waitForProcessToFinish(15000));
    const auto output = installer.readAllProcessOutput();
    EXPECT_TRUE(bounded) << output;
    EXPECT_TRUE(targetWasKeptAlive);
    EXPECT_NE(0, installer.getExitCode()) << output;
    EXPECT_EQ(juce::String("current known-good executable"), current.loadFileAsString());
    EXPECT_EQ(juce::String("previous known-good backup"), backup.loadFileAsString());
    EXPECT_FALSE(juce::File(spec.updatedFlagPath).exists());
    const auto result = juce::JSON::parse(juce::File(spec.resultFilePath));
    ASSERT_TRUE(result.isObject()) << output;
    EXPECT_EQ(juce::String("wait"), result["stage"].toString());
    EXPECT_TRUE(result["error"].toString().containsIgnoreCase("tim"));
    EXPECT_TRUE(juce::File(spec.logFilePath).loadFileAsString().contains("wait"));
}

TEST_F(UpdateScriptTest, RollbackFailureRetainsBothRecoveryFilesAndReportsIt)
{
    const auto spec = diagnosticSpec();
    const juce::File current(spec.currentExePath), backup(spec.backupExePath);
    ASSERT_TRUE(current.replaceWithText("current known-good executable"));
    ASSERT_TRUE(backup.replaceWithText("previous known-good backup"));
    ASSERT_TRUE(copyVersionedTestExecutableTo(juce::File(spec.downloadedFilePath)));
    auto script = directpipe::update_detail::buildWindowsHostUpdateScript(spec);
    script = script.replace("rem DIRECTPIPE_HOST_BEFORE_REPLACE",
        "mkdir \"" + windowsPath(current) + "\"");
    const auto run = runInstallScript(script, testDir_.getChildFile("rollback-failure.bat"));
    ASSERT_TRUE(run.started && run.finished) << run.output;
    EXPECT_NE(0, run.exitCode);
    EXPECT_TRUE(current.isDirectory());
    EXPECT_EQ(backup.loadFileAsString(), "current known-good executable");
    EXPECT_EQ(juce::File(spec.previousBackupExePath).loadFileAsString(), "previous known-good backup");
    EXPECT_TRUE(juce::File(spec.downloadedFilePath).existsAsFile());
    EXPECT_FALSE(juce::File(spec.updatedFlagPath).exists());
    const auto result = juce::JSON::parse(juce::File(spec.resultFilePath));
    ASSERT_TRUE(result.isObject()) << run.output;
    EXPECT_FALSE(static_cast<bool>(result["rollbackSucceeded"]));
    EXPECT_FALSE(static_cast<bool>(result["currentRestored"]));
    EXPECT_FALSE(static_cast<bool>(result["previousBackupRestored"]));
    EXPECT_TRUE(result["recoveryPaths"].isArray());
}

TEST_F(UpdateScriptTest, WrongPackageChecksumPreservesFilesAndRecordsValidation)
{
    auto spec = diagnosticSpec();
    const juce::File current(spec.currentExePath), backup(spec.backupExePath);
    ASSERT_TRUE(current.replaceWithText("current known-good executable"));
    ASSERT_TRUE(backup.replaceWithText("previous known-good backup"));
    ASSERT_TRUE(copyVersionedTestExecutableTo(juce::File(spec.downloadedFilePath)));
    spec.expectedPackageSha256 = juce::String::repeatedString("0", 64);
    const auto run = runInstallScript(directpipe::update_detail::buildWindowsHostUpdateScript(spec),
                                      testDir_.getChildFile("wrong-checksum.bat"));
    ASSERT_TRUE(run.started && run.finished) << run.output;
    EXPECT_NE(0, run.exitCode);
    EXPECT_EQ(current.loadFileAsString(), "current known-good executable");
    EXPECT_EQ(backup.loadFileAsString(), "previous known-good backup");
    EXPECT_TRUE(juce::File(spec.downloadedFilePath).existsAsFile());
    const auto result = juce::JSON::parse(juce::File(spec.resultFilePath));
    ASSERT_TRUE(result.isObject()) << run.output;
    EXPECT_EQ(result["stage"].toString(), "validate");
    EXPECT_TRUE(result["error"].toString().containsIgnoreCase("checksum"));
}

TEST_F(UpdateScriptTest, FullHostLauncherKeepsPercentAndUnicodePathsLiteral)
{
    ScopedUpdaterTestEnvironment environment(L"DP_UPDATER_PATH", L"expanded");
    ASSERT_TRUE(environment.changed());
    const auto directory = testDir_.getChildFile(
        juce::String::fromUTF8(u8"O'Brien %DP_UPDATER_PATH% $& \uC74C\uC131"));
    ASSERT_TRUE(directory.createDirectory());
    auto spec = makeSpec(false);
    spec.currentExePath = windowsPath(directory.getChildFile("DirectPipe.exe"));
    spec.downloadedFilePath = windowsPath(directory.getChildFile("DirectPipe_update.exe"));
    spec.stagedExePath = windowsPath(directory.getChildFile("DirectPipe_staged.exe"));
    spec.backupExePath = windowsPath(directory.getChildFile("DirectPipe_backup.exe"));
    spec.updateDirPath = windowsPath(directory.getChildFile("expanded"));
    spec.resultFilePath = windowsPath(directory.getChildFile("result.json"));
    spec.logFilePath = windowsPath(directory.getChildFile("update.log"));
    spec.previousBackupExePath = windowsPath(directory.getChildFile("previous-backup.exe"));
    spec.updatedFlagPath = windowsPath(directory.getChildFile("updated.flag"));
    ASSERT_TRUE(juce::File(spec.currentExePath).replaceWithText("current known-good executable"));
    ASSERT_TRUE(juce::File(spec.backupExePath).replaceWithText("previous known-good backup"));
    ASSERT_TRUE(copyVersionedTestExecutableTo(juce::File(spec.downloadedFilePath)));
    const auto expectedHash = juce::SHA256(juce::File(spec.downloadedFilePath)).toHexString();
    const auto batch = directory.getChildFile("install.bat");
    ASSERT_TRUE(batch.replaceWithText("@echo off\r\n"
        + directpipe::update_detail::buildWindowsHostUpdateScript(spec)));
    const auto powershell = juce::File(juce::SystemStats::getEnvironmentVariable(
        "SystemRoot", "C:\\Windows")).getChildFile("System32/WindowsPowerShell/v1.0/powershell.exe");
    const auto arguments = directpipe::update_detail::buildWindowsHostUpdateLauncherArguments(batch.getFullPathName());
    ASSERT_TRUE(arguments.isNotEmpty());
    juce::ChildProcess launcher;
    ASSERT_TRUE(launcher.start("\"" + powershell.getFullPathName() + "\" " + arguments));
    ASSERT_TRUE(launcher.waitForProcessToFinish(60000));
    const auto output = launcher.readAllProcessOutput();
    EXPECT_EQ(0, launcher.getExitCode()) << output;
    EXPECT_EQ(expectedHash, juce::SHA256(juce::File(spec.currentExePath)).toHexString());
    EXPECT_EQ(juce::File(spec.backupExePath).loadFileAsString(), "current known-good executable");
    EXPECT_EQ(juce::File(spec.updatedFlagPath).loadFileAsString().trim(), spec.expectedVersion);
    EXPECT_TRUE(static_cast<bool>(juce::JSON::parse(juce::File(spec.resultFilePath))["success"]));
    EXPECT_TRUE(juce::File(spec.logFilePath).loadFileAsString().contains("complete"));
    EXPECT_FALSE(testDir_.getChildFile("O'Brien expanded $& \uC74C\uC131").exists());
}

TEST_F(UpdateScriptTest, LateReceiptWriteFailureKeepsCommittedFilesAndReportsCompletion)
{
    const auto spec = diagnosticSpec();
    const juce::File current(spec.currentExePath), backup(spec.backupExePath);
    ASSERT_TRUE(current.replaceWithText("current known-good executable"));
    ASSERT_TRUE(backup.replaceWithText("previous known-good backup"));
    ASSERT_TRUE(copyVersionedTestExecutableTo(juce::File(spec.downloadedFilePath)));
    const auto expected = juce::SHA256(juce::File(spec.downloadedFilePath)).toHexString();
    auto script = directpipe::update_detail::buildWindowsHostUpdateScript(spec);
    script = script.replace("rem DIRECTPIPE_HOST_BEFORE_COMPLETION_RESULT",
        "mkdir \"" + (spec.resultFilePath + ".tmp").replace("%", "%%") + "\"");
    const auto run = runInstallScript(script, testDir_.getChildFile("late-receipt-failure.bat"));
    ASSERT_TRUE(run.started && run.finished) << run.output;
    EXPECT_EQ(2, run.exitCode);
    EXPECT_EQ(expected, juce::SHA256(current).toHexString());
    EXPECT_EQ(backup.loadFileAsString(), "current known-good executable");
    EXPECT_TRUE(juce::File(spec.updatedFlagPath).existsAsFile());
    EXPECT_TRUE(static_cast<bool>(juce::JSON::parse(juce::File(spec.resultFilePath))["success"]));
    EXPECT_TRUE(juce::File(spec.logFilePath).loadFileAsString().contains("files committed; completion needs attention"));
}

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
    const auto currentExe = testDir_.getChildFile("DirectPipe.exe");
    const auto updateExe = testDir_.getChildFile("DirectPipe_update.exe");
    const auto backupExe = testDir_.getChildFile("DirectPipe_backup.exe");
    ASSERT_TRUE(copyVersionedTestExecutableTo(updateExe));
    ASSERT_TRUE(backupExe.replaceWithText("previous known-good executable"));

    const auto script = directpipe::update_detail::buildWindowsUpdateInstallScript(
        makeSpec(false));
    const auto missingCurrentCheck =
        "if not exist \"" + windowsPath(currentExe) + "\" goto update_install_failed";
    const auto missingCurrentOffset = script.indexOf(missingCurrentCheck);
    const auto versionValidationOffset =
        script.indexOf("powershell -NoProfile -Command");
    ASSERT_GE(missingCurrentOffset, 0);
    ASSERT_GE(versionValidationOffset, 0);
    EXPECT_LT(missingCurrentOffset, versionValidationOffset);

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
