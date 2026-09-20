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
#include <shellapi.h>
namespace {
    constexpr const char* kUpdateBatchFile = "_update.bat";
    constexpr const char* kUpdateDir       = "_update";
    constexpr const char* kUpdateZip       = "DirectPipe_update.zip";
    constexpr const char* kUpdateExe       = "DirectPipe_update.exe";
    constexpr const char* kBackupExe       = "DirectPipe_backup.exe";
    constexpr const char* kUpdatedFlag     = "_updated.flag";

    class ReceiverPlanDialog final : public juce::AlertWindow {
    public:
        explicit ReceiverPlanDialog(const juce::String& details)
            : AlertWindow("Receiver Update", "Review the exact installation paths below.",
                          juce::MessageBoxIconType::InfoIcon)
        {
            details_.setMultiLine(true);
            details_.setReadOnly(true);
            details_.setScrollbarsShown(true);
            details_.setText(details, false);
            details_.setSize(620, 280);
            details_.setCaretPosition(0);
            addCustomComponent(&details_);
        }
    private:
        juce::TextEditor details_;
    };
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

#if JUCE_WINDOWS
void update_detail::cleanupTransientUpdateFiles(const juce::File& executableDirectory)
{
    executableDirectory.getChildFile(kUpdateBatchFile).deleteFile();
    executableDirectory.getChildFile(kUpdateDir).deleteRecursively();
    executableDirectory.getChildFile(kUpdateZip).deleteFile();
    executableDirectory.getChildFile(kUpdateExe).deleteFile();
}
#endif

UpdateChecker::UpdateChecker() = default;

UpdateChecker::~UpdateChecker()
{
    stopTimer();
    alive_->store(false);
#if JUCE_WINDOWS
    if (receiverInspectionThread_.joinable()) receiverInspectionThread_.join();
    if (maintenanceDialog_) maintenanceDialog_->exitModalState(0);
    if (companionProcess_) ::CloseHandle(static_cast<HANDLE>(companionProcess_));
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
    update_detail::cleanupTransientUpdateFiles(exeDir);

    auto flagFile = exeDir.getChildFile(kUpdatedFlag);
    if (flagFile.existsAsFile()) {
        auto version = flagFile.loadFileAsString().trim();
        flagFile.deleteFile();
        if (onPostUpdateNotification)
            onPostUpdateNotification(version);
    }
#endif
}

// ─── Check admission on caller; network worker posts message-thread results ──

void UpdateChecker::checkForUpdate()
{
    checkForUpdateImpl(false);
}

void UpdateChecker::checkForUpdateImpl(bool receiverOnly)
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

