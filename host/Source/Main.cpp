/**
 * @file Main.cpp
 * @brief DirectPipe application entry point
 */

#include <JuceHeader.h>
#include "MainComponent.h"

class DirectPipeApplication : public juce::JUCEApplication {
public:
    DirectPipeApplication() = default;

    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override           { return false; }

    void initialise(const juce::String& /*commandLine*/) override
    {
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow_.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String& /*commandLine*/) override
    {
        // Bring existing window to front
        if (mainWindow_) {
            mainWindow_->toFront(true);
        }
    }

private:
    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name,
                             juce::Desktop::getInstance().getDefaultLookAndFeel()
                                 .findColour(ResizableWindow::backgroundColourId),
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new directpipe::MainComponent(), true);

            setResizable(true, true);
            setResizeLimits(600, 500, 1200, 900);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);

        #if JUCE_WINDOWS
            // Set high-priority for the process (audio performance)
            SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        #endif
        }

        void closeButtonPressed() override
        {
            // Minimize to tray instead of closing (Phase 4)
            // For now, just quit
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow_;
};

START_JUCE_APPLICATION(DirectPipeApplication)
