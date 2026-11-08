#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"
#include "AppSettings.h"
#include "PluginScanning.h"
#include "Theme.h"

class LiveRackApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    // Only the hidden plugin-scanner copy may run alongside the main app
    bool moreThanOneInstanceAllowed() override
    {
        return getCommandLineParameters().contains (PluginScanning::workerUID);
    }

    void initialise (const juce::String& commandLine) override
    {
        auto worker = std::make_unique<PluginScanning::Worker>();

        if (worker->initialiseFromCommandLine (commandLine, PluginScanning::workerUID))
        {
           #if JUCE_MAC
            juce::Process::setDockIconVisible (false);
           #endif
            scannerWorker = std::move (worker);
            return;
        }

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        scannerWorker = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }
    void systemRequestedQuit() override            { quit(); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());

            const auto saved = AppSettings::get ("mainWindow");
            if (saved.isNotEmpty())
                restoreWindowStateFromString (saved);

            setVisible (true);
        }

        ~MainWindow() override { AppSettings::set ("mainWindow", getWindowStateAsString()); }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

private:
    Theme::LookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<PluginScanning::Worker> scannerWorker;
};

START_JUCE_APPLICATION (LiveRackApplication)
