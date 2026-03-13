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

#if JUCE_WINDOWS
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
    if (downloadThread_.joinable())
        downloadThread_.join();
#endif
    if (updateCheckThread_.joinable())
        updateCheckThread_.join();
}

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
            if (tagName.startsWithChar('v') || tagName.startsWithChar('V'))
                tagName = tagName.substring(1);

            if (tagName.isNotEmpty() && tagName != currentVersion) {
                auto parseVer = [](const juce::String& v) -> std::tuple<int,int,int> {
                    auto parts = juce::StringArray::fromTokens(v, ".", "");
                    return { parts.size() > 0 ? parts[0].getIntValue() : 0,
                             parts.size() > 1 ? parts[1].getIntValue() : 0,
                             parts.size() > 2 ? parts[2].getIntValue() : 0 };
                };
                auto [rMaj, rMin, rPat] = parseVer(tagName);
                auto [cMaj, cMin, cPat] = parseVer(currentVersion);

                if (std::tie(rMaj, rMin, rPat) > std::tie(cMaj, cMin, cPat)) {
                    // Find download URL from assets (prefer ZIP, fallback to exe)
                    juce::String downloadUrl;
                    if (auto* assets = obj->getProperty("assets").getArray()) {
                        juce::String exeUrl;
                        for (auto& asset : *assets) {
                            if (auto* assetObj = asset.getDynamicObject()) {
                                auto name = assetObj->getProperty("name").toString();
                                auto assetUrl = assetObj->getProperty("browser_download_url").toString();
                                if (name.endsWithIgnoreCase(".zip") &&
                                    name.containsIgnoreCase("DirectPipe")) {
                                    downloadUrl = assetUrl;
                                    break;
                                }
                                if (name.endsWithIgnoreCase(".exe") &&
                                    name.containsIgnoreCase("DirectPipe")) {
                                    exeUrl = assetUrl;
                                }
                            }
                        }
                        if (downloadUrl.isEmpty())
                            downloadUrl = exeUrl;
                    }

                    juce::MessageManager::callAsync([this, alive, tagName, downloadUrl]() {
                        if (!alive->load()) return;
                        latestVersion_ = tagName;
                        latestDownloadUrl_ = downloadUrl;
                        updateAvailable_ = true;

                        if (onUpdateAvailable)
                            onUpdateAvailable(tagName, downloadUrl);
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
    bool isZip = downloadUrl.endsWithIgnoreCase(".zip");

    // If a previous download is still running, reject the request (don't block message thread)
    if (downloadThread_.joinable()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Update in Progress",
            "A download is already in progress. Please wait for it to finish.",
            "OK");
        (*progressDlg)->exitModalState(0);
        return;
    }
    downloadThread_ = std::thread([alive = alive_, downloadUrl, updateDir, batchFile, currentExe, version, isZip, progressDlg]() {
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

        // Verify downloaded file
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

        // Create update batch script
        auto exeDir = currentExe.getParentDirectory().getFullPathName().replace("/", "\\");
        auto currentPath = currentExe.getFullPathName().replace("/", "\\");
        auto downloadPath = downloadFile.getFullPathName().replace("/", "\\");
        auto backupPath = currentExe.getSiblingFile(kBackupExe)
                              .getFullPathName().replace("/", "\\");
        auto batchPath = batchFile.getFullPathName().replace("/", "\\");
        auto updateDirPath = updateDir.getFullPathName().replace("/", "\\");

        juce::String script;
        script << "@echo off\r\n";
        script << "chcp 65001 > nul\r\n";
        script << "echo.\r\n";
        script << "echo  Updating DirectPipe to v" << version << " ...\r\n";
        script << "echo  Waiting for DirectPipe to close...\r\n";
        script << "timeout /t 3 /nobreak > nul\r\n";
        script << ":waitloop\r\n";
        script << "tasklist /FI \"IMAGENAME eq DirectPipe.exe\" 2>nul | find /I \"DirectPipe.exe\" > nul\r\n";
        script << "if not errorlevel 1 (\r\n";
        script << "    timeout /t 1 /nobreak > nul\r\n";
        script << "    goto waitloop\r\n";
        script << ")\r\n";

        auto flagPath = currentExe.getSiblingFile(kUpdatedFlag)
                            .getFullPathName().replace("/", "\\");

        if (isZip) {
            script << "echo  Extracting update...\r\n";
            script << "if exist \"" << updateDirPath << "\" rd /s /q \"" << updateDirPath << "\"\r\n";
            script << "powershell -NoProfile -Command \"Expand-Archive -Path '" << downloadPath
                   << "' -DestinationPath '" << updateDirPath << "' -Force\"\r\n";
            script << "if exist \"" << backupPath << "\" del /f \"" << backupPath << "\"\r\n";
            script << "move /y \"" << currentPath << "\" \"" << backupPath << "\"\r\n";
            script << "powershell -NoProfile -Command \"$f = Get-ChildItem -Path '" << updateDirPath
                   << "' -Recurse -Filter 'DirectPipe.exe' | Select-Object -First 1; "
                   << "if ($f) { Copy-Item $f.FullName -Destination '" << currentPath << "' -Force }\"\r\n";
            script << "rd /s /q \"" << updateDirPath << "\"\r\n";
            script << "del /f \"" << downloadPath << "\"\r\n";
            script << "del /f \"" << backupPath << "\"\r\n";
        } else {
            script << "echo  Applying update...\r\n";
            script << "if exist \"" << backupPath << "\" del /f \"" << backupPath << "\"\r\n";
            script << "move /y \"" << currentPath << "\" \"" << backupPath << "\"\r\n";
            script << "move /y \"" << downloadPath << "\" \"" << currentPath << "\"\r\n";
            script << "del /f \"" << backupPath << "\"\r\n";
        }

        script << "echo " << version << " > \"" << flagPath << "\"\r\n";
        script << "powershell -NoProfile -Command \"Start-Process -FilePath '" << currentPath << "'\"\r\n";
        script << "exit\r\n";

        batchFile.replaceWithText(script);

        juce::MessageManager::callAsync([alive, batchFile, progressDlg]() {
            if (!alive->load()) return;
            if (*progressDlg)
                (*progressDlg)->exitModalState(0);
            batchFile.startAsProcess();
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        });
    });
}
#endif // JUCE_WINDOWS

} // namespace directpipe
