#include "LiveConnection.h"
#include "OscRemote.h"
#include "WebRemote.h"
#include "ServerMode.h"
#include "ServerCheck.h"
#include "SocketUtil.h"

// The parts of LocalConnection that reach beyond the engine: consoles,
// remotes and server settings, owned by the main window.

ConsoleLink* LocalConnection::console (int i) const
{
    if (services.consoles != nullptr && juce::isPositiveAndBelow (i, (int) services.consoles->size()))
        return (*services.consoles)[(size_t) i].get();
    return nullptr;
}

int LocalConnection::getNumConsoles()
{
    return services.consoles != nullptr ? (int) services.consoles->size() : 0;
}

LiveConsole LocalConnection::getConsole (int i)
{
    LiveConsole c;
    if (auto* l = console (i))
    {
        c.settings = l->getSettings();
        c.displayName = l->getDisplayName();
        c.statusText = l->getStatusText();
        c.connected = l->isConnected();
        c.learning = l->isLearning();
        c.currentScene = l->getCurrentScene();
        c.lastSelectedChannel = l->getLastSelectedChannel();
        if (c.lastSelectedChannel > 0 && services.rackForChannel != nullptr)
            c.selectedRack = services.rackForChannel (i, c.lastSelectedChannel);
        c.lastActivityMs = l->getLastActivityMs();
        c.logCounter = l->getLogCounter();
    }
    return c;
}

juce::StringArray LocalConnection::getConsoleLog (int i)
{
    auto* l = console (i);
    return l != nullptr ? l->getLog() : juce::StringArray();
}

void LocalConnection::setConsoleSettings (int i, const ConsoleLink::Settings& s)
{
    if (auto* l = console (i))
    {
        l->setSettings (s);
        if (services.consolesChanged != nullptr)
            services.consolesChanged();
    }
}

void LocalConnection::addConsole()
{
    if (services.addConsole != nullptr)
        services.addConsole();
}

void LocalConnection::removeConsole (int i)
{
    if (services.removeConsole != nullptr)
        services.removeConsole (i);
}

void LocalConnection::learnConsoleTrigger (int i)
{
    if (auto* l = console (i))
        l->startLearning();
}

//==============================================================================
template <typename Remote>
static LiveRemoteSettings remoteSettings (const Remote& r)
{
    LiveRemoteSettings s;
    s.enabled = r.isEnabled();
    s.listening = r.isListening();
    s.port = r.getPort();
    s.clients = r.getNumClients();
    s.networkId = r.getNetworkId();
    s.networkName = r.getNetworkName();
    s.statusText = r.getStatusText();
    s.lastActivityMs = r.getLastActivityMs();
    return s;
}

LiveRemoteSettings LocalConnection::getOscSettings()
{
    return services.osc != nullptr ? remoteSettings (*services.osc) : LiveRemoteSettings();
}

LiveRemoteSettings LocalConnection::getWebSettings()
{
    if (services.web == nullptr)
        return {};
    auto s = remoteSettings (*services.web);
    s.url = services.web->getUrl();
    return s;
}

void LocalConnection::setOscSettings (bool enabled, int port, const juce::String& id, const juce::String& name)
{
    if (auto* o = services.osc)
    {
        o->setNetwork (id, name);
        o->setEnabled (enabled, port);
        o->saveSettings();
    }
}

void LocalConnection::setWebSettings (bool enabled, int port, const juce::String& id, const juce::String& name)
{
    if (auto* w = services.web)
    {
        w->setNetwork (id, name);
        w->setEnabled (enabled, port);
        w->saveSettings();
    }
}

std::vector<LiveNetwork> LocalConnection::getNetworks()
{
    std::vector<LiveNetwork> list;
    for (auto& n : SocketUtil::getNetworks())
        list.push_back ({ n.id, n.name, n.describe() });
    return list;
}

LiveServerSettings LocalConnection::getServerSettings()
{
    LiveServerSettings s;
    s.startAtLogin = ServerMode::startsAtLogin();
    s.restartAfterCrash = ServerMode::restartsAfterCrash();
    s.multiCore = engine.isMultiCore();
    s.cores = RackEngine::getNumRackThreads();
    s.loginProblem = ServerMode::startAtLoginProblem();
    s.startApps = ServerMode::getStartApps();
    return s;
}

void LocalConnection::setServerSettings (bool login, bool restart, bool multiCore, const juce::StringArray& apps)
{
    if (login != ServerMode::startsAtLogin())          ServerMode::setStartAtLogin (login);
    if (restart != ServerMode::restartsAfterCrash())   ServerMode::setRestartAfterCrash (restart);
    if (multiCore != engine.isMultiCore())             engine.setMultiCore (multiCore);
    if (apps != ServerMode::getStartApps())            ServerMode::setStartApps (apps);
}

std::vector<LiveCheckItem> LocalConnection::getServerCheck()
{
    std::vector<LiveCheckItem> list;
    auto add = [&list] (const ServerCheck::Item& i)
    {
        list.push_back ({ (int) i.state, i.title, i.detail, i.fix != nullptr ? i.fixLabel : juce::String() });
    };
    for (auto& i : ServerCheck::getSystemItems())
        add (i);
    if (services.web != nullptr)
        for (auto& i : ServerCheck::getAppItems (engine, *services.web))
            add (i);
    return list;
}

