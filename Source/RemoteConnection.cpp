#include "RemoteConnection.h"
#include "WebSocketClient.h"

namespace
{
juce::var at (const juce::var& list, int index)
{
    if (auto* a = list.getArray(); a != nullptr && juce::isPositiveAndBelow (index, a->size()))
        return a->getReference (index);
    return {};
}

int sizeOf (const juce::var& list)
{
    auto* a = list.getArray();
    return a != nullptr ? a->size() : 0;
}
} // namespace

//==============================================================================
RemoteConnection::RemoteConnection (const juce::String& h, int p)
    : juce::Thread ("LiveRack connection"), host (h), port (p)
{
    startThread();
}

RemoteConnection::~RemoteConnection()
{
    alive->store (false);
    signalThreadShouldExit();
    stopThread (4000);
}

juce::String RemoteConnection::getServerName() const
{
    return serverName.isNotEmpty() ? serverName : host;
}

void RemoteConnection::run()
{
    while (! threadShouldExit())
    {
        connectAndRun();

        if (connected.exchange (false))
        {
            auto flag = alive;
            juce::MessageManager::callAsync ([this, flag] { if (flag->load()) sendChangeMessage(); });
        }

        // Try again shortly
        for (int i = 0; i < 20 && ! threadShouldExit(); ++i)
            wait (100);
    }
}

bool RemoteConnection::connectAndRun()
{
    juce::StreamingSocket socket;
    if (! socket.connect (host, port, 3000))
        return false;

    WebSocketClient ws { socket };
    if (! ws.handshake (host, port, "/live?live=1"))
        return false;

    connected = true;
    sendQuery();   // a snapshot being watched, again after reconnecting
    auto lastPing = juce::Time::getMillisecondCounter();
    juce::MemoryBlock message;

    while (! threadShouldExit())
    {
        const int ready = socket.waitUntilReady (true, 20);
        if (ready < 0)
            return false;

        for (int reads = 0; reads < 64 && socket.waitUntilReady (true, 0) > 0; ++reads)
        {
            const int type = ws.read (message);
            if (type < 0)
                return false;
            if (type == 1)
                received (message.toString());
        }

        // Commands from the screens
        juce::StringArray toSend;
        {
            const juce::ScopedLock sl (queueLock);
            toSend.swapWith (outgoing);
        }
        for (auto& m : toSend)
            if (! ws.sendText (m))
                return false;

        // The server lets a client go after 15 s without a word
        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastPing > 5000)
        {
            lastPing = now;
            if (! ws.sendText ("{\"t\":\"ping\"}"))
                return false;
        }
    }

    return true;
}

void RemoteConnection::received (const juce::String& text)
{
    auto message = juce::JSON::parse (text);
    const auto t = message["t"].toString();
    auto flag = alive;

    if (t == "state")
    {
        juce::MessageManager::callAsync ([this, flag, data = message["data"]]
        {
            if (! flag->load())
                return;
            state = data;
            sendChangeMessage();
        });
    }
    else if (t == "meters")
    {
        juce::MessageManager::callAsync ([this, flag, data = message["data"]]
        {
            if (flag->load())
                meters = data;
        });
    }
    else if (t == "reply" && message["what"].toString() == "devices")
    {
        juce::MessageManager::callAsync ([this, flag, data = message["data"]["devices"], nets = message["data"]["networks"]]
        {
            if (! flag->load())
                return;
            deviceOptions = data;
            networks = nets;
            sendChangeMessage();
        });
    }
    else if (t == "reply" && message["what"].toString() == "plugins")
    {
        juce::MessageManager::callAsync ([this, flag, data = message["data"]]
        {
            if (! flag->load())
                return;
            pluginList = data;
            sendChangeMessage();
        });
    }
    else if (t == "hello")
    {
        juce::MessageManager::callAsync ([this, flag, name = message["server"].toString(), version = message["version"].toString()]
        {
            if (! flag->load())
                return;
            serverName = name;
            serverVersion = version;
            everConnected = true;
            sendChangeMessage();
        });
    }

    // A reply to request()
    if (t == "reply" && (int) message["id"] >= 100)
    {
        juce::MessageManager::callAsync ([this, flag, id = (int) message["id"], data = message["data"]]
        {
            if (! flag->load())
                return;
            const auto it = pendingReplies.find (id);
            if (it == pendingReplies.end())
                return;
            auto callback = std::move (it->second);
            pendingReplies.erase (it);
            if (callback != nullptr)
                callback (data);
        });
    }
}

