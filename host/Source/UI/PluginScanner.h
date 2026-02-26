/**
 * @file PluginScanner.h
 * @brief VST plugin scanner dialog with directory management
 */
#pragma once

#include <JuceHeader.h>
#include "../Audio/VSTChain.h"

namespace directpipe {

/**
 * @brief Content component for the plugin scanner dialog.
 *
 * Provides:
 * - Default + custom scan directory list
 * - Background scanning with progress
 * - Discovered plugin list with name/vendor/format
 * - Selection and callback to add to chain
 */
class PluginScannerComponent : public juce::Component,
                                public juce::Thread,
                                public juce::TableHeaderComponent::Listener {
public:
    explicit PluginScannerComponent(VSTChain& vstChain);
    ~PluginScannerComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Callback when user selects a plugin to add. */
    std::function<void(const juce::PluginDescription&)> onPluginSelected;

private:
    // juce::Thread
    void run() override;

    void addDefaultDirectories();
    void addCustomDirectory();
    void removeSelectedDirectory();
    void startScan();
    void clearCacheAndRescan();
    void refreshPluginList();
    void loadCachedPlugins();
    void saveCachedPlugins();
    juce::File getCacheFile() const;

    VSTChain& vstChain_;

    // Directory list
    juce::StringArray scanDirectories_;
    juce::ListBox directoryListBox_{"Scan Directories"};

    // Buttons
    juce::TextButton addDirButton_{"+ Add Directory"};
    juce::TextButton removeDirButton_{"- Remove"};
    juce::TextButton scanButton_{"Scan for Plugins"};
    juce::TextButton clearCacheButton_{"Clear Cache & Rescan"};
    juce::TextButton addPluginButton_{"Add Selected to Chain"};

    // Progress
    juce::Label progressLabel_;
    juce::ProgressBar* progressBar_ = nullptr;
    double scanProgress_ = 0.0;
    bool scanning_ = false;

    // Plugin list
    juce::KnownPluginList scannedPlugins_;
    juce::TableListBox pluginTable_{"Plugins"};

    // Section labels
    juce::Label dirSectionLabel_{"", "Scan Directories:"};
    juce::Label pluginSectionLabel_{"", "Discovered Plugins:"};

    // Table model
    class PluginTableModel : public juce::TableListBoxModel {
    public:
        explicit PluginTableModel(PluginScannerComponent& owner) : owner_(owner) {}
        int getNumRows() override;
        void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
        void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
        void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;
    private:
        PluginScannerComponent& owner_;
    };

    // Directory list model
    class DirectoryListModel : public juce::ListBoxModel {
    public:
        explicit DirectoryListModel(PluginScannerComponent& owner) : owner_(owner) {}
        int getNumRows() override { return owner_.scanDirectories_.size(); }
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    private:
        PluginScannerComponent& owner_;
    };

    std::unique_ptr<PluginTableModel> tableModel_;
    std::unique_ptr<DirectoryListModel> dirListModel_;

    // Search/filter
    juce::TextEditor searchBox_;
    std::vector<int> filteredIndices_;
    int sortColumnId_ = 0;
    bool sortAscending_ = true;
    void updateFilteredList();

    // TableHeaderComponent::Listener
    void tableColumnsChanged(juce::TableHeaderComponent*) override {}
    void tableColumnsResized(juce::TableHeaderComponent*) override {}
    void tableSortOrderChanged(juce::TableHeaderComponent*) override;

    static constexpr juce::uint32 kBgColour      = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurfaceColour  = 0xFF2A2A40;
    static constexpr juce::uint32 kAccentColour   = 0xFF6C63FF;
    static constexpr juce::uint32 kTextColour     = 0xFFE0E0E0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginScannerComponent)
};

} // namespace directpipe