void LocalConnection::fixServerCheck (const juce::String& title)
{
    auto items = ServerCheck::getSystemItems();
    if (services.web != nullptr)
        for (auto& i : ServerCheck::getAppItems (engine, *services.web))
            items.push_back (i);

    for (auto& i : items)
        if (i.title == title && i.fix != nullptr)
        {
            i.fix();
            break;
        }
}

void LocalConnection::recheckServer()
{
    ServerCheck::refreshSoon();
}

//==============================================================================
// Scanning for plugins, on a background thread. The scan itself runs in the
// scanner process (KnownPluginList's custom scanner), so a plugin that
// crashes while being scanned doesn't take LiveRack down; it's listed as failed.
class LocalConnection::ScanJob : private juce::Thread
{
public:
    ScanJob (RackEngine& e, bool all) : juce::Thread ("Plugin scan"), engine (e), rescanAll (all) { startThread(); }
    ~ScanJob() override { signalThreadShouldExit(); stopThread (30000); }

    LiveScanStatus status() const
    {
        const juce::ScopedLock sl (lock);
        LiveScanStatus s;
        s.running = isThreadRunning();
        s.progress = progress;
        s.current = current;
        s.message = message;
        return s;
    }

    bool isDone() const { return ! isThreadRunning(); }
    juce::String getMessage() const { const juce::ScopedLock sl (lock); return message; }
    void cancel() { signalThreadShouldExit(); }

private:
    void run() override
    {
        auto& formats = engine.getFormatManager();
        auto& list = engine.getKnownPlugins();
        const int before = list.getNumTypes();
        juce::StringArray failed;
        const int numFormats = juce::jmax (1, formats.getNumFormats());

        for (int f = 0; f < formats.getNumFormats() && ! threadShouldExit(); ++f)
        {
            auto* format = formats.getFormat (f);
            juce::PluginDirectoryScanner scanner (list, *format, format->getDefaultLocationsToSearch(), true,
                                                  engine.getDeadMansPedalFile(), false);
            juce::String name;
            for (;;)
            {
                if (threadShouldExit())
                    break;
                {
                    const juce::ScopedLock sl (lock);
                    current = scanner.getNextPluginFileThatWillBeScanned();
                    progress = ((float) f + juce::jlimit (0.0f, 1.0f, scanner.getProgress())) / (float) numFormats;
                }
                if (! scanner.scanNextFile (! rescanAll, name))
                    break;
            }
            failed.addArray (scanner.getFailedFiles());
        }

        const int added = list.getNumTypes() - before;
        const juce::ScopedLock sl (lock);
        progress = 1.0f;
        current.clear();
        message = threadShouldExit() ? juce::String ("Scan stopped. ") : juce::String ("Scan finished. ");
        if (added > 0)        message << added << (added == 1 ? " new plugin" : " new plugins");
        else if (added < 0)   message << "Plugin list updated";
        else                  message << "No new plugins";
        if (! failed.isEmpty())
            message << ", " << failed.size() << " failed to scan";
        message << ".";
    }

    RackEngine& engine;
    const bool rescanAll;
    juce::CriticalSection lock;
    float progress = 0.0f;
    juce::String current, message;
};

LocalConnection::LocalConnection (RackEngine& e) : engine (e) {}
LocalConnection::~LocalConnection() = default;

std::vector<LivePlugin> LocalConnection::getKnownPlugins()
{
    std::vector<LivePlugin> list;
    for (auto& d : engine.getKnownPlugins().getTypes())
        list.push_back ({ d.name, d.manufacturerName, d.pluginFormatName,
                          services.categoryFor != nullptr ? services.categoryFor (d) : d.category,
                          d.createIdentifierString(), d.isInstrument });
    std::sort (list.begin(), list.end(), [] (const LivePlugin& a, const LivePlugin& b)
               { return a.name.compareNatural (b.name) < 0; });
    return list;
}

juce::StringArray LocalConnection::getFailedPlugins()
{
    // What JUCE's own scan screen keeps as failed: files listed as blacklisted
    juce::StringArray failed;
    for (auto& f : engine.getKnownPlugins().getBlacklistedFiles())
        failed.add (f);
    return failed;
}

LiveScanStatus LocalConnection::getScanStatus()
{
    if (scan == nullptr)
    {
        LiveScanStatus s;
        s.message = lastScanMessage;
        return s;
    }
    auto s = scan->status();
    if (! s.running)
    {
        lastScanMessage = s.message;
        scan.reset();
    }
    return s;
}

void LocalConnection::startScan (bool rescanAll)
{
    if (scan != nullptr && ! scan->isDone())
        return;
    scan = std::make_unique<ScanJob> (engine, rescanAll);
}

void LocalConnection::cancelScan()
{
    if (scan != nullptr)
        scan->cancel();
}

void LocalConnection::forgetPlugin (const juce::String& identifier)
{
    auto& list = engine.getKnownPlugins();
    for (auto& d : list.getTypes())
        if (d.createIdentifierString() == identifier)
        {
            list.removeType (d);
            break;
        }
}

void LocalConnection::clearFailedPlugins()
{
    engine.getKnownPlugins().clearBlacklistedFiles();
}
