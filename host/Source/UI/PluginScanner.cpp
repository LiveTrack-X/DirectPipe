/**
 * @file PluginScanner.cpp
 * @brief VST plugin scanner dialog implementation
 */

#include "PluginScanner.h"

namespace directpipe {

// ─── DirectoryListModel ──────────────────────────────────────────

void PluginScannerComponent::DirectoryListModel::paintListBoxItem(
    int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll(juce::Colour(0xFF3A3A5A));

    g.setColour(juce::Colour(kTextColour));
    g.setFont(juce::Font(13.0f));

    if (rowNumber >= 0 && rowNumber < owner_.scanDirectories_.size())
        g.drawText(owner_.scanDirectories_[rowNumber], 8, 0, width - 8, height,
                   juce::Justification::centredLeft);
}

// ─── PluginTableModel ────────────────────────────────────────────

int PluginScannerComponent::PluginTableModel::getNumRows()
{
    return owner_.scannedPlugins_.getNumTypes();
}

void PluginScannerComponent::PluginTableModel::paintRowBackground(
    juce::Graphics& g, int /*rowNumber*/, int /*width*/, int /*height*/, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll(juce::Colour(0xFF3A3A5A));
    else
        g.fillAll(juce::Colour(kSurfaceColour));
}

void PluginScannerComponent::PluginTableModel::paintCell(
    juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool /*rowIsSelected*/)
{
    auto types = owner_.scannedPlugins_.getTypes();
    if (rowNumber < 0 || rowNumber >= types.size()) return;

    const auto& desc = types[rowNumber];
    g.setColour(juce::Colour(kTextColour));
    g.setFont(juce::Font(13.0f));

    juce::String text;
    switch (columnId) {
        case 1: text = desc.name; break;
        case 2: text = desc.manufacturerName; break;
        case 3: text = desc.pluginFormatName; break;
        default: break;
    }
    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

void PluginScannerComponent::PluginTableModel::cellDoubleClicked(
    int rowNumber, int /*columnId*/, const juce::MouseEvent&)
{
    auto types = owner_.scannedPlugins_.getTypes();
    if (rowNumber >= 0 && rowNumber < types.size()) {
        if (owner_.onPluginSelected)
            owner_.onPluginSelected(types[rowNumber]);
    }
}

// ─── PluginScannerComponent ──────────────────────────────────────

PluginScannerComponent::PluginScannerComponent(VSTChain& vstChain)
    : juce::Thread("VST Scanner"),
      vstChain_(vstChain)
{
    tableModel_ = std::make_unique<PluginTableModel>(*this);
    dirListModel_ = std::make_unique<DirectoryListModel>(*this);

    // Section labels
    dirSectionLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    dirSectionLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(dirSectionLabel_);

    pluginSectionLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    pluginSectionLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(pluginSectionLabel_);

    // Directory list
    directoryListBox_.setModel(dirListModel_.get());
    directoryListBox_.setRowHeight(22);
    directoryListBox_.setColour(juce::ListBox::backgroundColourId, juce::Colour(kSurfaceColour));
    addAndMakeVisible(directoryListBox_);

    // Directory buttons
    addDirButton_.onClick = [this] { addCustomDirectory(); };
    removeDirButton_.onClick = [this] { removeSelectedDirectory(); };
    addAndMakeVisible(addDirButton_);
    addAndMakeVisible(removeDirButton_);

    // Scan button
    scanButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(kAccentColour));
    scanButton_.onClick = [this] { startScan(); };
    addAndMakeVisible(scanButton_);

    // Clear cache & rescan button
    clearCacheButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFE05050));
    clearCacheButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    clearCacheButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    clearCacheButton_.onClick = [this] { clearCacheAndRescan(); };
    addAndMakeVisible(clearCacheButton_);

    // Progress label
    progressLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    progressLabel_.setFont(juce::Font(12.0f));
    addAndMakeVisible(progressLabel_);

    // Plugin table
    pluginTable_.setModel(tableModel_.get());
    pluginTable_.setRowHeight(24);
    pluginTable_.setColour(juce::ListBox::backgroundColourId, juce::Colour(kSurfaceColour));
    pluginTable_.getHeader().addColumn("Plugin Name", 1, 250, 100, 400);
    pluginTable_.getHeader().addColumn("Vendor", 2, 150, 80, 250);
    pluginTable_.getHeader().addColumn("Format", 3, 80, 60, 120);
    pluginTable_.getHeader().setColour(juce::TableHeaderComponent::backgroundColourId,
                                       juce::Colour(0xFF2A2A40));
    pluginTable_.getHeader().setColour(juce::TableHeaderComponent::textColourId,
                                       juce::Colour(kTextColour));
    addAndMakeVisible(pluginTable_);

    // Add plugin button
    addPluginButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF4CAF50));
    addPluginButton_.onClick = [this] {
        int selected = pluginTable_.getSelectedRow();
        auto types = scannedPlugins_.getTypes();
        if (selected >= 0 && selected < types.size() && onPluginSelected) {
            onPluginSelected(types[selected]);
        }
    };
    addAndMakeVisible(addPluginButton_);

    // Set up default directories
    addDefaultDirectories();

    // Load cached plugin list if available
    loadCachedPlugins();
    refreshPluginList();
}

