// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
//
// This file is part of DirectPipe.
//
// DirectPipe is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// DirectPipe is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with DirectPipe. If not, see <https://www.gnu.org/licenses/>.

/**
 * @file UpdateChecker.cpp
 * @brief Background update checker implementation
 */

#include "UpdateChecker.h"
#include "UpdateScript.h"

#include <exception>

#if JUCE_WINDOWS
#include <windows.h>
namespace {
    constexpr const char* kUpdateBatchFile = "_update.bat";
    constexpr const char* kUpdateDir       = "_update";
    constexpr const char* kUpdateZip       = "DirectPipe_update.zip";
    constexpr const char* kUpdateExe       = "DirectPipe_update.exe";
    constexpr const char* kBackupExe       = "DirectPipe_backup.exe";
    constexpr const char* kUpdatedFlag     = "_updated.flag";
}
#endif

namespace directpipe {

bool update_detail::releaseRequiresChecksum(const juce::String& version)
{
    std::array<int, 3> components{};
    juce::String canonical;
    return parseStrictReleaseVersion(version, components, canonical)
        && components >= std::array<int, 3>{4, 2, 0};
}

bool update_detail::parseExpectedSha256(const juce::String& checksumContent,
                                        const juce::String& assetName,
                                        juce::String& expectedHash)
{
    expectedHash.clear();
    for (auto line : juce::StringArray::fromLines(checksumContent)) {
        line = line.trim();
        const int separator = line.indexOfAnyOf(" \t");
        if (separator <= 0)
            continue;

        auto hash = line.substring(0, separator).trim().toLowerCase();
        auto filename = line.substring(separator).trim();
        if (filename.startsWithChar('*'))
            filename = filename.substring(1);
        if (filename != assetName)
            continue;

        if (hash.length() != 64
            || !hash.containsOnly("0123456789abcdef"))
            return false;
        expectedHash = hash;
        return true;
    }
    return false;
}

UpdateChecker::UpdateChecker() = default;

UpdateChecker::~UpdateChecker()
{
    alive_->store(false);
#if JUCE_WINDOWS
    {
        std::lock_guard<std::mutex> lock(downloadThreadMutex_);
        if (downloadThread_.joinable())
            downloadThread_.join();
        downloadInProgress_.store(false, std::memory_order_release);
        downloadThreadFinished_.store(false, std::memory_order_release);
    }
#endif
    {
        std::lock_guard<std::mutex> lock(updateCheckThreadMutex_);
        if (updateCheckThread_.joinable())
            updateCheckThread_.join();
    }
}

bool UpdateChecker::startUpdateCheckWorker(std::function<void()> work)
{
    if (!work)
        return false;

    std::lock_guard<std::mutex> lock(updateCheckThreadMutex_);
    if (updateCheckInProgress_.load(std::memory_order_acquire))
        return false;
    if (updateCheckThread_.joinable())
        updateCheckThread_.join();

    updateCheckThreadFinished_.store(false, std::memory_order_release);
    updateCheckInProgress_.store(true, std::memory_order_release);
    try {
        updateCheckThread_ = std::thread([this, work = std::move(work)]() mutable {
            try {
                work();
            } catch (const std::exception& error) {
                setCheckFailure(UpdateCheckStatus::InvalidResponse,
                                "Update check exception: " + juce::String(error.what()));
            } catch (...) {
                setCheckFailure(UpdateCheckStatus::InvalidResponse,
                                "Update check failed with an unknown exception");
            }
            updateCheckThreadFinished_.store(true, std::memory_order_release);
            updateCheckInProgress_.store(false, std::memory_order_release);
        });
    } catch (...) {
        updateCheckInProgress_.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void UpdateChecker::reapFinishedUpdateCheckThread()
{
    std::lock_guard<std::mutex> lock(updateCheckThreadMutex_);
    if (updateCheckInProgress_.load(std::memory_order_acquire))
        return;
    if (updateCheckThread_.joinable())
        updateCheckThread_.join();
    updateCheckThreadFinished_.store(false, std::memory_order_release);
}

void UpdateChecker::setCheckFailure(UpdateCheckStatus status,
                                    const juce::String& message)
{
    {
        std::lock_guard<std::mutex> lock(checkStateMutex_);
        lastCheckError_ = message;
    }
    checkStatus_.store(status, std::memory_order_release);
    juce::Logger::writeToLog("[APP] " + message);
}

uint64_t UpdateChecker::beginUpdateCheckRequest()
{
    // Invalidate any MessageManager callback queued by a completed prior check
    // before publishing the new request's observable state.
    const auto requestGeneration =
        updateCheckRequestGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
    {
        std::lock_guard<std::mutex> lock(checkStateMutex_);
        lastCheckError_.clear();
    }
    updateAvailable_.store(false, std::memory_order_release);
    checkStatus_.store(UpdateCheckStatus::Checking, std::memory_order_release);
    return requestGeneration;
}

bool UpdateChecker::postUpdateAvailable(uint64_t requestGeneration,
                                        const juce::String& version,
                                        const juce::String& downloadUrl)
{
    return juce::MessageManager::callAsync(
        [this, alive = alive_, requestGeneration, version, downloadUrl] {
            if (!alive->load(std::memory_order_acquire)
                || requestGeneration
                       != updateCheckRequestGeneration_.load(std::memory_order_acquire)) {
                return;
            }

            latestVersion_ = version;
            latestDownloadUrl_ = downloadUrl;
            updateAvailable_.store(true, std::memory_order_release);
            checkStatus_.store(UpdateCheckStatus::UpdateAvailable,
                               std::memory_order_release);
            if (onUpdateAvailable)
                onUpdateAvailable(version, downloadUrl);
        });
}

#if JUCE_WINDOWS
bool UpdateChecker::startDownloadWorker(std::function<void()> work)
{
    if (!work)
        return false;

    // Serialize the std::thread object itself. Atomics describe worker state,
    // but they cannot make "finished old thread -> assign new thread" atomic;
    // assigning over a still-joinable std::thread would call std::terminate.
    std::lock_guard<std::mutex> lock(downloadThreadMutex_);
    if (downloadInProgress_.load(std::memory_order_acquire))
        return false;

    if (downloadThread_.joinable())
        downloadThread_.join();

    downloadThreadFinished_.store(false, std::memory_order_release);
    downloadInProgress_.store(true, std::memory_order_release);
    try {
        downloadThread_ = std::thread([this, work = std::move(work)]() mutable {
            try {
                work();
            } catch (const std::exception& error) {
                juce::Logger::writeToLog(
                    "[APP] Update worker failed with exception: " + juce::String(error.what()));
            } catch (...) {
                juce::Logger::writeToLog("[APP] Update worker failed with unknown exception");
            }

            // Publish completion before clearing in-progress. The next attempt
            // can then join/reap this finished std::thread before replacing it.
            downloadThreadFinished_.store(true, std::memory_order_release);
            downloadInProgress_.store(false, std::memory_order_release);
        });
    } catch (...) {
        downloadThreadFinished_.store(false, std::memory_order_release);
        downloadInProgress_.store(false, std::memory_order_release);
        return false;
    }

    return true;
}

void UpdateChecker::reapFinishedDownloadThread()
{
    std::lock_guard<std::mutex> lock(downloadThreadMutex_);
    if (downloadInProgress_.load(std::memory_order_acquire))
        return;

    if (downloadThread_.joinable())
        downloadThread_.join();

    downloadThreadFinished_.store(false, std::memory_order_release);
}
#endif

void UpdateChecker::cleanupPreviousUpdate()
{
#if JUCE_WINDOWS
    auto exeDir = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile).getParentDirectory();
    exeDir.getChildFile(kUpdateBatchFile).deleteFile();
    exeDir.getChildFile(kUpdateDir).deleteRecursively();
    exeDir.getChildFile(kUpdateZip).deleteFile();
    exeDir.getChildFile(kUpdateExe).deleteFile();
    exeDir.getChildFile(kBackupExe).deleteFile();

    auto flagFile = exeDir.getChildFile(kUpdatedFlag);
    if (flagFile.existsAsFile()) {
        auto version = flagFile.loadFileAsString().trim();
        flagFile.deleteFile();
        if (onPostUpdateNotification)
            onPostUpdateNotification(version);
    }
#endif
}

// ─── Check for Update (background thread) ─────────────────────────────────

void UpdateChecker::checkForUpdate()
{
    // Serialize request admission separately from the worker lifecycle mutex.
    // This keeps concurrent callers from invalidating the generation captured
    // by the one worker that actually starts.
    std::lock_guard<std::mutex> invocationLock(updateCheckInvocationMutex_);
    if (updateCheckInProgress_.load(std::memory_order_acquire)) {
        juce::Logger::writeToLog("[APP] Update check already in progress");
        return;
    }
    reapFinishedUpdateCheckThread();
    const auto currentVersion = juce::String(ProjectInfo::versionString);
    const auto requestGeneration = beginUpdateCheckRequest();

    if (!startUpdateCheckWorker([this, currentVersion, requestGeneration] {
        juce::URL url("https://api.github.com/repos/LiveTrack-X/DirectPipe/releases/latest");
        int statusCode = 0;
        auto stream = url.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(10000)
                .withExtraHeaders("User-Agent: DirectPipe-Updater\r\n"
                                  "Accept: application/vnd.github+json\r\n")
                .withStatusCode(&statusCode));
        if (!stream) {
            setCheckFailure(statusCode > 0 ? UpdateCheckStatus::ApiError
                                           : UpdateCheckStatus::NetworkError,
                            statusCode > 0
                                ? "GitHub release API returned HTTP " + juce::String(statusCode)
                                : "Could not connect to the GitHub release API");
            return;
        }

        const auto response = stream->readEntireStreamAsString();
        if (statusCode != 200) {
            setCheckFailure(UpdateCheckStatus::ApiError,
                            "GitHub release API returned HTTP " + juce::String(statusCode));
            return;
        }
        if (response.isEmpty()) {
            setCheckFailure(UpdateCheckStatus::InvalidResponse,
                            "GitHub release API returned an empty response");
            return;
        }

        auto parsed = juce::JSON::parse(response);
        auto* object = parsed.getDynamicObject();
        if (object == nullptr) {
            setCheckFailure(UpdateCheckStatus::InvalidResponse,
                            "GitHub release API returned invalid JSON");
            return;
        }

        std::array<int, 3> releaseVersion{};
        std::array<int, 3> installedVersion{};
        juce::String canonicalReleaseVersion;
        juce::String canonicalInstalledVersion;
        const auto tagName = object->getProperty("tag_name").toString();
        if (!update_detail::parseStrictReleaseVersion(
                tagName, releaseVersion, canonicalReleaseVersion)) {
            setCheckFailure(UpdateCheckStatus::InvalidResponse,
                            "GitHub release has a malformed tag: " + tagName);
            return;
        }
        if (!update_detail::parseStrictReleaseVersion(
                currentVersion, installedVersion, canonicalInstalledVersion)) {
            setCheckFailure(UpdateCheckStatus::InvalidResponse,
                            "Installed version is not strict semver: " + currentVersion);
            return;
        }

        if (releaseVersion <= installedVersion) {
            checkStatus_.store(UpdateCheckStatus::UpToDate, std::memory_order_release);
            return;
        }

#if JUCE_WINDOWS
        constexpr const char* platformTag = "Windows";
        constexpr const char* packageExtension = ".zip";
#elif JUCE_MAC
        constexpr const char* platformTag = "macOS";
        constexpr const char* packageExtension = ".dmg";
#else
        constexpr const char* platformTag = "Linux";
        constexpr const char* packageExtension = ".tar.gz";
#endif
        juce::String downloadUrl;
        juce::String fallbackArchiveUrl;
        juce::String fallbackExecutableUrl;
        if (auto* assets = object->getProperty("assets").getArray()) {
            for (auto& asset : *assets) {
                auto* assetObject = asset.getDynamicObject();
                if (assetObject == nullptr)
                    continue;
                const auto name = assetObject->getProperty("name").toString();
                const auto assetUrl = assetObject->getProperty("browser_download_url").toString();
                if (name.containsIgnoreCase(platformTag)
                    && name.containsIgnoreCase("DirectPipe")
                    && name.endsWithIgnoreCase(packageExtension)) {
                    downloadUrl = assetUrl;
                    break;
                }
                if (fallbackArchiveUrl.isEmpty()
                    && name.containsIgnoreCase("DirectPipe")
                    && name.endsWithIgnoreCase(".zip"))
                    fallbackArchiveUrl = assetUrl;
                if (fallbackExecutableUrl.isEmpty()
                    && name.containsIgnoreCase("DirectPipe")
                    && name.endsWithIgnoreCase(".exe"))
                    fallbackExecutableUrl = assetUrl;
            }
        }
        if (downloadUrl.isEmpty())
            downloadUrl = fallbackArchiveUrl;
        if (downloadUrl.isEmpty())
            downloadUrl = fallbackExecutableUrl;

        if (!postUpdateAvailable(requestGeneration,
                                 canonicalReleaseVersion,
                                 downloadUrl)) {
            setCheckFailure(UpdateCheckStatus::InvalidResponse,
                            "Could not queue the update result on the message thread");
        }
    })) {
        if (updateCheckInProgress_.load(std::memory_order_acquire)) {
            // Another check won the lifecycle race. Keep its observable state.
            juce::Logger::writeToLog("[APP] Update check already in progress");
        } else {
            // std::thread construction can fail. Do not leave the UI-facing
            // state stuck at Checking when no worker exists.
            setCheckFailure(UpdateCheckStatus::InvalidResponse,
                            "Could not start the update-check worker");
        }
    }
}

void UpdateChecker::showUpdateDialog()
{
    auto* window = new juce::AlertWindow(
        "Update Available",
        "New version v" + latestVersion_ + " is available.\n"
        "Current version: v" + juce::String(ProjectInfo::versionString) + "\n\n"
        "Would you like to update?",
        juce::MessageBoxIconType::InfoIcon);

#if JUCE_WINDOWS
    window->addButton("Update Now", 1);
#endif
    window->addButton("View on GitHub", 2);
    window->addButton("Later", 0);

    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, alive = alive_](int result) {
            if (!alive->load()) return;
#if JUCE_WINDOWS
            if (result == 1) {
                performUpdate();
            } else
#endif
            if (result == 2) {
                juce::URL("https://github.com/LiveTrack-X/DirectPipe/releases/latest")
                    .launchInDefaultBrowser();
            }
        }), true);
}

