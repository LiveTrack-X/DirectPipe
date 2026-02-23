/**
 * @file Main.cpp
 * @brief DirectPipe application entry point
 */

#include <JuceHeader.h>
#include "BinaryData.h"
#include "MainComponent.h"

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

    auto logDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DirectPipe");
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
    bool moreThanOneInstanceAllowed() override           { return true; }

    void initialise(const juce::String& commandLine) override
    {
        auto args = juce::StringArray::fromTokens(commandLine, true);

        // Check for scanner mode
        if (args.size() >= 1 && args[0] == "--scan") {
            scannerMode_ = true;
            int result = runScannerMode(args);
            setApplicationReturnValue(result);
            quit();
            return;
        }

        mainWindow_ = std::make_unique<MainWindow>(getApplicationName(), *this);
    }

    void shutdown() override
    {
        if (scannerMode_) return;
        trayIcon_.reset();
        mainWindow_.reset();
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
        if (mainWindow_) {
            mainWindow_->setVisible(false);
        }

        if (!trayIcon_) {
            trayIcon_ = std::make_unique<DirectPipeTrayIcon>(*this);
        }
    }

private:
    bool scannerMode_ = false;

    // ─── System Tray Icon ─────────────────────────────────────────
    class DirectPipeTrayIcon : public juce::SystemTrayIconComponent {
    public:
        explicit DirectPipeTrayIcon(DirectPipeApplication& app)
            : app_(app)
        {
            // Load icon from embedded binary data
            auto fullIcon = juce::ImageFileFormat::loadFrom(
                BinaryData::icon_png, BinaryData::icon_pngSize);

            // Scale to 16x16 for tray icon
            juce::Image icon(juce::Image::ARGB, 16, 16, true);
            juce::Graphics g(icon);
            g.drawImageWithin(fullIcon, 0, 0, 16, 16,
                              juce::RectanglePlacement::centred);
            setIconImage(icon, icon);
            setIconTooltip("DirectPipe - Running");
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

    private:
        void showTrayMenu()
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Show Window");
            menu.addSeparator();
            menu.addItem(2, "Quit DirectPipe");

            menu.showMenuAsync(juce::PopupMenu::Options(),
                [this](int result) {
                    if (result == 1) {
                        app_.showWindow();
                    } else if (result == 2) {
                        juce::JUCEApplication::getInstance()->systemRequestedQuit();
                    }
                });
        }

        DirectPipeApplication& app_;
    };

    // ─── Main Window ──────────────────────────────────────────────
    class MainWindow : public juce::DocumentWindow {
    public:
        MainWindow(const juce::String& name, DirectPipeApplication& app)
            : DocumentWindow(name,
                             juce::Desktop::getInstance().getDefaultLookAndFeel()
                                 .findColour(ResizableWindow::backgroundColourId),
                             DocumentWindow::allButtons),
              app_(app)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new directpipe::MainComponent(), true);

            setResizable(true, true);
            setResizeLimits(600, 800, 1400, 1200);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);

        #if JUCE_WINDOWS
            SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        #endif
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
};

START_JUCE_APPLICATION(DirectPipeApplication)