// What the state includes besides the basics: the live extras, and one snapshot in detail
void RemoteConnection::sendQuery()
{
    juce::String q ("live=1");
    if (const int n = watched.load(); n != std::numeric_limits<int>::min())
        q << "&snapshot=" << n;
    if (const int c = watchedLog.load(); c >= 0)
        q << "&consoleLog=" << c;

    const juce::ScopedLock sl (queueLock);
    outgoing.add ("{\"t\":\"query\",\"q\":\"" + q + "\"}");
}

void RemoteConnection::request (const juce::String& what, std::function<void (const juce::var&)> onReply)
{
    const int id = nextRequestId++;
    pendingReplies[id] = std::move (onReply);

    auto* o = new juce::DynamicObject();
    o->setProperty ("t", "get");
    o->setProperty ("what", what);
    o->setProperty ("id", id);
    const juce::ScopedLock sl (queueLock);
    outgoing.add (juce::JSON::toString (juce::var (o), true));
}

void RemoteConnection::sendCommand (const juce::var& command)
{
    auto copy = command.clone();
    if (auto* o = copy.getDynamicObject())
        o->setProperty ("t", "cmd");

    const juce::ScopedLock sl (queueLock);
    outgoing.add (juce::JSON::toString (copy, true));
}

//==============================================================================
// Reading the state
juce::var RemoteConnection::rackState (int rack) const      { return at (state["racks"], rack); }
juce::var RemoteConnection::slotState (int rack, int s) const { return at (rackState (rack)["slots"], s); }
juce::var RemoteConnection::rackInfo (int rack) const       { return at (state["rackInfo"], rack); }
juce::var RemoteConnection::rackMeters (int rack) const     { return at (meters["racks"], rack); }

int RemoteConnection::getNumRacks()  { return sizeOf (state["racks"]); }
int RemoteConnection::getNumDrivers() { return juce::jmax (1, sizeOf (state["drivers"])); }

RackConfig RemoteConnection::getRackConfig (int rack)
{
    RackConfig cfg;
    const auto r = rackState (rack);
    if (! r.isObject())
        return cfg;

    cfg.name = r["name"].toString();
    cfg.stereo = (bool) r["stereo"];
    cfg.monoToStereo = (bool) r["m2s"];
    cfg.driver = (int) r["driver"];
    for (int ch = 0; ch < 2; ++ch)
    {
        cfg.input[ch] = (int) at (r["in"], ch);
        cfg.output[ch] = (int) at (r["out"], ch);
    }
    cfg.consoleChannel = (int) r["console"];
    cfg.console = (int) r["consoleIndex"];
    cfg.recallSafe = (bool) r["safe"];
    cfg.colourIndex = r.hasProperty ("colourIndex") ? (int) r["colourIndex"] : -1;
    cfg.id = (int) r["id"];
    cfg.latencyGroup = (int) rackInfo (rack)["group"];
    return cfg;
}

bool RemoteConnection::isRackBypassed (int rack)      { return (bool) rackState (rack)["bypass"]; }
int RemoteConnection::getNumSlots (int rack)          { return sizeOf (rackState (rack)["slots"]); }

LiveSlot RemoteConnection::getSlot (int rack, int slot)
{
    LiveSlot s;
    const auto v = slotState (rack, slot);
    if (v.isObject())
    {
        s.exists = true;
        s.id = (int) v["id"];
        s.name = v["name"].toString();
        s.key = v["key"].toString();
        s.bypassed = (bool) v["bypass"];
        s.isolated = (bool) at (rackInfo (rack)["isolated"], slot);
    }
    return s;
}

int RemoteConnection::getStereoPoint (int rack)
{
    for (int s = 0; s < getNumSlots (rack); ++s)
        if ((bool) slotState (rack, s)["sp"])
            return s;
    return -1;
}

double RemoteConnection::getRackTotalLatencyMs (int rack) { return (double) rackInfo (rack)["latencyMs"]; }
double RemoteConnection::getGroupLatencyMs (int group)    { return (double) at (state["groupLatencyMs"], group); }

std::vector<int> RemoteConnection::getVisibleRacks()
{
    std::vector<int> racks;
    if (auto* order = state["order"].getArray())
        for (auto& r : *order)
            racks.push_back ((int) r);
    return racks;
}