// ─── Auto-Update (Windows only) ──────────────────────────────────────────

#if JUCE_WINDOWS
void UpdateChecker::performUpdate()
{
    if (latestDownloadUrl_.isEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Update Error",
            "Download URL not found.\nPlease download manually from GitHub.",
            "OK");
        juce::URL("https://github.com/LiveTrack-X/DirectPipe/releases/latest")
            .launchInDefaultBrowser();
        return;
    }

    // std::thread::joinable() remains true after a worker has returned. Reap a
    // completed attempt first, then use the explicit running flag to distinguish
    // a real in-progress download from a finished thread awaiting join().
    reapFinishedDownloadThread();
    if (downloadInProgress_.load(std::memory_order_acquire)) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Update in Progress",
            "A download is already in progress. Please wait for it to finish.",
            "OK");
        return;
    }

    // Determine paths
    auto currentExe = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile);
    auto updateDir = currentExe.getParentDirectory().getChildFile(kUpdateDir);
    auto batchFile = currentExe.getSiblingFile(kUpdateBatchFile);

    // Show progress (indeterminate spinner)
    auto progressDlg = std::make_shared<std::unique_ptr<juce::AlertWindow>>(
        std::make_unique<juce::AlertWindow>("Updating...",
            "Downloading v" + latestVersion_ + "...",
            juce::MessageBoxIconType::NoIcon));
    downloadProgress_ = -1.0;
    (*progressDlg)->addProgressBarComponent(downloadProgress_);
    (*progressDlg)->enterModalState(true, nullptr, false);

    auto downloadUrl = latestDownloadUrl_;
    auto version = latestVersion_;
    std::array<int, 3> versionComponents{};
    juce::String canonicalVersion;
    if (!update_detail::parseStrictReleaseVersion(version, versionComponents, canonicalVersion)) {
        (*progressDlg)->exitModalState(0);
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Update Error",
            "The release version is invalid. Please download manually from GitHub.",
            "OK");
        return;
    }

    version = canonicalVersion;
    bool isZip = downloadUrl.endsWithIgnoreCase(".zip");
    const auto currentProcessId = static_cast<unsigned long>(::GetCurrentProcessId());

    const bool workerStarted = startDownloadWorker(
        [alive = alive_, downloadUrl, updateDir, batchFile, currentExe,
         version, isZip, currentProcessId, progressDlg]() {
        // Download the file
        juce::URL url(downloadUrl);
        int statusCode = 0;
        auto stream = url.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(15000)
                .withStatusCode(&statusCode));

        if (!stream || statusCode != 200) {
            juce::MessageManager::callAsync([alive, progressDlg]() {
                if (!alive->load()) return;
                if (*progressDlg)
                    (*progressDlg)->exitModalState(0);
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Download Failed",
                    "Could not download the update.\nPlease try again or download manually.",
                    "OK");
            });
            return;
        }

        // Determine download target
        auto downloadFile = isZip
            ? currentExe.getSiblingFile(kUpdateZip)
            : currentExe.getSiblingFile(kUpdateExe);

        // Write to file
        {
            juce::FileOutputStream output(downloadFile);
            if (!output.openedOk()) {
                juce::MessageManager::callAsync([alive, progressDlg]() {
                    if (!alive->load()) return;
                    if (*progressDlg)
                        (*progressDlg)->exitModalState(0);
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Update Error",
                        "Could not write update file.\nCheck write permissions.",
                        "OK");
                });
                return;
            }

            char buffer[8192];
            while (!stream->isExhausted()) {
                auto bytesRead = stream->read(buffer, sizeof(buffer));
                if (bytesRead <= 0) break;
                output.write(buffer, static_cast<size_t>(bytesRead));
            }
            output.flush();
        }

        // Verify downloaded file — size check
        if (!downloadFile.existsAsFile() || downloadFile.getSize() < 100 * 1024) {
            downloadFile.deleteFile();
            juce::MessageManager::callAsync([alive, progressDlg]() {
                if (!alive->load()) return;
                if (*progressDlg)
                    (*progressDlg)->exitModalState(0);
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Download Failed",
                    "Downloaded file appears invalid.\nPlease download manually.",
                    "OK");
            });
            return;
        }

        // Verify downloaded file — SHA-256 integrity check
        {
            const bool checksumRequired = update_detail::releaseRequiresChecksum(version);
            const auto remoteAssetName = juce::URL::removeEscapeChars(
                downloadUrl.fromLastOccurrenceOf("/", false, false));
            const auto checksumUrl =
                downloadUrl.upToLastOccurrenceOf("/", true, false) + "checksums.sha256";
            int checksumStatus = 0;
            auto checksumStream = juce::URL(checksumUrl).createInputStream(
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs(5000)
                    .withStatusCode(&checksumStatus));

            juce::String expectedHash;
            bool checksumEntryValid = false;
            if (checksumStream && checksumStatus == 200) {
                checksumEntryValid = update_detail::parseExpectedSha256(
                    checksumStream->readEntireStreamAsString(), remoteAssetName, expectedHash);
            }

            auto failIntegrityCheck =
                [downloadFile, alive, progressDlg](juce::String reason) mutable {
                downloadFile.deleteFile();
                juce::Logger::writeToLog("[APP] Update integrity verification failed: " + reason);
                juce::MessageManager::callAsync([alive, progressDlg, reason]() mutable {
                    if (!alive->load()) return;
                    if (*progressDlg)
                        (*progressDlg)->exitModalState(0);
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Integrity Check Failed",
                        reason + "\nDirectPipe was not changed. Please download manually from GitHub.",
                        "OK");
                });
                };

            if (!checksumEntryValid) {
                if (checksumRequired) {
                    const auto reason = checksumStatus == 200
                        ? "The release checksum does not contain a valid exact entry for "
                              + remoteAssetName + "."
                        : "The release checksum could not be downloaded (HTTP "
                              + juce::String(checksumStatus) + ").";
                    failIntegrityCheck(reason);
                    return;
                }
                juce::Logger::writeToLog(
                    "[APP] Legacy release has no usable checksum; continuing for compatibility");
            } else {
                juce::FileInputStream input(downloadFile);
                if (!input.openedOk()) {
                    if (checksumRequired) {
                        failIntegrityCheck("The downloaded update could not be read for verification.");
                        return;
                    }
                } else {
                    const auto actualHash = juce::SHA256(input).toHexString().toLowerCase();
                    if (actualHash != expectedHash) {
                        failIntegrityCheck("The downloaded update hash does not match the release checksum.");
                        return;
                    }
                    juce::Logger::writeToLog("[APP] SHA-256 verified: " + actualHash);
                }
            }
        }

        // Create update batch script
        auto currentPath = currentExe.getFullPathName().replace("/", "\\");
        auto downloadPath = downloadFile.getFullPathName().replace("/", "\\");
        auto backupPath = currentExe.getSiblingFile(kBackupExe)
                              .getFullPathName().replace("/", "\\");
        auto updateDirPath = updateDir.getFullPathName().replace("/", "\\");

        juce::String script;
        script << "@echo off\r\n";
        script << "chcp 65001 > nul\r\n";
        script << "echo.\r\n";
        script << "echo  Updating DirectPipe to v" << version << " ...\r\n";
        script << update_detail::buildWindowsUpdateWaitScript(currentProcessId);

        auto flagPath = currentExe.getSiblingFile(kUpdatedFlag)
                            .getFullPathName().replace("/", "\\");

        auto stagedPath = currentExe.getSiblingFile(kUpdateExe)
                              .getFullPathName().replace("/", "\\");
        script << update_detail::buildWindowsUpdateInstallScript({
            currentPath,
            downloadPath,
            stagedPath,
            backupPath,
            updateDirPath,
            isZip,
        });

        script << update_detail::buildWindowsUpdateCompletionScript(
            version, flagPath, currentPath);
        script << "exit\r\n";

        if (!batchFile.replaceWithText(script)) {
            juce::Logger::writeToLog("[APP] Failed to create update installer script: "
                + batchFile.getFullPathName());
            juce::MessageManager::callAsync([alive, progressDlg]() {
                if (!alive->load()) return;
                if (*progressDlg)
                    (*progressDlg)->exitModalState(0);
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Update Failed",
                    "Could not create the update installer. DirectPipe was not changed.",
                    "OK");
            });
            return;
        }

        juce::MessageManager::callAsync([alive, batchFile, progressDlg]() {
            if (!alive->load()) return;
            if (*progressDlg)
                (*progressDlg)->exitModalState(0);
            if (!batchFile.startAsProcess()) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Update Failed",
                    "Could not launch the update installer. DirectPipe was not changed.",
                    "OK");
                return;
            }
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        });
        });

    if (!workerStarted) {
        if (*progressDlg)
            (*progressDlg)->exitModalState(0);
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Update Error",
            "Could not start the update worker. Please try again.",
            "OK");
    }
}
#endif // JUCE_WINDOWS

} // namespace directpipe