PluginScannerComponent::~PluginScannerComponent()
{
    if (isThreadRunning()) {
        signalThreadShouldExit();
        waitForThreadToExit(5000);
    }
}

void PluginScannerComponent::addDefaultDirectories()
{
    scanDirectories_.clear();

    scanDirectories_.add("C:\\Program Files\\Common Files\\VST3");
    scanDirectories_.add("C:\\Program Files (x86)\\Common Files\\VST3");
    scanDirectories_.add("C:\\Program Files\\Common Files\\VST");
    scanDirectories_.add("C:\\Program Files\\VstPlugins");
    scanDirectories_.add("C:\\Program Files (x86)\\VstPlugins");
    scanDirectories_.add("C:\\Program Files\\Steinberg\\VST3");
    scanDirectories_.add("C:\\Program Files\\Steinberg\\VSTPlugins");
    scanDirectories_.add("C:\\Program Files (x86)\\Steinberg\\VSTPlugins");

    directoryListBox_.updateContent();
}

void PluginScannerComponent::addCustomDirectory()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Select VST Plugin Directory", juce::File(), "", true);

    chooser->launchAsync(juce::FileBrowserComponent::openMode |
                         juce::FileBrowserComponent::canSelectDirectories,
                         [this, chooser](const juce::FileChooser& fc) {
        auto result = fc.getResult();
        if (result.isDirectory()) {
            if (!scanDirectories_.contains(result.getFullPathName())) {
                scanDirectories_.add(result.getFullPathName());
                directoryListBox_.updateContent();
            }
        }
    });
}

void PluginScannerComponent::removeSelectedDirectory()
{
    int selected = directoryListBox_.getSelectedRow();
    if (selected >= 0 && selected < scanDirectories_.size()) {
        scanDirectories_.remove(selected);
        directoryListBox_.updateContent();
    }
}

void PluginScannerComponent::startScan()
{
    if (scanning_) return;
    if (isThreadRunning()) return;  // Safety: don't start if thread still active

    scanning_ = true;
    scanProgress_ = 0.0;
    scanButton_.setEnabled(false);
    scanButton_.setButtonText("Scanning...");
    addDirButton_.setEnabled(false);
    removeDirButton_.setEnabled(false);
    clearCacheButton_.setEnabled(false);
    progressLabel_.setText("Preparing scan...", juce::dontSendNotification);

    startThread();
}

void PluginScannerComponent::clearCacheAndRescan()
{
    if (scanning_) return;

    // Delete cache file and dead man's pedal
    auto cacheFile = getCacheFile();
    auto pedalFile = cacheFile.getSiblingFile("scan-deadmanspedal.txt");
    auto resultFile = cacheFile.getSiblingFile("scan-result.xml");

    cacheFile.deleteFile();
    pedalFile.deleteFile();
    resultFile.deleteFile();

    // Clear in-memory plugin list
    scannedPlugins_.clear();
    refreshPluginList();

    progressLabel_.setText("Cache cleared. Starting fresh scan...",
                           juce::dontSendNotification);

    // Start a fresh scan
    startScan();
}

