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
 * @file Main.cpp
 * @brief DirectPipe application entry point
 */

#include <JuceHeader.h>
#include <atomic>
#include <mutex>
#if ! JUCE_WINDOWS
#include <csignal>
#endif
#include "BinaryData.h"
#include "MainComponent.h"
#include "Control/StateBroadcaster.h"
#include "Control/Log.h"

// ═══════════════════════════════════════════════════════════════════
// Platform abstractions (AutoStart, ProcessPriority, MultiInstanceLock)
// ═══════════════════════════════════════════════════════════════════

#include "Platform/AutoStart.h"
#include "Platform/ProcessPriority.h"
#include "Platform/MultiInstanceLock.h"

// ═══════════════════════════════════════════════════════════════════
// Out-of-process plugin scanner mode
// When launched with "--scan <searchPaths> <outputFile> <pedalFile>",
// DirectPipe acts as a headless scanner process.
// If a bad plugin crashes the scanner, only this child process dies.
// ═══════════════════════════════════════════════════════════════════

static int runScannerMode(const juce::StringArray& args)
{
    // args: --scan <searchPaths(;-separated)> <outputXmlFile> <pedalFile> [<blacklistFile>]
    // The blacklistFile is optional and contains accumulated crashed plugin paths.

    auto logDir = directpipe::ControlMappingStore::getConfigDirectory();
    logDir.createDirectory();
    auto logFile = logDir.getChildFile("scanner-log.txt");

    logFile.replaceWithText("Scanner child process started at "
        + juce::Time::getCurrentTime().toString(true, true) + "\n");
    logFile.appendText("Args count: " + juce::String(args.size()) + "\n");
    for (int i = 0; i < args.size(); ++i)
        logFile.appendText("  [" + juce::String(i) + "] " + args[i] + "\n");

    if (args.size() < 4) {
        logFile.appendText("ERROR: Not enough arguments\n");
        return 1;
    }

    // JUCE's fromTokens(commandLine, true) preserves quote characters inside
    // the token strings.  Strip them so file paths are valid.
    juce::String searchPathsStr = args[1].unquoted();
    juce::File outputFile(args[2].unquoted());
    juce::File pedalFile(args[3].unquoted());
    juce::File blacklistFile;
    if (args.size() >= 5)
        blacklistFile = juce::File(args[4].unquoted());

    logFile.appendText("Search paths: " + searchPathsStr + "\n");
    logFile.appendText("Output file: " + outputFile.getFullPathName() + "\n");
    logFile.appendText("Pedal file: " + pedalFile.getFullPathName() + "\n");
    if (blacklistFile != juce::File())
        logFile.appendText("Blacklist file: " + blacklistFile.getFullPathName() + "\n");

    juce::AudioPluginFormatManager formatManager;
    formatManager.addDefaultFormats();

    logFile.appendText("Formats registered: " + juce::String(formatManager.getNumFormats()) + "\n");
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
        logFile.appendText("  Format: " + formatManager.getFormat(i)->getName() + "\n");

    juce::KnownPluginList pluginList;

    // Load intermediate results from previous child process attempts.
    // This avoids re-scanning plugins that were already found.
    if (outputFile.existsAsFile()) {
        auto xml = juce::parseXML(outputFile);
        if (xml) {
            pluginList.recreateFromXml(*xml);
            logFile.appendText("Loaded " + juce::String(pluginList.getNumTypes())
                + " previously found plugins from output file\n");
        }
    }

    // Load accumulated blacklist (plugins that crashed in prior attempts).
    // The parent process appends each crashed plugin path to this file.
    if (blacklistFile != juce::File() && blacklistFile.existsAsFile()) {
        auto lines = juce::StringArray::fromLines(blacklistFile.loadFileAsString());
        int blacklisted = 0;
        for (auto& line : lines) {
            auto trimmed = line.trim();
            if (trimmed.isNotEmpty()) {
                pluginList.addToBlacklist(trimmed);
                ++blacklisted;
            }
        }
        logFile.appendText("Blacklisted " + juce::String(blacklisted)
            + " previously crashed plugins\n");
    }

    juce::FileSearchPath searchPath;
    for (auto& dir : juce::StringArray::fromTokens(searchPathsStr, ";", ""))
        searchPath.add(juce::File(dir));

    logFile.appendText("Search path entries: " + juce::String(searchPath.getNumPaths()) + "\n");

    int totalScanned = 0;

    for (int f = 0; f < formatManager.getNumFormats(); ++f) {
        auto* format = formatManager.getFormat(f);
        logFile.appendText("Scanning format: " + format->getName() + "\n");

        juce::PluginDirectoryScanner scanner(
            pluginList, *format, searchPath, true, pedalFile);

        juce::String pluginName;
        while (scanner.scanNextFile(true, pluginName)) {
            ++totalScanned;
            logFile.appendText("  Scanned: " + pluginName + "\n");

            // Save intermediate results after each successful scan.
            auto xml = pluginList.createXml();
            if (xml)
                xml->writeTo(outputFile);
        }

        logFile.appendText("  Format done. Failed files: "
            + juce::String(scanner.getFailedFiles().size()) + "\n");
        for (auto& failed : scanner.getFailedFiles())
            logFile.appendText("    Failed: " + failed + "\n");
    }

    // Final write
    auto xml = pluginList.createXml();
    if (xml)
        xml->writeTo(outputFile);

    logFile.appendText("Scan complete. Total scanned: " + juce::String(totalScanned)
        + ", Found: " + juce::String(pluginList.getNumTypes()) + "\n");

    return 0;
}

