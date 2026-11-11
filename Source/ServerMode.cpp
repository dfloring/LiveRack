#include "ServerMode.h"
#include <thread>
#include "AppSettings.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <csignal>
 #include <cerrno>
 #include <unistd.h>
#endif

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
    // Is a process still running? (Not a child of ours, so it is checked by id.)
    bool isProcessAlive (juce::uint32 pid)
    {
        if (pid == 0)
            return false;

       #if JUCE_WINDOWS
        auto h = OpenProcess (SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) pid);
        if (h == nullptr)
            return false;
        const bool running = WaitForSingleObject (h, 0) == WAIT_TIMEOUT;
        CloseHandle (h);
        return running;
       #else
        if (kill ((pid_t) pid, 0) != 0 && errno != EPERM)
            return false;
        #if JUCE_LINUX
        // A finished process nobody has collected yet still answers
        const auto stat = juce::File ("/proc/" + juce::String (pid) + "/stat").loadFileAsString();
        if (stat.fromLastOccurrenceOf (")", false, false).trimStart().startsWithChar ('Z'))
            return false;
        #endif
        return true;
       #endif
    }

    juce::uint32 thisProcessId()
    {
       #if JUCE_WINDOWS
        return (juce::uint32) GetCurrentProcessId();
       #else
        return (juce::uint32) getpid();
       #endif
    }

    // The watcher runs on its own, not as a child of LiveRack: when Windows
    // ends LiveRack together with everything it started, the watcher stays.
    // LiveRack starts a short-lived "spawner", which starts the watcher and
    // quits at once. The files below are how the two talk.
    juce::File tokenFile()      { return RackEngine::getSettingsDir().getChildFile ("watcher-token"); }   // the current watcher
    juce::File watcherPidFile() { return RackEngine::getSettingsDir().getChildFile ("watcher-pid"); }
    juce::File cleanQuitFile()  { return RackEngine::getSettingsDir().getChildFile ("clean-quit"); }      // pid of a proper quit

    constexpr const char* spawnFlag = "--liverackwatcherspawn";
    juce::String currentToken;
    int restartSetting = -1; // not read yet
    std::unique_ptr<juce::ChildProcess> spawner;

    void launchHidden (const juce::StringArray& args, std::unique_ptr<juce::ChildProcess>& holder)
    {
        juce::StringArray command;
        command.add (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName());
        command.addArray (args);
        holder = std::make_unique<juce::ChildProcess>();
        holder->start (command, 0);
    }

    void startWatcher()
    {
        currentToken = juce::String::toHexString (juce::Random::getSystemRandom().nextInt64());
        tokenFile().replaceWithText (currentToken);
        watcherPidFile().deleteFile();
        launchHidden ({ spawnFlag, currentToken, juce::String (thisProcessId()) }, spawner);
    }

    void stopWatcher()
    {
        currentToken = {};
        tokenFile().deleteFile(); // the watcher sees this and quits
    }

    // LiveRack checks now and then that its watcher is still there
    struct WatcherCheck final : private juce::Timer
    {
        WatcherCheck() { startTimer (5000); }

        void timerCallback() override
        {
            if (spawner != nullptr && ! spawner->isRunning()) // collects the finished spawner
                spawner.reset();

            if (currentToken.isEmpty() || ++ticks < 2)
                return;

            const auto pid = (juce::uint32) watcherPidFile().loadFileAsString().getLargeIntValue();
            if (! isProcessAlive (pid))
                startWatcher();
        }

        int ticks = 0;
    };

    std::unique_ptr<WatcherCheck> watcherCheck;

    //==========================================================================
    class WatcherProcess final : public ServerMode::Watcher,
                                 private juce::Timer
    {
    public:
        WatcherProcess (juce::String t, juce::uint32 app) : token (std::move (t)), appPid (app)
        {
            watcherPidFile().replaceWithText (juce::String (thisProcessId()));
            startTimer (500);
        }

    private:
        void timerCallback() override
        {
            // Turned off, or a newer watcher took over
            if (tokenFile().loadFileAsString().trim() != token)
                return finish();

            if (isProcessAlive (appPid))
                return;

            stopTimer();

            if ((juce::uint32) cleanQuitFile().loadFileAsString().getLargeIntValue() == appPid)
                return finish();

            juce::Timer::callAfterDelay (1500, [] { relaunch(); juce::JUCEApplicationBase::quit(); });
        }

        void finish()
        {
            stopTimer();
            juce::JUCEApplicationBase::quit();
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

        const juce::String token;
        const juce::uint32 appPid;
    };

    // Starts the watcher and quits
    struct Spawner final : public ServerMode::Watcher
    {
        Spawner (const juce::String& token, const juce::String& pid)
        {
            launchHidden ({ ServerMode::watcherUID, token, pid }, child);
            juce::MessageManager::callAsync ([] { juce::JUCEApplicationBase::quit(); });
        }

        std::unique_ptr<juce::ChildProcess> child;
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

    if (on)
    {
       #if JUCE_WINDOWS
        // Let a crash end LiveRack at once, instead of waiting behind a
        // "LiveRack has stopped working" box that nobody is there to close
        SetErrorMode (GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
       #endif
        startWatcher();
        if (watcherCheck == nullptr)
            watcherCheck = std::make_unique<WatcherCheck>();
    }
    else
    {
        stopWatcher();
        watcherCheck.reset();
    }
}

//==============================================================================
static juce::File startAppsFile() { return RackEngine::getSettingsDir().getChildFile ("start-apps.txt"); }

juce::StringArray ServerMode::getStartApps()
{
    juce::StringArray list;
    list.addLines (startAppsFile().loadFileAsString());
    list.trim();
    list.removeEmptyStrings();
    return list;
}

void ServerMode::setStartApps (const juce::StringArray& paths)
{
    startAppsFile().replaceWithText (paths.joinIntoString ("\n"));
}

static juce::StringArray runningProcesses()
{
    juce::ChildProcess p;
   #if JUCE_WINDOWS
    if (! p.start ("tasklist /FO CSV /NH", juce::ChildProcess::wantStdOut))
        return {};
   #else
    if (! p.start (juce::StringArray { "ps", "-axo", "args=" }, juce::ChildProcess::wantStdOut))
        return {};
   #endif
    juce::StringArray lines;
    lines.addLines (p.readAllProcessOutput());
    p.waitForProcessToFinish (3000);
    return lines;
}

bool ServerMode::isAppRunning (const juce::File& app)
{
    const auto lines = runningProcesses();
   #if JUCE_WINDOWS
    const auto exe = "\"" + app.getFileName().toLowerCase() + "\"";
    for (auto& l : lines)
        if (l.toLowerCase().startsWith (exe))
            return true;
   #elif JUCE_MAC
    // An .app bundle runs the binary inside it
    const auto inside = app.getFullPathName() + (app.getFileExtension() == ".app" ? "/Contents/MacOS/" : "");
    for (auto& l : lines)
        if (l.trim().startsWith (inside))
            return true;
   #else
    for (auto& l : lines)
        if (l.trim().startsWith (app.getFullPathName()))
            return true;
   #endif
    return false;
}

static void startAppsIfNeeded()
{
    const auto apps = ServerMode::getStartApps();
    if (apps.isEmpty())
        return;

    // Off the message thread: listing processes takes a moment
    std::thread ([apps]
    {
        for (auto& path : apps)
        {
            const juce::File app (path);
            if (app.exists() && ! ServerMode::isAppRunning (app))
                app.startAsProcess();
        }
    }).detach();
}

void ServerMode::appStarted()
{
    wasRestartedAfterCrash();
    startAppsIfNeeded();

    // The app may have been moved or updated since start-at-login was turned on
    if (startsAtLogin() && startAtLoginProblem().isEmpty())
        setStartAtLogin (true);

    cleanQuitFile().deleteFile();

    if (restartsAfterCrash())
        setRestartAfterCrash (true);
}

void ServerMode::appQuitting()
{
    watcherCheck.reset();
    cleanQuitFile().replaceWithText (juce::String (thisProcessId()));
    if (currentToken.isNotEmpty())
        tokenFile().deleteFile();
    currentToken = {};
}

bool ServerMode::wasRestartedAfterCrash()
{
    static const bool restarted = [] { const bool was = restartMarker().existsAsFile(); restartMarker().deleteFile(); return was; }();
    return restarted;
}

std::unique_ptr<ServerMode::Watcher> ServerMode::startWatcherIfRequested (const juce::String& commandLine)
{
    juce::StringArray args;
    args.addTokens (commandLine, true);
    args.removeEmptyStrings();

    const int spawn = args.indexOf (spawnFlag), watch = args.indexOf (ServerMode::watcherUID);

    if (spawn >= 0 && args.size() > spawn + 2)
        return std::make_unique<Spawner> (args[spawn + 1], args[spawn + 2]);

    if (watch >= 0 && args.size() > watch + 2)
        return std::make_unique<WatcherProcess> (args[watch + 1], (juce::uint32) args[watch + 2].getLargeIntValue());

    return nullptr;
}