float RemoteConnection::getRackLoad (int rack)            { return (float) (double) rackMeters (rack)["load"]; }
float RemoteConnection::getSlotLoad (int rack, int slot)  { return (float) (double) at (rackMeters (rack)["loads"], slot); }
float RemoteConnection::getMeter (int rack, bool output)  { return (float) (double) rackMeters (rack)[output ? "out" : "in"]; }

float RemoteConnection::getGainReduction (int rack, int slot)
{
    const auto v = at (rackMeters (rack)["gr"], slot);
    return v.isVoid() ? -1.0f : (float) (double) v;
}

bool RemoteConnection::isGainReductionEstimated (int rack, int slot)
{
    return (bool) at (rackMeters (rack)["est"], slot);
}

juce::PluginDescription RemoteConnection::getSlotDescription (int rack, int slot)
{
    juce::PluginDescription d;
    d.name = getSlot (rack, slot).name;
    return d;
}

//==============================================================================
// Changes: commands to the server; the new state follows from it
namespace
{
juce::var command (const char* name, int rack)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("cmd", name);
    o->setProperty ("rack", rack);
    return juce::var (o);
}
}

void RemoteConnection::setRackBypassed (int rack, bool b)
{
    auto c = command ("rackBypass", rack);
    c.getDynamicObject()->setProperty ("value", b ? 1 : 0);
    sendCommand (c);
}

void RemoteConnection::setRackRecallSafe (int rack, bool b)
{
    auto c = command ("rackConfig", rack);
    c.getDynamicObject()->setProperty ("safe", b);
    sendCommand (c);
}

void RemoteConnection::setRackLatencyGroup (int rack, int group)
{
    auto c = command ("latencyGroup", rack);
    c.getDynamicObject()->setProperty ("group", group);
    sendCommand (c);
}

void RemoteConnection::setRackColour (int rack, int colourIndex)
{
    auto c = command ("rackConfig", rack);
    c.getDynamicObject()->setProperty ("colour", colourIndex);
    sendCommand (c);
}

void RemoteConnection::setRackName (int rack, const juce::String& name)
{
    auto c = command ("rackConfig", rack);
    c.getDynamicObject()->setProperty ("name", name);
    sendCommand (c);
}

void RemoteConnection::setSlotBypassed (int rack, int slot, bool b)
{
    auto c = command ("slotBypass", rack);
    c.getDynamicObject()->setProperty ("slot", slot);
    c.getDynamicObject()->setProperty ("value", b ? 1 : 0);
    sendCommand (c);
}

//==============================================================================
// Snapshots
namespace
{
juce::var make (const char* name, std::initializer_list<std::pair<const char*, juce::var>> props)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("cmd", name);
    for (auto& p : props)
        o->setProperty (p.first, p.second);
    return juce::var (o);
}
}

std::vector<LiveConnection::Snapshot> RemoteConnection::getSnapshots()
{
    std::vector<Snapshot> list;
    if (auto* a = state["snapshots"].getArray())
        for (auto& v : *a)
        {
            Snapshot s;
            s.number = (int) v["number"];
            s.name = v["name"].toString();
            if (const auto stored = v["stored"].toString(); stored.isNotEmpty())
                s.stored = juce::Time::fromISO8601 (stored);
            s.consoleScene = v["consoleScene"].toString();
            s.midi = v["midi"].toString();
            list.push_back (s);
        }
    return list;
}

void RemoteConnection::watchSnapshot (int number)
{
    if (watched.exchange (number) != number)
        sendQuery();
}

juce::var RemoteConnection::watchedSnapshot (int number) const
{
    const auto v = state["snapshot"];
    return v.isObject() && (int) v["number"] == number ? v : juce::var();
}

SnapshotScope RemoteConnection::getScopeFor (int number)
{
    SnapshotScope sc;
    const auto v = watchedSnapshot (number)["scope"];
    if (v.isObject())
    {
        sc.pluginSettings = (bool) v["pluginSettings"];
        sc.pluginBypass = (bool) v["pluginBypass"];
        sc.rackBypass = (bool) v["rackBypass"];
        sc.bpm = (bool) v["bpm"];
    }
    return sc;
}

bool RemoteConnection::isSlotInScopeFor (int number, int rack, int slot)
{
    return (int) at (at (watchedSnapshot (number)["inScope"], rack), slot) == 1;
}

