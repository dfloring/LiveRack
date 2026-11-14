#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"
#include "ClientComponent.h"
#include "AppMode.h"
#include "AppSettings.h"
#include "PluginScanning.h"
#include "IsolatedPlugin.h"
#include "ServerMode.h"
#include "Theme.h"

class LiveRackApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    // Only the hidden plugin-scanner copy may run alongside the main app
    bool moreThanOneInstanceAllowed() override
    {
        return getCommandLineParameters().contains (PluginScanning::workerUID)
            || getCommandLineParameters().contains (Isolation::workerUID)
            || getCommandLineParameters().contains ("--liverackwatcher")
            || getCommandLineParameters().contains ("--client");   // a Client can run beside a server on the same computer
    }

    void initialise (const juce::String& commandLine) override
    {
        // A helper process that runs one isolated plugin
        if ((pluginHelper = Isolation::startWorkerIfRequested (commandLine)) != nullptr)
        {
           #if JUCE_MAC
            juce::Process::setDockIconVisible (false);
           #endif
            juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
            return;
        }

        // The watcher that restarts LiveRack after a crash
        if ((watcher = ServerMode::startWatcherIfRequested (commandLine)) != nullptr)
        {
           #if JUCE_MAC
            juce::Process::setDockIconVisible (false);
           #endif
            return;
        }

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

        // Native or Client: "--client [host[:port]]", else as it was last time
        auto args = juce::StringArray::fromTokens (commandLine, true);
        args.trim();
        const int clientArg = args.indexOf ("--client");
        bool client = clientArg >= 0 || AppSettings::get ("appMode") == "client";
        juce::String host;
        int port = 0;
        if (clientArg >= 0 && clientArg + 1 < args.size() && ! args[clientArg + 1].startsWith ("-"))
            host = args[clientArg + 1].unquoted();
        else if (client)
            host = AppSettings::get ("clientLastServer");   // straight back to the last server
        if (host.containsChar (':'))
        {
            port = host.fromLastOccurrenceOf (":", false, false).getIntValue();
            host = host.upToLastOccurrenceOf (":", false, false);
        }

        AppMode::switchTo = [this] (bool toClient)
        {
            // After the menu that asked has gone
            juce::MessageManager::callAsync ([this, toClient]
            {
                if (mainWindow != nullptr)
                    mainWindow->setMode (toClient, toClient ? AppSettings::get ("clientLastServer").upToLastOccurrenceOf (":", false, false) : juce::String(),
                                         AppSettings::get ("clientLastServer").fromLastOccurrenceOf (":", false, false).getIntValue(), true);
            });
        };

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), client, host, port);

        // A Client started beside a server on this computer leaves Server mode to it
        serverModeStarted = clientArg < 0;
        if (serverModeStarted)
            ServerMode::appStarted();
    }

    void shutdown() override
    {
        AppMode::switchTo = nullptr;
        if (mainWindow != nullptr && serverModeStarted)
            ServerMode::appQuitting();
        mainWindow = nullptr;
        watcher = nullptr;
        scannerWorker = nullptr;
        pluginHelper = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }
    void systemRequestedQuit() override            { quit(); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, bool client, const juce::String& host, int port)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setMode (client, host, port, false);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());

            const auto saved = AppSettings::get ("mainWindow");
            if (saved.isNotEmpty())
                restoreWindowStateFromString (saved);

            setVisible (true);
        }

        // Native (the show and its audio here) or Client (a server's screens)
        // keepSize: switching from the menu (also remembered for next time)
        void setMode (bool client, const juce::String& host, int port, bool keepSize)
        {
            AppMode::isClient = client;
            if (keepSize)
                AppSettings::set ("appMode", client ? "client" : "native");
            clearContentComponent();   // Native: the show is kept as it closes, and the audio stops

            if (client)
            {
                setName ("LiveRack Client");
                setContentOwned (new ClientComponent (host, port), ! keepSize);
            }
            else
            {
                setName (JUCE_APPLICATION_NAME_STRING);
                setContentOwned (new MainComponent(), ! keepSize);
            }
        }

        ~MainWindow() override { AppSettings::set ("mainWindow", getWindowStateAsString()); }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

private:
    Theme::LookAndFeel lookAndFeel;
    bool serverModeStarted = false;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<PluginScanning::Worker> scannerWorker;
    std::unique_ptr<Isolation::Worker> pluginHelper;
    std::unique_ptr<ServerMode::Watcher> watcher;
};

START_JUCE_APPLICATION (LiveRackApplication)
