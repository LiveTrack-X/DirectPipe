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
 * @file UpdateChecker.h
 * @brief Background update checker — polls GitHub releases, shows update dialog,
 *        and performs in-app auto-update (Windows only).
 */
#pragma once

#include <JuceHeader.h>
#include <thread>
#include <functional>

namespace directpipe {

class UpdateChecker {
public:
    UpdateChecker();
    ~UpdateChecker();

    /** Check GitHub for a newer release (runs on background thread). */
    void checkForUpdate();

    /** Clean up leftover update files from a previous update and detect post-update flag.
     *  Call once during startup. If a flag is found, onPostUpdateNotification is called. */
    void cleanupPreviousUpdate();

    /** Show the update dialog (Update Now / View on GitHub / Later). */
    void showUpdateDialog();

    bool isUpdateAvailable() const { return updateAvailable_; }
    juce::String getLatestVersion() const { return latestVersion_; }

    /** Called on message thread when a newer release is found.
     *  Parameters: (version string, download URL). */
    std::function<void(const juce::String& version,
                       const juce::String& downloadUrl)> onUpdateAvailable;

    /** Called on message thread after detecting a successful post-update flag.
     *  Parameter: version string from the flag file. */
    std::function<void(const juce::String& version)> onPostUpdateNotification;

private:
#if JUCE_WINDOWS
    void performUpdate();
    double downloadProgress_ = -1.0;
    std::thread downloadThread_;
#endif

    std::thread updateCheckThread_;
    juce::String latestVersion_;
    juce::String latestDownloadUrl_;
    bool updateAvailable_ = false;

    /// Shared lifetime flag — set to false in destructor to invalidate pending callAsync lambdas
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateChecker)
};

} // namespace directpipe