    if (!startUpdateCheckWorker([this, currentVersion, requestGeneration, receiverOnly] {
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

        // Startup compares the host only. Explicit maintenance still needs the
        // published package so each discovered Receiver can be compared separately.
        if (!receiverOnly && releaseVersion <= installedVersion) {
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

#if JUCE_WINDOWS
        if (receiverOnly) {
            juce::MessageManager::callAsync([this, alive = alive_, requestGeneration,
                                           canonicalReleaseVersion, downloadUrl] {
                if (!alive->load() || requestGeneration != updateCheckRequestGeneration_.load())
                    return;
                maintenanceCheckPending_ = false;
                stopTimer();
                if (maintenanceDialog_) maintenanceDialog_->exitModalState(0);
                maintenanceDialog_.reset();
                latestVersion_ = canonicalReleaseVersion;
                latestDownloadUrl_ = downloadUrl;
                checkStatus_.store(UpdateCheckStatus::UpToDate, std::memory_order_release);
                prepareReceiverUpdate(true);
            });
            return;
        }
#endif
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

void UpdateChecker::checkForReceiverUpdate()
{
#if JUCE_WINDOWS
    if (receiverFlowActive_ || companionProcess_ || downloadInProgress_.load()
        || updateCheckInProgress_.load()) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            "Update in Progress", "Please finish the current update check or installation first.");
        return;
    }
    receiverFlowActive_ = true;
    maintenanceCheckPending_ = true;
    maintenanceDialog_ = std::make_unique<juce::AlertWindow>(
        "Receiver Update", "Checking the latest published release...",
        juce::MessageBoxIconType::NoIcon);
    maintenanceDialog_->enterModalState(true, nullptr, false);
    checkForUpdateImpl(true);
    startTimer(250);
#endif
}

void UpdateChecker::timerCallback()
{
#if JUCE_WINDOWS
    if (maintenanceCheckPending_) {
        const auto status = getCheckStatus();
        if (status == UpdateCheckStatus::Checking) return;
        maintenanceCheckPending_ = false;
        receiverFlowActive_ = false;
        stopTimer();
        if (maintenanceDialog_) maintenanceDialog_->exitModalState(0);
        maintenanceDialog_.reset();
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "Receiver Update", getLastCheckError() + "\nPlease try again later.");
        return;
    }
    if (companionProcess_) {
        DWORD exitCode = STILL_ACTIVE;
        if (::GetExitCodeProcess(static_cast<HANDLE>(companionProcess_), &exitCode)
            && exitCode == STILL_ACTIVE) return;
        ::CloseHandle(static_cast<HANDLE>(companionProcess_));
        companionProcess_ = nullptr;
        receiverFlowActive_ = false;
        stopTimer();
        if (maintenanceDialog_) maintenanceDialog_->exitModalState(0);
        maintenanceDialog_.reset();
        const auto result = juce::JSON::parse(companionResultFile_);
        const bool success = result.getProperty("success", false);
        juce::String message;
        if (success) {
            message = "Receiver update complete. You can reopen OBS or your audio application.\n"
                      "DirectPipe and your saved settings were kept running and unchanged.";
        } else {
            message = result.getProperty("error", "The installer did not report a result.").toString();
            if (result.getProperty("rollbackSucceeded", false))
                message += "\nPrevious files were preserved or restored.";
            message += "\nDetails: " + companionResultFile_.getFullPathName()
                     + "\nClose applications using the Receiver, then check again.";
        }
        juce::Logger::writeToLog("[APP] Receiver update: " + message);
        juce::AlertWindow::showMessageBoxAsync(success ? juce::MessageBoxIconType::InfoIcon
                                                     : juce::MessageBoxIconType::WarningIcon,
                                              "Receiver Update", message);
    }
#endif
}

#if JUCE_WINDOWS
void UpdateChecker::runReceiverInspection(std::function<void()> task)
{
    // Message-thread entry only. A preceding inspection posts its result only
    // after all I/O has finished, so reaping it here never waits on that I/O.
    if (receiverInspectionThread_.joinable()) receiverInspectionThread_.join();
    const auto failed = [this, alive = alive_] {
        juce::MessageManager::callAsync([this, alive] {
            if (!alive->load()) return;
            if (maintenanceDialog_) maintenanceDialog_->exitModalState(0);
            maintenanceDialog_.reset();
            receiverFlowActive_ = false;
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "Receiver Update", "The installation check could not finish. No files were replaced. Please retry.");
        });
    };
    try {
        receiverInspectionThread_ = std::thread([task = std::move(task), failed] {
            try { task(); } catch (...) { failed(); }
        });
    } catch (...) { failed(); }
}

void UpdateChecker::prepareReceiverUpdate(bool receiverOnly, const juce::File& customFolder)
{
    // Discovery reads metadata without loading plug-ins. A custom directory is
    // added to the standard search roots; it does not authorize a new install.
    if (companionProcess_ || downloadInProgress_.load()) return;
    receiverFlowActive_ = true;
    maintenanceDialog_ = std::make_unique<juce::AlertWindow>("Receiver Update",
        "Finding installed Receivers...", juce::MessageBoxIconType::NoIcon);
    maintenanceDialog_->enterModalState(true, nullptr, false);
    runReceiverInspection([this, alive = alive_, receiverOnly, customFolder] {
        auto found = update_detail::discoverInstalledReceivers(customFolder);
        juce::MessageManager::callAsync([this, alive, receiverOnly, found = std::move(found)]() mutable {
            if (!alive->load()) return;
            if (maintenanceDialog_) maintenanceDialog_->exitModalState(0);
            maintenanceDialog_.reset();
            showReceiverUpdatePlan(receiverOnly, std::move(found));
        });
    });
}

void UpdateChecker::showReceiverUpdatePlan(bool receiverOnly, update_detail::ReceiverDiscoveryResult found)
{
    std::vector<update_detail::ReceiverInstallTarget> targets;
    juce::String details = receiverOnly
        ? "DirectPipe will keep running. Installed Receivers older than v" + latestVersion_
              + " can be updated from that published release.\n\n"
        : "DirectPipe will close and restart after the update to v" + latestVersion_ + ".\n\n";
    for (const auto& target : found.targets) {
        const bool update = update_detail::receiverNeedsUpdate(target.installedVersion, latestVersion_);
        details += target.installPath + "\n  v" + target.installedVersion;
        details += update ? " -> v" + latestVersion_ : " (kept: same or newer version)";
        details += "\n\n";
        if (update) targets.push_back(target);
    }
    if (found.targets.empty())
        details += "No verified installed Receiver was found in the standard VST folders.\n"
                   "Use Choose Folder for a custom installation.\n\n";
    if (!found.warnings.isEmpty())
        details += "Some locations could not be verified:\n" + found.warnings.joinIntoString("\n") + "\n\n";
    if (!targets.empty())
        details += "Close OBS or other applications using these Receivers before continuing.\n"
                   "Windows may request administrator permission for protected folders.";
    auto* window = new ReceiverPlanDialog(details);
    if (!receiverOnly || !targets.empty()) window->addButton("Update", 1);
    window->addButton("Choose Folder...", 2);
    window->addButton("Later", 0);
    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, alive = alive_, receiverOnly, targets](int choice) {
            if (!alive->load()) return;
            if (choice == 1) {
                checkReceiverUseAndUpdate(receiverOnly, targets);
            } else if (choice == 2) {
                auto chooser = std::make_shared<juce::FileChooser>("Select your Receiver installation folder");
                chooser->launchAsync(juce::FileBrowserComponent::openMode
                                     | juce::FileBrowserComponent::canSelectDirectories,
                    [this, alive, receiverOnly, chooser](const juce::FileChooser& selected) {
                        if (!alive->load()) return;
                        if (selected.getResult().isDirectory())
                            prepareReceiverUpdate(receiverOnly, selected.getResult());
                        else receiverFlowActive_ = false;
                    });
            } else receiverFlowActive_ = false;
        }), true);
}