// ═══════════════════════════════════════════════════════════════════

class DirectPipeApplication : public juce::JUCEApplication {
public:
    DirectPipeApplication() = default;

    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override
    {
        // Allow scanner child processes to run alongside the main instance
        auto args = juce::StringArray::fromTokens(
            juce::JUCEApplication::getCommandLineParameters(), true);
        if (args.contains("--scan")) return true;

        // Portable mode: allow multiple instances (each portable copy is independent)
        return directpipe::ControlMappingStore::isPortableMode();
    }

    void initialise(const juce::String& commandLine) override
    {
        auto args = juce::StringArray::fromTokens(commandLine, true);

        sessionStartMs_ = juce::Time::getMillisecondCounter();

        // Check for scanner mode (before session header — scanner has its own logging)
        if (args.size() >= 1 && args[0] == "--scan") {
            scannerMode_ = true;
            int result = runScannerMode(args);
            setApplicationReturnValue(result);
            quit();
            return;
        }

        // POSIX signal handling
#if ! JUCE_WINDOWS
        ::signal(SIGPIPE, SIG_IGN);  // Writing to a closed socket must not crash

        // SIGTERM: clean shutdown (save settings, unmap SHM, close servers).
        // JUCE only handles SIGINT on Linux — SIGTERM from systemd/kill goes unhandled,
        // causing abrupt termination without cleanup.
        ::signal(SIGTERM, [](int) {
            if (auto* app = juce::JUCEApplicationBase::getInstance())
                app->systemRequestedQuit();
        });
#endif

        // Elevate process priority and timer resolution for real-time audio
        directpipe::Platform::setHighPriority();

        // Portable mode: per-folder single instance lock
        // (JUCE's built-in lock is bypassed since moreThanOneInstanceAllowed returns true)
        if (directpipe::ControlMappingStore::isPortableMode()) {
            auto configDir = directpipe::ControlMappingStore::getConfigDirectory();
            auto lockName = "DirectPipe_Portable_"
                + juce::String::toHexString(configDir.getFullPathName().hashCode());
            portableLock_ = std::make_unique<juce::InterProcessLock>(lockName);
            if (!portableLock_->enter(0)) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "DirectPipe (Portable)",
                    "Another portable instance is already running from this folder.");
                quit();
                return;
            }
        }

        // Multi-instance external control priority
        int controlResult = directpipe::Platform::acquireExternalControlPriority(
            directpipe::ControlMappingStore::isPortableMode());
        if (controlResult < 0) {
            // A portable instance already owns external controls — block normal mode
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "DirectPipe",
                "A portable DirectPipe instance is currently using external controls.\n"
                "Please close it first, then try again.",
                "OK",
                nullptr,
                juce::ModalCallbackFunction::create([this](int) { quit(); }));
            return;
        }
        enableExternalControls_ = (controlResult == 1);

        // Log session header (OS, CPU, process info)
        directpipe::Log::sessionStart(getApplicationVersion());

        mainWindow_ = std::make_unique<MainWindow>(getApplicationName(), *this, enableExternalControls_);

        // Log audio/plugin configuration after MainComponent fully initialized
        auto safeWindow = juce::Component::SafePointer<MainWindow>(mainWindow_.get());
        juce::MessageManager::callAsync([safeWindow]() {
            if (!safeWindow) return;
            auto* mc = dynamic_cast<directpipe::MainComponent*>(safeWindow->getContentComponent());
            if (!mc) return;

            auto& engine = mc->getAudioEngine();
            auto* device = engine.getDeviceManager().getCurrentAudioDevice();
            if (device) {
                auto setup = engine.getDeviceManager().getAudioDeviceSetup();
                directpipe::Log::audioConfig(
                    engine.getDeviceManager().getCurrentAudioDeviceType(),
                    setup.inputDeviceName,
                    setup.outputDeviceName,
                    device->getCurrentSampleRate(),
                    device->getCurrentBufferSizeSamples());
            }

            auto& chain = engine.getVSTChain();
            juce::StringArray pluginNames;
            for (int i = 0; i < chain.getPluginCount(); ++i) {
                auto* slot = chain.getPluginSlot(i);
                if (slot) pluginNames.add(slot->name);
            }
            auto* pm = mc->getPresetManager();
            directpipe::Log::pluginChain(pluginNames, pm ? pm->getActiveSlot() : -1, "");
        });
    }

    void shutdown() override
    {
        if (scannerMode_) return;
        directpipe::Log::sessionEnd(sessionStartMs_);
        trayIcon_.reset();
        mainWindow_.reset();
        directpipe::Platform::releaseExternalControlPriority();
        directpipe::Platform::restoreNormalPriority();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String& /*commandLine*/) override
    {
        if (!scannerMode_)
            showWindow();
    }

    void showWindow()
    {
        if (mainWindow_) {
            mainWindow_->setVisible(true);
            mainWindow_->toFront(true);
        }
    }

    void hideWindowToTray()
    {
        if (!trayIcon_) {
            directpipe::StateBroadcaster* bc = nullptr;
            if (mainWindow_) {
                auto* mc = dynamic_cast<directpipe::MainComponent*>(
                    mainWindow_->getContentComponent());
                if (mc) bc = &mc->getBroadcaster();
            }
            trayIcon_ = std::make_unique<DirectPipeTrayIcon>(*this, bc);
        }

        if (mainWindow_) {
#if JUCE_LINUX
            // GNOME 42+ removed legacy XEmbed system tray support.
            // Minimize to taskbar instead of hiding — ensures the window
            // is always recoverable even without a tray icon (AppIndicator).
            mainWindow_->setMinimised(true);
#else
            mainWindow_->setVisible(false);
#endif
        }
    }

