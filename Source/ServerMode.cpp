#include "ServerMode.h"
#include "AppSettings.h"

namespace
{
    juce::File appFile()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::currentApplicationFile);
       #else
        return juce::File::getSpecialLocation (juce::File::currentExecutableFile);
       #endif
    }

    juce::File restartMarker() { return RackEngine::getSettingsDir().getChildFile ("restarted-after-crash"); }
    juce::File crashLog()      { return RackEngine::getSettingsDir().getChildFile ("crash-restarts.txt"); }

   #if JUCE_MAC
    juce::File loginFile() { return juce::File ("~/Library/LaunchAgents/com.liverack.host.plist"); }

    juce::String loginFileContent()
    {
        auto esc = [] (juce::String s) { return s.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;"); };
        return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
               "<plist version=\"1.0\">\n<dict>\n"
               "  <key>Label</key><string>com.liverack.host</string>\n"
               "  <key>ProgramArguments</key>\n  <array>\n"
               "    <string>/usr/bin/open</string>\n    <string>-a</string>\n"
               "    <string>" + esc (appFile().getFullPathName()) + "</string>\n  </array>\n"
               "  <key>RunAtLoad</key><true/>\n"
               "</dict>\n</plist>\n";
    }
   #elif JUCE_LINUX
    juce::File loginFile() { return juce::File ("~/.config/autostart/liverack.desktop"); }

    juce::String loginFileContent()
    {
        return "[Desktop Entry]\nType=Application\nName=LiveRack\nExec=\"" + appFile().getFullPathName() + "\"\n";
    }
   #elif JUCE_WINDOWS
    const char* const runKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\LiveRack";
    juce::String runValue() { return "\"" + appFile().getFullPathName() + "\""; }
   #endif

    //==========================================================================
    struct WatcherLink final : public juce::ChildProcessCoordinator
    {
        void handleMessageFromWorker (const juce::MemoryBlock&) override {}

        void handleConnectionLost() override
        {
            // The watcher went away (not us quitting): start another
            if (! quitting)
                juce::MessageManager::callAsync ([] { ServerMode::setRestartAfterCrash (ServerMode::restartsAfterCrash()); });
        }

        std::atomic<bool> quitting { false };
    };

    std::unique_ptr<WatcherLink> watcherLink;
    int restartSetting = -1; // not read yet

    void startWatcher()
    {
        if (watcherLink != nullptr)
        {
            watcherLink->quitting = true;
            watcherLink.reset();
        }

        auto link = std::make_unique<WatcherLink>();
        if (link->launchWorkerProcess (juce::File::getSpecialLocation (juce::File::currentExecutableFile),
                                       ServerMode::watcherUID, 0, 0))
            watcherLink = std::move (link);
    }

    void stopWatcher()
    {
        if (watcherLink == nullptr)
            return;

        watcherLink->quitting = true;
        watcherLink->sendMessageToWorker (juce::MemoryBlock ("bye", 3));
        juce::Thread::sleep (150); // let it hear that before it is stopped
        watcherLink.reset();
    }

    //==========================================================================
    class WatcherProcess final : public ServerMode::Watcher,
                                 private juce::ChildProcessWorker
    {
    public:
        using juce::ChildProcessWorker::initialiseFromCommandLine;

    private:
        void handleMessageFromCoordinator (const juce::MemoryBlock& mb) override
        {
            if (mb.toString() == "bye")
                properQuit = true;
        }

        void handleConnectionLost() override
        {
            const bool crashed = ! properQuit.load();
            juce::MessageManager::callAsync ([crashed]
            {
                if (crashed)
                    juce::Timer::callAfterDelay (1500, [] { relaunch(); juce::JUCEApplicationBase::quit(); });
                else
                    juce::JUCEApplicationBase::quit();
            });
        }

        static void relaunch()
        {
            // Not more than three times in five minutes: something is badly wrong then
            const auto now = juce::Time::currentTimeMillis();
            juce::StringArray recent;

            for (auto& line : juce::StringArray::fromLines (crashLog().loadFileAsString()))
                if (now - line.getLargeIntValue() < 5 * 60 * 1000 && line.getLargeIntValue() > 0)
                    recent.add (line);

            recent.add (juce::String (now));
            crashLog().replaceWithText (recent.joinIntoString ("\n"));

            if (recent.size() > 3)
                return;

            restartMarker().replaceWithText (juce::String (now));
            appFile().startAsProcess();
        }

        std::atomic<bool> properQuit { false };
    };
}

//==============================================================================
juce::String ServerMode::startAtLoginProblem()
{
   #if JUCE_MAC
    if (appFile().getFullPathName().contains ("/AppTranslocation/"))
        return "Move LiveRack to the Applications folder first, then open it from there";
   #endif
    return {};
}

bool ServerMode::startsAtLogin()
{
   #if JUCE_WINDOWS
    return juce::WindowsRegistry::valueExists (runKey);
   #else
    return loginFile().existsAsFile();
   #endif
}

void ServerMode::setStartAtLogin (bool on)
{
   #if JUCE_WINDOWS
    if (on) juce::WindowsRegistry::setValue (runKey, runValue());
    else    juce::WindowsRegistry::deleteValue (runKey);
   #else
    if (on)
    {
        loginFile().getParentDirectory().createDirectory();
        loginFile().replaceWithText (loginFileContent());
    }
    else
    {
        loginFile().deleteFile();
    }
   #endif
}

bool ServerMode::restartsAfterCrash()
{
    if (restartSetting < 0)
        restartSetting = AppSettings::get ("restartAfterCrash") == "1" ? 1 : 0;

    return restartSetting == 1;
}

void ServerMode::setRestartAfterCrash (bool on)
{
    if (on != restartsAfterCrash())
        AppSettings::set ("restartAfterCrash", on ? "1" : "0");

    restartSetting = on ? 1 : 0;

    if (on) startWatcher();
    else    stopWatcher();
}

void ServerMode::appStarted()
{
    wasRestartedAfterCrash();

    // The app may have been moved or updated since start-at-login was turned on
    if (startsAtLogin() && startAtLoginProblem().isEmpty())
        setStartAtLogin (true);

    if (restartsAfterCrash())
        startWatcher();
}

void ServerMode::appQuitting() { stopWatcher(); }

bool ServerMode::wasRestartedAfterCrash()
{
    static const bool restarted = [] { const bool was = restartMarker().existsAsFile(); restartMarker().deleteFile(); return was; }();
    return restarted;
}

std::unique_ptr<ServerMode::Watcher> ServerMode::startWatcherIfRequested (const juce::String& commandLine)
{
    auto watcher = std::make_unique<WatcherProcess>();

    if (watcher->initialiseFromCommandLine (commandLine, ServerMode::watcherUID))
        return watcher;

    return nullptr;
}