void UpdateChecker::checkReceiverUseAndUpdate(
    bool receiverOnly, std::vector<update_detail::ReceiverInstallTarget> targets)
{
    checkReceiverUse(targets, [this, alive = alive_, receiverOnly, targets] {
        if (!alive->load()) return;
        receiverFlowActive_ = false;
        performUpdate(receiverOnly, targets);
    });
}

void UpdateChecker::checkReceiverUse(std::vector<update_detail::ReceiverInstallTarget> targets,
                                    std::function<void()> whenReady)
{
    // Read-only preflight: Retry repeats inspection after the user closes OBS or
    // another owner manually; Later cancels this flow. The installer rechecks use
    // and target identity because this UI check is not a lasting file lock.
    receiverFlowActive_ = true;
    maintenanceDialog_ = std::make_unique<juce::AlertWindow>("Receiver Update",
        "Checking whether the selected Receivers are in use...", juce::MessageBoxIconType::NoIcon);
    maintenanceDialog_->enterModalState(true, nullptr, false);
    runReceiverInspection([this, alive = alive_, targets, whenReady] {
      const auto usage = update_detail::queryReceiverUse(targets);
      juce::MessageManager::callAsync([this, alive, targets, whenReady, usage] {
        if (!alive->load()) return;
        if (maintenanceDialog_) maintenanceDialog_->exitModalState(0);
        maintenanceDialog_.reset();
        if (!targets.empty() && (!usage.inspectable || usage.busy)) {
        auto* window = new juce::AlertWindow("Receiver In Use",
            usage.busy
                ? "Close the applications using the selected Receiver, then retry.\n\n"
                    + usage.processes.joinIntoString("\n")
                : "Receiver access could not be verified. No files have been replaced.\n\n"
                    + usage.error,
            juce::MessageBoxIconType::WarningIcon);
        window->addButton("Retry", 1);
        window->addButton("Later", 0);
        window->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, alive = alive_, targets, whenReady](int result) {
                if (!alive->load()) return;
                if (result == 1) checkReceiverUse(targets, whenReady);
                else receiverFlowActive_ = false;
            }), true);
        return;
        }
        receiverFlowActive_ = false;
        whenReady();
      });
    });
}
#endif

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
                prepareReceiverUpdate(false);
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
void UpdateChecker::performUpdate(bool receiverOnly,
                                  std::vector<update_detail::ReceiverInstallTarget> targets)
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

    // Selected Receiver targets use an isolated companion transaction. A host-only
    // update with no eligible Receiver keeps the existing batch-script path.
    auto currentExe = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile);
    const bool companionUpdate = receiverOnly || !targets.empty();
    auto companionDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("DirectPipe-update-" + juce::Uuid().toString());
    auto updateDir = companionUpdate ? companionDir.getChildFile("expanded")
                                    : currentExe.getParentDirectory().getChildFile(kUpdateDir);
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
        [this, alive = alive_, downloadUrl, updateDir, batchFile, currentExe,
         version, isZip, currentProcessId, progressDlg, receiverOnly, targets,
         companionUpdate, companionDir]() {
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
        if (companionUpdate) companionDir.createDirectory();
        auto downloadFile = companionUpdate ? companionDir.getChildFile(kUpdateZip)
            : (isZip ? currentExe.getSiblingFile(kUpdateZip) : currentExe.getSiblingFile(kUpdateExe));

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

            output.setPosition(0);
            output.truncate();
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

        juce::String expectedHash;
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

        // The companion independently revalidates the ZIP hash, source identities,
        // selected destination versions and file use immediately before replacement.
        if (companionUpdate) {
            update_detail::WindowsUpdateInstallSpec spec;
            spec.currentExePath = currentExe.getFullPathName();
            spec.downloadedFilePath = downloadFile.getFullPathName();
            spec.stagedExePath = companionDir.getChildFile(kUpdateExe).getFullPathName();
            spec.backupExePath = currentExe.getSiblingFile(kBackupExe).getFullPathName();
            spec.updateDirPath = updateDir.getFullPathName();
            spec.expectedVersion = version;
            spec.isZip = isZip;
            spec.receiverTargets = targets;
            spec.skipHostUpdate = receiverOnly;
            spec.processId = currentProcessId;
            spec.resultFilePath = companionDir.getChildFile("result.json").getFullPathName();
            spec.updatedFlagPath = currentExe.getSiblingFile(kUpdatedFlag).getFullPathName();
            spec.relaunchHostAfterUpdate = !receiverOnly;
            spec.expectedPackageSha256 = expectedHash;
            // Use the same path rules as installation while this host is still
            // running. This child only extracts into the private download folder;
            // no elevation, installation writes, host wait, or relaunch occurs.
            auto preflightSpec = spec;
            preflightSpec.resultFilePath = companionDir.getChildFile("preflight-result.json").getFullPathName();
            const auto preflightFile = companionDir.getChildFile("preflight.ps1");
            const auto preflightScript = update_detail::buildWindowsCompanionInstallPowerShell(preflightSpec, true);
            juce::String preparationError;
            juce::ChildProcess preflight;
            const auto powershell = juce::File::getSpecialLocation(juce::File::windowsSystemDirectory)
                .getChildFile("WindowsPowerShell/v1.0/powershell.exe").getFullPathName();
            if (preflightScript.isEmpty() || !preflightFile.replaceWithText(preflightScript, false, true)
                || !preflight.start(juce::StringArray{powershell, "-NoProfile", "-NonInteractive",
                    "-ExecutionPolicy", "Bypass", "-File", preflightFile.getFullPathName()})) {
                preparationError = "Could not start the update path check.";
            } else {
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 90000.0;
                while (preflight.isRunning() && alive->load()
                       && juce::Time::getMillisecondCounterHiRes() < deadline)
                    preflight.waitForProcessToFinish(100);
                if (preflight.isRunning()) {
                    preflight.kill();
                    preflight.waitForProcessToFinish(5000);
                    preparationError = "The update path check did not finish. Please retry.";
                } else {
                    const auto checked = juce::JSON::parse(juce::File(preflightSpec.resultFilePath).loadFileAsString());
                    if (preflight.getExitCode() != 0 || !static_cast<bool>(checked["success"])) {
                        preparationError = checked["error"].toString();
                        if (preparationError.isEmpty()) preparationError = "The update paths could not be verified.";
                    }
                }
            }
            if (!alive->load()) return;
            const auto script = update_detail::buildWindowsCompanionInstallPowerShell(spec);
            const auto scriptFile = companionDir.getChildFile("install.ps1");
            const bool prepared = preparationError.isEmpty() && script.isNotEmpty()
                && scriptFile.replaceWithText(script, false, true);
            juce::MessageManager::callAsync([this, alive, progressDlg, prepared, scriptFile,
                                            receiverOnly, targets, spec, preparationError] {
                if (!alive->load()) return;
                if (*progressDlg) (*progressDlg)->exitModalState(0);
                if (!prepared) {
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                        "Update Failed", (preparationError.isEmpty()
                            ? "Could not prepare a verified Receiver update." : preparationError)
                            + "\nDirectPipe is still running; no installed files were replaced.");
                    return;
                }
                // A retry here keeps the already verified download; no second download is needed.
                checkReceiverUse(targets, [this, alive, scriptFile, receiverOnly, targets, spec] {
                if (!alive->load()) return;
                bool elevate = std::any_of(targets.begin(), targets.end(),
                    [](const auto& target) { return target.needsElevation; });
                // Receiver ACL/root checks are not enough for paired updates:
                // the host executable's destination may also need elevation.
                if (!receiverOnly && !elevate) {
                    const auto probe = juce::File(spec.currentExePath).getParentDirectory()
                        .getChildFile(".DirectPipe-write-probe-" + juce::Uuid().toString()).getFullPathName();
                    HANDLE access = ::CreateFileW(probe.toWideCharPointer(), GENERIC_WRITE | DELETE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_NEW,
                        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
                    elevate = access == INVALID_HANDLE_VALUE;
                    if (access != INVALID_HANDLE_VALUE) ::CloseHandle(access);
                }
                const auto powershell = juce::File::getSpecialLocation(juce::File::windowsSystemDirectory)
                    .getChildFile("WindowsPowerShell/v1.0/powershell.exe").getFullPathName();
                const auto arguments = "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File \""
                                       + scriptFile.getFullPathName() + "\"";
                SHELLEXECUTEINFOW launch{};
                launch.cbSize = sizeof(launch);
                launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
                launch.lpVerb = elevate ? L"runas" : L"open";
                launch.lpFile = powershell.toWideCharPointer();
                launch.lpParameters = arguments.toWideCharPointer();
                launch.nShow = SW_HIDE;
                if (!::ShellExecuteExW(&launch) || !launch.hProcess) {
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                        "Update Not Started", "The installer could not start or administrator permission was cancelled."
                                              "\nDirectPipe is still running; no files were replaced.");
                    return;
                }
                // Quit only after a paired installer has started. Receiver-only
                // work keeps the audio host alive and polls the child result file.
                if (receiverOnly) {
                    companionProcess_ = launch.hProcess;
                    companionResultFile_ = juce::File(spec.resultFilePath);
                    receiverFlowActive_ = true;
                    maintenanceDialog_ = std::make_unique<juce::AlertWindow>("Receiver Update",
                        "Updating the selected Receivers. DirectPipe will keep running...",
                        juce::MessageBoxIconType::NoIcon);
                    maintenanceDialog_->enterModalState(true, nullptr, false);
                    startTimer(250);
                } else {
                    ::CloseHandle(launch.hProcess);
                    juce::JUCEApplication::getInstance()->systemRequestedQuit();
                }
                });
            });
            return;
        }

        // Legacy host-only batch installer (no selected Receiver destinations).
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
        const auto installScript = update_detail::buildWindowsUpdateInstallScript({
            currentPath,
            downloadPath,
            stagedPath,
            backupPath,
            updateDirPath,
            version,
            isZip,
        });
        if (installScript.isEmpty()) {
            juce::Logger::writeToLog(
                "[APP] Refusing to build updater script for an invalid expected version");
            juce::MessageManager::callAsync([alive, progressDlg]() {
                if (!alive->load()) return;
                if (*progressDlg)
                    (*progressDlg)->exitModalState(0);
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Update Failed",
                    "The update executable identity could not be validated. "
                    "DirectPipe was not changed.",
                    "OK");
            });
            return;
        }
        script << installScript;

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
