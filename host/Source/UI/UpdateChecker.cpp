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
    if (updateCheckThread_.joinable())
        updateCheckThread_.join();
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
    auto currentVersion = juce::String(ProjectInfo::versionString);

    updateCheckThread_ = std::thread([this, alive = alive_, currentVersion]() {
        juce::URL url("https://api.github.com/repos/LiveTrack-X/DirectPipe/releases/latest");
        auto stream = url.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(10000));
        juce::String response;
        if (stream) response = stream->readEntireStreamAsString();
        if (response.isEmpty()) return;

        auto parsed = juce::JSON::parse(response);
        if (auto* obj = parsed.getDynamicObject()) {
            auto tagName = obj->getProperty("tag_name").toString();
            std::array<int, 3> releaseVersion{};
            std::array<int, 3> installedVersion{};
            juce::String canonicalReleaseVersion;
            juce::String canonicalInstalledVersion;
            if (!update_detail::parseStrictReleaseVersion(
                    tagName, releaseVersion, canonicalReleaseVersion)) {
                juce::Logger::writeToLog("[APP] Ignoring malformed release tag: " + tagName);
                return;
            }
            if (!update_detail::parseStrictReleaseVersion(
                    currentVersion, installedVersion, canonicalInstalledVersion)) {
                juce::Logger::writeToLog("[APP] Installed version is not strict semver: " + currentVersion);
                return;
            }

            if (canonicalReleaseVersion != canonicalInstalledVersion) {
                if (releaseVersion > installedVersion) {
                    // Find download URL from assets — platform-aware selection
                    // 1st pass: match platform-tagged asset (e.g. "DirectPipe-...-Windows.zip")
                    // 2nd pass: fallback to any "DirectPipe*.zip/exe" (legacy releases without platform tag)
#if JUCE_WINDOWS
                    constexpr const char* platformTag = "Windows";
                    constexpr const char* pkgExt      = ".zip";
#elif JUCE_MAC
                    constexpr const char* platformTag = "macOS";
                    constexpr const char* pkgExt      = ".dmg";
#elif JUCE_LINUX
                    constexpr const char* platformTag = "Linux";
                    constexpr const char* pkgExt      = ".tar.gz";
#endif
                    juce::String downloadUrl;
                    if (auto* assets = obj->getProperty("assets").getArray()) {
                        juce::String fallbackZipUrl;  // legacy: first DirectPipe*.zip
                        juce::String fallbackExeUrl;  // legacy: first DirectPipe*.exe
                        for (auto& asset : *assets) {
                            if (auto* assetObj = asset.getDynamicObject()) {
                                auto name = assetObj->getProperty("name").toString();
                                auto assetUrl = assetObj->getProperty("browser_download_url").toString();
                                // Platform-specific match (highest priority)
                                if (name.containsIgnoreCase(platformTag) &&
                                    name.containsIgnoreCase("DirectPipe") &&
                                    name.endsWithIgnoreCase(pkgExt)) {
                                    downloadUrl = assetUrl;
                                    break;
                                }
                                // Fallback: legacy ZIP without platform tag
                                if (fallbackZipUrl.isEmpty() &&
                                    name.endsWithIgnoreCase(".zip") &&
                                    name.containsIgnoreCase("DirectPipe")) {
                                    fallbackZipUrl = assetUrl;
                                }
                                // Fallback: standalone exe
                                if (fallbackExeUrl.isEmpty() &&
                                    name.endsWithIgnoreCase(".exe") &&
                                    name.containsIgnoreCase("DirectPipe")) {
                                    fallbackExeUrl = assetUrl;
                                }
                            }
                        }
                        if (downloadUrl.isEmpty()) downloadUrl = fallbackZipUrl;
                        if (downloadUrl.isEmpty()) downloadUrl = fallbackExeUrl;
                    }

                    juce::MessageManager::callAsync([this, alive, canonicalReleaseVersion, downloadUrl]() {
                        if (!alive->load()) return;
                        latestVersion_ = canonicalReleaseVersion;
                        latestDownloadUrl_ = downloadUrl;
                        updateAvailable_ = true;

                        if (onUpdateAvailable)
                            onUpdateAvailable(canonicalReleaseVersion, downloadUrl);
                    });
                }
            }
        }
    });
}

// ─── Update Dialog ────────────────────────────────────────────────────────

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
        // Try to fetch checksums.sha256 from the same release.
        // Format: "<sha256hex>  <filename>\n" (shasum -a 256 compatible)
        // If checksum file is unavailable, skip verification (older releases won't have it).
        {
            auto checksumUrl = downloadUrl.upToLastOccurrenceOf("/", true, false) + "checksums.sha256";
            int csStatus = 0;
            auto csStream = juce::URL(checksumUrl).createInputStream(
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs(5000)
                    .withStatusCode(&csStatus));

            if (csStream && csStatus == 200) {
                auto checksumContent = csStream->readEntireStreamAsString();
                auto downloadName = downloadFile.getFileName();

                // Parse checksum file: find line containing our filename
                juce::String expectedHash;
                for (auto line : juce::StringArray::fromLines(checksumContent)) {
                    line = line.trim();
                    if (line.containsIgnoreCase(downloadName) ||
                        line.containsIgnoreCase(downloadUrl.fromLastOccurrenceOf("/", false, false))) {
                        expectedHash = line.upToFirstOccurrenceOf(" ", false, false).trim().toLowerCase();
                        break;
                    }
                }

                if (expectedHash.isNotEmpty() && expectedHash.length() == 64) {
                    // Compute SHA-256 of downloaded file
                    juce::FileInputStream fis(downloadFile);
                    if (fis.openedOk()) {
                        juce::SHA256 sha256(fis);
                        auto actualHash = sha256.toHexString().toLowerCase();

                        if (actualHash != expectedHash) {
                            downloadFile.deleteFile();
                            juce::Logger::writeToLog("[APP] SHA-256 mismatch: expected "
                                + expectedHash + ", got " + actualHash);
                            juce::MessageManager::callAsync([alive, progressDlg]() {
                                if (!alive->load()) return;
                                if (*progressDlg)
                                    (*progressDlg)->exitModalState(0);
                                juce::AlertWindow::showMessageBoxAsync(
                                    juce::MessageBoxIconType::WarningIcon,
                                    "Integrity Check Failed",
                                    "Downloaded file hash does not match expected value.\n"
                                    "The file may be corrupted or tampered.\n"
                                    "Please download manually from GitHub.",
                                    "OK");
                            });
                            return;
                        }
                        juce::Logger::writeToLog("[APP] SHA-256 verified: " + actualHash);
                    }
                }
                // If no matching hash line found, skip verification (release may not have checksum for this asset)
            }
            // If checksums.sha256 not found (404), skip verification — older releases don't have it
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