void PluginScannerComponent::run()
{
    // Out-of-process scanning: launch DirectPipe.exe --scan as a child process.
    // If a bad plugin crashes the scanner, only the child process dies.
    // The parent accumulates a blacklist of crashed plugins across retries
    // so the child can skip ALL previously crashed plugins, not just the last one.

    auto exeFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    auto exePath = exeFile.getFullPathName();

    juce::String searchPathsStr;
    for (int i = 0; i < scanDirectories_.size(); ++i) {
        if (i > 0) searchPathsStr += ";";
        searchPathsStr += scanDirectories_[i];
    }

    auto outputFile = getCacheFile().getSiblingFile("scan-result.xml");
    auto pedalFile = getCacheFile().getSiblingFile("scan-deadmanspedal.txt");
    auto blacklistFile = getCacheFile().getSiblingFile("scan-blacklist.txt");

    outputFile.deleteFile();
    blacklistFile.deleteFile();
    pedalFile.deleteFile();

    constexpr int maxRetries = 10;
    int attempt = 0;
    int badPluginCount = 0;
    bool lastRunCrashed = false;
    bool startFailed = false;

    while (attempt < maxRetries && !threadShouldExit()) {
        ++attempt;

        // After a crash, read the pedal file and accumulate the crashed plugin
        // into the blacklist file so ALL bad plugins are skipped in subsequent attempts.
        if (pedalFile.existsAsFile()) {
            auto crashed = pedalFile.loadFileAsString().trim();
            if (crashed.isNotEmpty()) {
                blacklistFile.appendText(crashed + "\n");
                ++badPluginCount;
            }
        }

        juce::MessageManager::callAsync([this, attempt, badPluginCount] {
            juce::String msg = "Scanning (attempt " + juce::String(attempt) + ")";
            if (badPluginCount > 0)
                msg += " - skipped " + juce::String(badPluginCount) + " bad plugin(s)";
            msg += "...";
            progressLabel_.setText(msg, juce::dontSendNotification);
        });

        juce::ChildProcess scanner;
        juce::String cmdLine = "\"" + exePath + "\" --scan "
            + "\"" + searchPathsStr + "\" "
            + "\"" + outputFile.getFullPathName() + "\" "
            + "\"" + pedalFile.getFullPathName() + "\" "
            + "\"" + blacklistFile.getFullPathName() + "\"";

        if (!scanner.start(cmdLine)) {
            startFailed = true;
            break;
        }

        bool finished = scanner.waitForProcessToFinish(300000);
        auto exitCode = scanner.getExitCode();

        if (!finished)
            scanner.kill();

        // Read intermediate results (saved after each plugin)
        if (outputFile.existsAsFile()) {
            auto xmlStr = outputFile.loadFileAsString();
            if (xmlStr.isNotEmpty()) {
                juce::MessageManager::callAsync([this, xmlStr] {
                    if (auto parsed = juce::parseXML(xmlStr))
                        scannedPlugins_.recreateFromXml(*parsed);
                    refreshPluginList();
                });
            }
        }

        lastRunCrashed = !finished || exitCode != 0;

        if (!lastRunCrashed)
            break;

        juce::Thread::sleep(200);
    }

    // Handle last crash's pedal entry
    if (lastRunCrashed && pedalFile.existsAsFile()) {
        auto crashed = pedalFile.loadFileAsString().trim();
        if (crashed.isNotEmpty())
            ++badPluginCount;
    }

    // Final read
    juce::String finalXml;
    if (outputFile.existsAsFile())
        finalXml = outputFile.loadFileAsString();

    juce::MessageManager::callAsync([this, finalXml, lastRunCrashed, startFailed, badPluginCount] {
        if (finalXml.isNotEmpty()) {
            if (auto parsed = juce::parseXML(finalXml))
                scannedPlugins_.recreateFromXml(*parsed);
        }
        refreshPluginList();
        saveCachedPlugins();

        scanning_ = false;
        scanButton_.setEnabled(true);
        scanButton_.setButtonText("Scan for Plugins");
        addDirButton_.setEnabled(true);
        removeDirButton_.setEnabled(true);
        clearCacheButton_.setEnabled(true);

        juce::String statusText;
        if (startFailed) {
            statusText = "Error: Failed to launch scanner process";
        } else {
            statusText = "Found " + juce::String(scannedPlugins_.getNumTypes()) + " plugins";
            if (badPluginCount > 0)
                statusText += " (skipped " + juce::String(badPluginCount) + " bad plugin(s))";
        }
        progressLabel_.setText(statusText, juce::dontSendNotification);
    });
}

void PluginScannerComponent::refreshPluginList()
{
    pluginTable_.updateContent();
    pluginTable_.repaint();

    pluginSectionLabel_.setText(
        "Discovered Plugins (" + juce::String(scannedPlugins_.getNumTypes()) + "):",
        juce::dontSendNotification);
}

juce::File PluginScannerComponent::getCacheFile() const
{
    auto dir = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory).getChildFile("DirectPipe");
    dir.createDirectory();
    return dir.getChildFile("plugin-cache.xml");
}

void PluginScannerComponent::loadCachedPlugins()
{
    auto cacheFile = getCacheFile();
    if (!cacheFile.existsAsFile()) return;

    auto xml = juce::parseXML(cacheFile);
    if (xml) {
        scannedPlugins_.recreateFromXml(*xml);
    }
}

void PluginScannerComponent::saveCachedPlugins()
{
    auto xml = scannedPlugins_.createXml();
    if (xml) {
        auto cacheFile = getCacheFile();
        xml->writeTo(cacheFile);
    }
}

// ─── Paint / Layout ──────────────────────────────────────────────

void PluginScannerComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBgColour));
}

void PluginScannerComponent::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    int y = bounds.getY();

    // Directory section label
    dirSectionLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), 22);
    y += 24;

    // Directory list (120px)
    directoryListBox_.setBounds(bounds.getX(), y, bounds.getWidth(), 100);
    y += 104;

    // Directory buttons
    addDirButton_.setBounds(bounds.getX(), y, 120, 26);
    removeDirButton_.setBounds(bounds.getX() + 124, y, 80, 26);
    clearCacheButton_.setBounds(bounds.getRight() - 310, y, 150, 26);
    scanButton_.setBounds(bounds.getRight() - 150, y, 150, 26);
    y += 32;

    // Progress
    progressLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), 20);
    y += 24;

    // Plugin section label
    pluginSectionLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), 22);
    y += 24;

    // Plugin table (fill remaining minus button)
    int tableH = bounds.getBottom() - y - 34;
    pluginTable_.setBounds(bounds.getX(), y, bounds.getWidth(), tableH);
    y += tableH + 4;

    // Add plugin button
    addPluginButton_.setBounds(bounds.getRight() - 200, y, 200, 28);
}

} // namespace directpipe