bool RemoteConnection::snapshotHasSlot (int number, int rack, int slot)
{
    const auto v = at (at (watchedSnapshot (number)["inScope"], rack), slot);
    return ! v.isVoid() && (int) v != -1;
}

std::vector<SnapshotDifference> RemoteConnection::differencesFromNow (int number)
{
    std::vector<SnapshotDifference> list;
    if (auto* a = watchedSnapshot (number)["differences"].getArray())
        for (auto& v : *a)
        {
            SnapshotDifference d;
            d.rack = (int) v["rack"];
            d.slot = (int) v["slot"];
            d.changedSettings = (int) v["settings"];
            d.bypassDiffers = (bool) v["bypass"];
            if (auto* details = v["details"].getArray())
                for (auto& t : *details)
                    d.details.add (t.toString());
            list.push_back (d);
        }
    return list;
}

void RemoteConnection::setScopeFor (int number, const SnapshotScope& sc)
{
    sendCommand (make ("sceneScope", { { "number", number }, { "pluginSettings", sc.pluginSettings }, { "pluginBypass", sc.pluginBypass },
                                       { "rackBypass", sc.rackBypass }, { "bpm", sc.bpm } }));
}

void RemoteConnection::setSlotInScopeFor (int number, int rack, int slot, bool in)
{
    sendCommand (make ("sceneScope", { { "number", number }, { "rack", rack }, { "slot", slot }, { "in", in } }));
}

void RemoteConnection::renameSnapshot (int number, const juce::String& name)
{
    sendCommand (make ("sceneRename", { { "number", number }, { "name", name } }));
}

void RemoteConnection::deleteSnapshot (int number)
{
    sendCommand (make ("sceneDelete", { { "number", number } }));
}

bool RemoteConnection::duplicateSnapshot (int from, int to)
{
    if (hasSnapshot (to))
        return false;
    sendCommand (make ("sceneDuplicate", { { "number", from }, { "to", to } }));
    return true;
}

bool RemoteConnection::renumberSnapshot (int from, int to)
{
    if (hasSnapshot (to))
        return false;
    sendCommand (make ("sceneRenumber", { { "number", from }, { "to", to } }));
    return true;
}

void RemoteConnection::setSnapshotConsoleScene (int number, const juce::String& scene)
{
    sendCommand (make ("sceneLink", { { "number", number }, { "scene", scene } }));
}

MidiControl::SnapshotLink RemoteConnection::getSnapshotLink()
{
    MidiControl::SnapshotLink link;
    const auto v = state["midiSnapshots"];
    if (v.isObject())
    {
        link.follow = (bool) v["follow"];
        link.input = v["input"].toString();
        link.channel = (int) v["channel"];
        link.firstSnapshot = (int) v["first"];
        link.send = (bool) v["send"];
        link.output = v["output"].toString();
        link.outChannel = (int) v["outChannel"];
    }
    return link;
}

void RemoteConnection::setSnapshotLink (const MidiControl::SnapshotLink& l)
{
    sendCommand (make ("midiSnapshots", { { "follow", l.follow }, { "input", l.input }, { "channel", l.channel }, { "first", l.firstSnapshot },
                                          { "send", l.send }, { "output", l.output }, { "outChannel", l.outChannel } }));
}

juce::StringArray RemoteConnection::getMidiInputNames()
{
    juce::StringArray names;
    if (auto* a = state["midiSnapshots"]["inputs"].getArray())
        for (auto& n : *a)
            names.add (n.toString());
    return names;
}

juce::StringArray RemoteConnection::getMidiOutputNames()
{
    juce::StringArray names;
    if (auto* a = state["midiSnapshots"]["outputs"].getArray())
        for (auto& n : *a)
            names.add (n.toString());
    return names;
}

//==============================================================================
// Routing
LiveDriver RemoteConnection::getDriver (int driver)
{
    LiveDriver d;
    const auto v = at (state["drivers"], driver);
    d.label = v.isObject() ? v["label"].toString() : "Driver " + juce::String (driver + 1);
    d.open = (bool) v["open"];
    d.numIns = (int) v["numIns"];
    d.numOuts = (int) v["numOuts"];
    auto names = [] (const juce::var& list) { juce::StringArray a; if (auto* arr = list.getArray()) for (auto& n : *arr) a.add (n.toString()); return a; };
    d.inNames = names (v["ins"]);
    d.outNames = names (v["outs"]);
    d.blockLabels = names (v["blocks"]);
    return d;
}