private:
    bool scannerMode_ = false;
    bool enableExternalControls_ = true;
    juce::int64 sessionStartMs_ = 0;

    // ─── System Tray Icon ─────────────────────────────────────────
    class DirectPipeTrayIcon : public juce::SystemTrayIconComponent,
                               public directpipe::StateListener,
                               public juce::Timer {
    public:
        explicit DirectPipeTrayIcon(DirectPipeApplication& app,
                                     directpipe::StateBroadcaster* broadcaster = nullptr)
            : app_(app), broadcaster_(broadcaster)
        {
            auto smallIcon = juce::ImageFileFormat::loadFrom(
                BinaryData::icon_16_png, BinaryData::icon_16_pngSize);
            auto largeIcon = juce::ImageFileFormat::loadFrom(
                BinaryData::icon_32_png, BinaryData::icon_32_pngSize);
            setIconImage(smallIcon, largeIcon);
            {
                juce::String initTooltip = "DirectPipe";
                if (directpipe::ControlMappingStore::isPortableMode())
                    initTooltip += app_.enableExternalControls_ ? " (Portable)" : " (Portable/Audio Only)";
                initTooltip += " - Running";
                setIconTooltip(initTooltip);
            }

            if (broadcaster_)
                broadcaster_->addListener(this);
            startTimerHz(2);
        }

        ~DirectPipeTrayIcon() override
        {
            stopTimer();
            if (broadcaster_)
                broadcaster_->removeListener(this);
        }

        void mouseDoubleClick(const juce::MouseEvent&) override
        {
            app_.showWindow();
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            if (e.mods.isRightButtonDown() || e.mods.isPopupMenu()) {
                showTrayMenu();
            } else if (e.mods.isLeftButtonDown()) {
                app_.showWindow();
            }
        }

        // StateListener
        void onStateChanged(const directpipe::AppState& state) override
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            cachedState_ = state;
            stateDirty_.store(true, std::memory_order_release);
        }

        void timerCallback() override
        {
            if (!stateDirty_.load(std::memory_order_acquire)) return;

            directpipe::AppState snapshot;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                snapshot = cachedState_;
                stateDirty_.store(false, std::memory_order_relaxed);
            }

            juce::String tooltip = "DirectPipe";
            if (directpipe::ControlMappingStore::isPortableMode()) {
                tooltip += app_.enableExternalControls_ ? " (Portable)" : " (Portable/Audio Only)";
            }

            if (snapshot.activeSlot == 5 || snapshot.autoSlotActive)
                tooltip += " [Auto]";
            else if (snapshot.activeSlot >= 0 && snapshot.activeSlot <= 4) {
                char slotChar = 'A' + static_cast<char>(juce::jlimit(0, 4, snapshot.activeSlot));
                tooltip += " [Slot " + juce::String::charToString(slotChar) + "]";
            }
            tooltip += " | CPU " + juce::String(snapshot.cpuPercent, 1) + "%";
            tooltip += " | " + juce::String(snapshot.latencyMs, 1) + "ms";

            if (snapshot.muted)
                tooltip += " | MUTED";
            else {
                if (snapshot.outputMuted)
                    tooltip += " | OUT-MUTE";
                if (!snapshot.monitorEnabled)
                    tooltip += " | MON-MUTE";
            }

            setIconTooltip(tooltip);
        }

    private:
        void showTrayMenu()
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Show Window");
            menu.addSeparator();
            if (directpipe::Platform::isAutoStartSupported()) {
#if JUCE_MAC
                const char* autoStartLabel = "Open at Login";
#else
                const char* autoStartLabel = "Start with System";
#endif
                menu.addItem(3, autoStartLabel,
                             true, directpipe::Platform::isAutoStartEnabled());
            }
            menu.addSeparator();
            menu.addItem(2, "Quit DirectPipe");

            menu.showMenuAsync(juce::PopupMenu::Options(),
                [this](int result) {
                    if (result == 1) {
                        app_.showWindow();
                    } else if (result == 2) {
                        juce::JUCEApplication::getInstance()->systemRequestedQuit();
                    } else if (result == 3) {
                        bool desired = !directpipe::Platform::isAutoStartEnabled();
                        if (!directpipe::Platform::setAutoStartEnabled(desired))
                            juce::Logger::writeToLog("[APP] Failed to change auto-start setting");
                    }
                });
        }

        DirectPipeApplication& app_;
        directpipe::StateBroadcaster* broadcaster_ = nullptr;
        directpipe::AppState cachedState_;
        std::mutex stateMutex_;
        std::atomic<bool> stateDirty_{false};
    };

    // ─── Main Window ──────────────────────────────────────────────
    class MainWindow : public juce::DocumentWindow {
    public:
        MainWindow(const juce::String& name, DirectPipeApplication& app,
                   bool enableExternalControls = true)
            : DocumentWindow(name,
                             juce::Desktop::getInstance().getDefaultLookAndFeel()
                                 .findColour(ResizableWindow::backgroundColourId),
                             DocumentWindow::allButtons),
              app_(app)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new directpipe::MainComponent(enableExternalControls), true);

            setResizable(true, true);
            setResizeLimits(600, 800, 1400, 1200);
            centreWithSize(getWidth(), getHeight());
            // setVisible deferred — MainComponent shows window after initial load completes
            // NOTE: Process priority already set by Platform::setHighPriority() in initialise()
        }

        void closeButtonPressed() override
        {
            app_.hideWindowToTray();
        }

    private:
        DirectPipeApplication& app_;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow_;
    std::unique_ptr<DirectPipeTrayIcon> trayIcon_;
    std::unique_ptr<juce::InterProcessLock> portableLock_;
};

START_JUCE_APPLICATION(DirectPipeApplication)