void RemoteConnection::setRackRouting (int rack, int driver, const int inputs[2], const int outputs[2])
{
    auto c = command ("rackConfig", rack);
    c.getDynamicObject()->setProperty ("driver", driver);
    c.getDynamicObject()->setProperty ("in", juce::Array<juce::var> { inputs[0], inputs[1] });
    c.getDynamicObject()->setProperty ("out", juce::Array<juce::var> { outputs[0], outputs[1] });
    sendCommand (c);
}

//==============================================================================
// Audio drivers
DriverStatus RemoteConnection::getDriverStatus (int driver)
{
    DriverStatus st;
    const auto v = at (state["drivers"], driver);
    if (! v.isObject())
        return st;

    st.open = (bool) v["open"];
    st.typeName = v["type"].toString();
    st.deviceName = v["device"].toString();
    st.sampleRate = (double) v["rate"];
    st.bufferSize = (int) v["buffer"];
    st.numIns = (int) v["numIns"];
    st.numOuts = (int) v["numOuts"];
    st.ioLatency = (int) v["ioLatency"];
    st.dropouts = (int) v["dropouts"];
    if (const auto last = v["lastDropout"].toString(); last.isNotEmpty())
        st.lastDropout = juce::Time::fromISO8601 (last);
    st.threads = juce::jmax (1, (int) v["threads"]);
    st.numRacks = (int) v["racks"];

    const auto m = at (meters["drivers"], driver);
    st.cpu = (double) m["dsp"];
    st.worstBlock = (double) m["longest"];
    return st;
}

std::vector<RackEngine::HeavyPlugin> RemoteConnection::getHeaviestPlugins (int driver, int count)
{
    std::vector<RackEngine::HeavyPlugin> list;
    for (int r = 0; r < getNumRacks(); ++r)
    {
        if (driver >= 0 && (int) rackState (r)["driver"] != driver)
            continue;
        for (int s = 0; s < getNumSlots (r); ++s)
            list.push_back ({ r, s, getSlotLoad (r, s) });
    }
    std::sort (list.begin(), list.end(), [] (auto& a, auto& b) { return a.load > b.load; });
    if ((int) list.size() > count)
        list.resize ((size_t) count);
    return list;
}

bool RemoteConnection::addDriver()
{
    if (getNumDrivers() >= RackEngine::maxDrivers)
        return false;
    sendCommand (make ("driverAdd", {}));
    return true;
}

void RemoteConnection::removeDriver (int driver)
{
    sendCommand (make ("driverRemove", { { "driver", driver } }));
}

void RemoteConnection::refreshDriverOptions()
{
    const juce::ScopedLock sl (queueLock);
    outgoing.add ("{\"t\":\"get\",\"what\":\"devices\",\"id\":1}");
}

LiveDriverOptions RemoteConnection::getDriverOptions (int driver)
{
    LiveDriverOptions o;
    const auto v = at (deviceOptions, driver);
    if (! v.isObject())
        return o;

    o.known = true;
    o.type = v["type"].toString();
    o.device = v["device"].toString();
    if (auto* types = v["types"].getArray())
        for (auto& t : *types)
        {
            LiveDriverOptions::Type type { t["name"].toString(), {} };
            if (auto* names = t["devices"].getArray())
                for (auto& n : *names)
                    type.devices.add (n.toString());
            o.types.push_back (type);
        }
    if (auto* rates = v["rates"].getArray())
        for (auto& r : *rates) o.rates.add ((double) r);
    if (auto* buffers = v["buffers"].getArray())
        for (auto& b : *buffers) o.buffers.add ((int) b);
    o.rate = (double) v["rate"];
    o.buffer = (int) v["buffer"];
    return o;
}

juce::String RemoteConnection::setDriver (int driver, const juce::String& type, const juce::String& device, double rate, int buffer)
{
    auto c = make ("driverSet", { { "driver", driver } });
    auto* o = c.getDynamicObject();
    if (type.isNotEmpty())   o->setProperty ("type", type);
    if (device.isNotEmpty()) o->setProperty ("device", device);
    if (rate > 0)            o->setProperty ("rate", rate);
    if (buffer > 0)          o->setProperty ("buffer", buffer);
    sendCommand (c);
    return {};   // the server's status line says how it went
}

//==============================================================================
// Consoles, remotes and server settings
juce::uint32 RemoteConnection::activityFromAge (const juce::var& ageMs) const
{
    if (ageMs.isVoid() || (int) ageMs < 0)
        return 0;
    return juce::jmax<juce::uint32> (1, juce::Time::getMillisecondCounter() - (juce::uint32) (int) ageMs);
}

int RemoteConnection::getNumConsoles() { return sizeOf (state["consoleLinks"]); }

LiveConsole RemoteConnection::getConsole (int i)
{
    LiveConsole c;
    const auto v = at (state["consoleLinks"], i);
    if (! v.isObject())
        return c;

    c.settings = ConsoleLink::settingsFromVar (v["settings"]);
    c.displayName = v["displayName"].toString();
    c.statusText = v["status"].toString();
    c.connected = (bool) v["connected"];
    c.learning = (bool) v["learning"];
    c.currentScene = (int) v["scene"];
    c.lastSelectedChannel = (int) v["selected"];
    c.selectedRack = v["selectedRack"].toString();
    c.logCounter = (int) v["logCounter"];
    c.lastActivityMs = activityFromAge (at (meters["consoleAges"], i));
    return c;
}

juce::StringArray RemoteConnection::getConsoleLog (int i)
{
    juce::StringArray lines;
    const auto v = state["consoleLog"];
    if ((int) v["console"] == i)
        if (auto* a = v["lines"].getArray())
            for (auto& l : *a)
                lines.add (l.toString());
    return lines;
}

void RemoteConnection::watchConsoleLog (int i)
{
    if (watchedLog.exchange (i) != i)
        sendQuery();
}

void RemoteConnection::setConsoleSettings (int i, const ConsoleLink::Settings& s)
{
    sendCommand (make ("consoleSet", { { "console", i }, { "settings", ConsoleLink::settingsToVar (s) } }));
}

void RemoteConnection::addConsole()                 { sendCommand (make ("consoleAdd", {})); }
void RemoteConnection::removeConsole (int i)        { sendCommand (make ("consoleRemove", { { "console", i } })); }
void RemoteConnection::learnConsoleTrigger (int i)  { sendCommand (make ("consoleLearn", { { "console", i } })); }

LiveRemoteSettings RemoteConnection::remoteFrom (const juce::var& v, const juce::var& ageMs) const
{
    LiveRemoteSettings s;
    s.enabled = (bool) v["enabled"];
    s.listening = (bool) v["listening"];
    s.port = (int) v["port"];
    s.clients = (int) v["clients"];
    s.networkId = v["network"].toString();
    s.networkName = v["networkName"].toString();
    s.statusText = v["status"].toString();
    s.url = v["url"].toString();
    s.lastActivityMs = activityFromAge (ageMs);
    return s;
}

LiveRemoteSettings RemoteConnection::getOscSettings() { return remoteFrom (state["osc"], meters["oscAge"]); }
LiveRemoteSettings RemoteConnection::getWebSettings() { return remoteFrom (state["web"], meters["webAge"]); }

void RemoteConnection::setOscSettings (bool on, int port, const juce::String& id, const juce::String& name)
{
    sendCommand (make ("oscSet", { { "enabled", on }, { "port", port }, { "network", id }, { "networkName", name } }));
}

void RemoteConnection::setWebSettings (bool on, int port, const juce::String& id, const juce::String& name)
{
    sendCommand (make ("webSet", { { "enabled", on }, { "port", port }, { "network", id }, { "networkName", name } }));
}

std::vector<LiveNetwork> RemoteConnection::getNetworks()
{
    std::vector<LiveNetwork> list;
    if (auto* a = networks.getArray())
        for (auto& n : *a)
            list.push_back ({ n["id"].toString(), n["name"].toString(), n["label"].toString() });
    return list;
}

LiveServerSettings RemoteConnection::getServerSettings()
{
    LiveServerSettings s;
    const auto v = state["server"];
    s.startAtLogin = (bool) v["startAtLogin"];
    s.restartAfterCrash = (bool) v["restartAfterCrash"];
    s.multiCore = (bool) v["multiCore"];
    s.cores = juce::jmax (1, (int) v["cores"]);
    s.loginProblem = state["serverSetup"]["loginProblem"].toString();
    if (auto* a = state["serverSetup"]["startApps"].getArray())
        for (auto& p : *a)
            s.startApps.add (p.toString());
    return s;
}

void RemoteConnection::setServerSettings (bool login, bool restart, bool multiCore, const juce::StringArray& apps)
{
    juce::Array<juce::var> list;
    for (auto& a : apps)
        list.add (a);
    sendCommand (make ("server", { { "startAtLogin", login }, { "restartAfterCrash", restart }, { "multiCore", multiCore }, { "startApps", list } }));
}

std::vector<LiveCheckItem> RemoteConnection::getServerCheck()
{
    std::vector<LiveCheckItem> list;
    if (auto* a = state["serverCheck"].getArray())
        for (auto& i : *a)
            list.push_back ({ (int) i["state"], i["title"].toString(), i["detail"].toString(), i["fix"].toString() });
    return list;
}

void RemoteConnection::fixServerCheck (const juce::String& title) { sendCommand (make ("serverFix", { { "title", title } })); }
void RemoteConnection::recheckServer()                            { sendCommand (make ("serverRecheck", {})); }

//==============================================================================
// Pages, Hot Plugins and plugins
std::vector<RackPage> RemoteConnection::getPages()
{
    std::vector<RackPage> pages;
    if (auto* list = state["pageRacks"].getArray())
        for (auto& p : *list)
        {
            RackPage page;
            page.name = p["name"].toString();
            if (auto* ids = p["racks"].getArray())
                for (auto& id : *ids)
                    page.rackIds.push_back ((int) id);
            pages.push_back (page);
        }
    return pages;
}

static juce::var pagesToVar (const std::vector<RackPage>& pages)
{
    juce::Array<juce::var> list;
    for (auto& p : pages)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", p.name);
        juce::Array<juce::var> ids;
        for (int id : p.rackIds) ids.add (id);
        o->setProperty ("racks", ids);
        list.add (juce::var (o));
    }
    return list;
}

void RemoteConnection::setPages (std::vector<RackPage> pages)
{
    sendCommand (make ("pagesSet", { { "pages", pagesToVar (pages) } }));
}

int RemoteConnection::getCurrentPage()        { return state.hasProperty ("page") ? (int) state["page"] : -1; }
void RemoteConnection::setCurrentPage (int p) { sendCommand (make ("page", { { "page", p } })); }

std::vector<int> RemoteConnection::getHotPlugins()
{
    std::vector<int> ids;
    if (auto* list = state["hotIds"].getArray())
        for (auto& id : *list)
            ids.push_back ((int) id);
    return ids;
}

void RemoteConnection::setHotPlugins (std::vector<int> ids)
{
    juce::Array<juce::var> list;
    for (int id : ids) list.add (id);
    sendCommand (make ("hotSet", { { "ids", list } }));
}

void RemoteConnection::refreshKnownPlugins()
{
    const juce::ScopedLock sl (queueLock);
    outgoing.add ("{\"t\":\"get\",\"what\":\"plugins\",\"id\":2}");
}

std::vector<LivePlugin> RemoteConnection::getKnownPlugins()
{
    std::vector<LivePlugin> list;
    if (auto* a = pluginList["plugins"].getArray())
        for (auto& p : *a)
            list.push_back ({ p["name"].toString(), p["maker"].toString(), p["format"].toString(),
                              p["category"].toString(), p["id"].toString(), (bool) p["instrument"] });
    return list;
}

juce::StringArray RemoteConnection::getFailedPlugins()
{
    juce::StringArray failed;
    if (auto* a = pluginList["failedPlugins"].getArray())
        for (auto& f : *a)
            failed.add (f.toString());
    return failed;
}

LiveScanStatus RemoteConnection::getScanStatus()
{
    LiveScanStatus s;
    const auto v = state["scan"];
    s.running = (bool) v["running"];
    s.progress = (float) (double) v["progress"];
    s.current = v["current"].toString();
    s.message = v["message"].toString();
    return s;
}

void RemoteConnection::startScan (bool all)                         { sendCommand (make ("scanStart", { { "all", all } })); }
void RemoteConnection::cancelScan()                                 { sendCommand (make ("scanCancel", {})); }
void RemoteConnection::forgetPlugin (const juce::String& id)        { sendCommand (make ("pluginForget", { { "id", id } })); refreshKnownPlugins(); }
void RemoteConnection::clearFailedPlugins()                         { sendCommand (make ("failedClear", {})); refreshKnownPlugins(); }
