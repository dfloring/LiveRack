#include "ConsoleLink.h"

//==============================================================================
// Yamaha RCP: plain text lines over TCP 49280. Runs on its own thread and
// hands every received line to the message thread through post().
class ConsoleLink::RcpThread : public juce::Thread
{
public:
    RcpThread (ConsoleLink& o, juce::String h)
        : juce::Thread ("Yamaha RCP"), owner (o), host (std::move (h)) {}

    ~RcpThread() override { stopThread (4000); }

    void run() override
    {
        while (! threadShouldExit())
        {
            juce::StreamingSocket socket;

            if (! socket.connect (host, 49280, 2000))
            {
                owner.post ("#disconnected");
                wait (3000);
                continue;
            }

            owner.post ("#connected");
            send (socket, "sscurrent_ex MIXER:Lib/Scene"); // ask for the current scene
            auto lastSend = juce::Time::getMillisecondCounter();

            juce::String pending;
            char buffer[2048];

            while (! threadShouldExit())
            {
                const int ready = socket.waitUntilReady (true, 500);

                if (ready < 0)
                    break;

                // Poll now and then so a dead connection is noticed
                if (juce::Time::getMillisecondCounter() - lastSend > 20000)
                {
                    send (socket, "sscurrent_ex MIXER:Lib/Scene");
                    lastSend = juce::Time::getMillisecondCounter();
                }

                if (ready == 0)
                    continue;

                const int n = socket.read (buffer, (int) sizeof (buffer), false);

                if (n <= 0)
                    break; // console closed the connection

                pending += juce::String::fromUTF8 (buffer, n);

                for (int nl = pending.indexOfChar ('\n'); nl >= 0; nl = pending.indexOfChar ('\n'))
                {
                    owner.post (pending.substring (0, nl).trim());
                    pending = pending.substring (nl + 1);
                }
            }

            owner.post ("#disconnected");

            if (! threadShouldExit())
                wait (2000);
        }
    }

private:
    static void send (juce::StreamingSocket& s, const juce::String& line)
    {
        const auto text = line + "\n";
        s.write (text.toRawUTF8(), (int) text.getNumBytesAsUTF8());
    }

    ConsoleLink& owner;
    juce::String host;
};

//==============================================================================
ConsoleLink::ConsoleLink()  { startTimer (50); }
ConsoleLink::~ConsoleLink() { stopTimer(); stop(); }

juce::String ConsoleLink::x32FollowName (int mode)
{
    switch (mode)
    {
        case 1:  return "Snippets";
        case 2:  return "Cues";
        default: break;
    }
    return "Scenes";
}

void ConsoleLink::startLearning()
{
    learning = true;
    learnStartedMs = juce::Time::getMillisecondCounter();
    addLog ("Learning: recall a " + x32FollowName (settings.x32Follow).toLowerCase().dropLastCharacters (1)
            + " on the X32 now (30 s)...");
}

juce::String ConsoleLink::typeName (Type t)
{
    switch (t)
    {
        case Type::yamahaRcp:    return "Yamaha CL/QL";
        case Type::behringerX32: return "Behringer X32";
        case Type::off:          break;
    }
    return "Off";
}

void ConsoleLink::setSettings (const Settings& s)
{
    stop();
    settings = s;
    if (settings.slots.empty())
        settings.slots = defaultSlots (settings.type);
    start();
}

void ConsoleLink::start()
{
    connected = false;
    x32Primed = false;
    currentScene = -1;

    const auto host = settings.address.trim();

    if (settings.type == Type::off || host.isEmpty())
        return;

    if (settings.type == Type::yamahaRcp)
    {
        addLog ("Connecting to Yamaha console at " + host + ":49280");
        rcp = std::make_unique<RcpThread> (*this, host);
        rcp->startThread();
    }
    else if (settings.type == Type::behringerX32)
    {
        // One socket for both directions: the X32 replies to the port we send from
        oscSocket = std::make_unique<juce::DatagramSocket> (false);

        if (! oscSocket->bindToPort (0))
        {
            addLog ("Couldn't open a UDP port");
            oscSocket.reset();
            return;
        }

        oscReceiver.addListener (this);
        oscReceiver.connectToSocket (*oscSocket);
        oscSender.connectToSocket (*oscSocket, host, 10023);

        addLog ("Talking to X32 at " + host + ":10023");
        sendX32 ("/xremote");
        sendX32 ("/-show/prepos/current");
        sendX32 ("/-stat/selidx");
    }
}

void ConsoleLink::stop()
{
    rcp.reset();

    oscReceiver.removeListener (this);
    oscReceiver.disconnect();
    oscSender.disconnect();
    oscSocket.reset();

    connected = false;
}

juce::String ConsoleLink::getStatusText() const
{
    if (settings.type == Type::off)
        return "Console link off";

    juce::String s = typeName (settings.type) + " " + settings.address.trim()
                   + (connected ? ": connected" : ": not connected");

    if (currentScene >= 0)
        s << ", scene " << currentScene;

    return s;
}

//==============================================================================
void ConsoleLink::post (const juce::String& line)
{
    const juce::ScopedLock sl (queueLock);
    incoming.add (line);
}

void ConsoleLink::timerCallback()
{
    juce::StringArray lines;

    {
        const juce::ScopedLock sl (queueLock);
        lines.swapWith (incoming);
    }

    for (auto& line : lines)
        if (line.isNotEmpty() && ! line.startsWithChar ('#'))
            lastActivityMs = juce::Time::getMillisecondCounter();

    for (auto& line : lines)
    {
        if (line == "#connected")
        {
            connected = true;
            addLog ("Connected");
        }
        else if (line == "#disconnected")
        {
            if (connected)
                addLog ("Disconnected, retrying");
            connected = false;
        }
        else if (line.isNotEmpty())
        {
            handleRcpLine (line);
        }
    }

    if (learning && juce::Time::getMillisecondCounter() - learnStartedMs > 30000)
    {
        learning = false;
        addLog ("Learning timed out: no recall message seen. Check the log for what the X32 sent.");
    }

    if (settings.type == Type::behringerX32 && oscSocket != nullptr)
    {
        // The X32 stops sending updates unless /xremote is renewed within 10 s
        if (++ticks % 160 == 0)
            sendX32 ("/xremote");

        connected = lastOscReceivedMs != 0
                 && juce::Time::getMillisecondCounter() - lastOscReceivedMs < 15000;
    }
}

//==============================================================================
void ConsoleLink::handleRcpLine (const juce::String& line)
{
    addLog (line);

    juce::StringArray t;
    t.addTokens (line, " ", "\"");

    // NOTIFY sscurrent_ex MIXER:Lib/Scene 12   (a recall happened)
    // OK sscurrent_ex MIXER:Lib/Scene 12 ...   (answer to our query)
    if (t.size() >= 4 && t[2] == "MIXER:Lib/Scene"
        && (t[1] == "sscurrent_ex" || t[1] == "ssrecall_ex"))
    {
        const int scene = t[3].getIntValue();

        if (t[0] == "NOTIFY")
            sceneChanged (scene);
        else if (t[0] == "OK")
            currentScene = scene;
    }
}

void ConsoleLink::sendX32 (const juce::String& address)
{
    if (oscSocket != nullptr)
        oscSender.send (juce::OSCMessage (juce::OSCAddressPattern (address)));
}

void ConsoleLink::oscMessageReceived (const juce::OSCMessage& m)
{
    lastOscReceivedMs = juce::Time::getMillisecondCounter();
    lastActivityMs = lastOscReceivedMs;
    const auto address = m.getAddressPattern().toString();

    juce::String text = address;
    for (auto& arg : m)
    {
        text << " ";
        if (arg.isInt32())        text << arg.getInt32();
        else if (arg.isFloat32()) text << arg.getFloat32();
        else if (arg.isString())  text << arg.getString();
        else                      text << "?";
    }
    addLog (text);

    if (m.size() == 0 || ! m[0].isInt32())
        return;

    const int value = m[0].getInt32();
    const int mode = juce::jlimit (0, 2, settings.x32Follow);

    // Learning: the first show-related message with a number becomes the trigger
    if (learning)
    {
        const auto lower = address.toLowerCase();
        if (lower.startsWith ("/-show") || lower.startsWith ("/-action") || lower.contains ("scene")
            || lower.contains ("snip") || lower.contains ("cue") || lower.contains ("load"))
        {
            settings.x32Trigger[mode] = address;
            learning = false;
            x32Primed = true;
            addLog ("Learned: " + x32FollowName (mode) + " are announced as " + address);
            sceneChanged (value);
            return;
        }
    }

    if (address == "/-show/prepos/current" && ! x32Primed)
    {
        x32Primed = true;     // answer to our own query on connect
        currentScene = value;
        return;
    }

    if (address == settings.x32Trigger[mode])
    {
        sceneChanged (value);
    }
    else if (address == "/-stat/selidx")
    {
        // 0-31 are input channels 1-32; the rest are aux, FX, buses etc.
        if (value >= 0 && value < 32 && onChannelSelected != nullptr)
            channelSelected (value + 1);
    }
}

void ConsoleLink::sceneChanged (int scene)
{
    const auto now = juce::Time::getMillisecondCounter();

    // A recall can be reported twice (recall started + current changed)
    if (scene == currentScene && now - lastSceneEventMs < 1000)
        return;

    currentScene = scene;
    lastSceneEventMs = now;
    addLog ("Scene " + juce::String (scene) + " recalled on the console");

    if (onSceneRecalled != nullptr)
        onSceneRecalled (scene);
}

void ConsoleLink::addLog (const juce::String& s)
{
    log.add (juce::Time::getCurrentTime().toString (false, true, true, true) + "  " + s);

    if (log.size() > 400)
        log.removeRange (0, log.size() - 400);

    ++logCounter;
}

//==============================================================================
static int leadingNumber (const juce::File& f)
{
    const auto digits = f.getFileNameWithoutExtension().initialSectionContainingOnly ("0123456789");
    return digits.isEmpty() ? -1 : digits.getIntValue();
}

juce::File ConsoleLink::fileForScene (int scene) const
{
    if (! settings.sceneFolder.isDirectory())
        return {};

    for (auto& f : settings.sceneFolder.findChildFiles (juce::File::findFiles, false, "*.liverack"))
        if (leadingNumber (f) == scene)
            return f;

    return {};
}

juce::File ConsoleLink::newFileForScene (int scene) const
{
    auto existing = fileForScene (scene);
    if (existing.existsAsFile())
        return existing;

    return settings.sceneFolder.getChildFile (juce::String (scene).paddedLeft ('0', 3) + ".liverack");
}

void ConsoleLink::saveSettingsTo (const juce::File& file) const
{
    juce::XmlElement xml ("CONSOLELINK");
    xml.setAttribute ("type", (int) settings.type);
    xml.setAttribute ("address", settings.address);
    xml.setAttribute ("sceneFolder", settings.sceneFolder.getFullPathName());
    xml.setAttribute ("recallScenes", settings.recallScenes);
    xml.setAttribute ("followSelection", settings.followSelection);
    xml.setAttribute ("x32Follow", settings.x32Follow);
    xml.setAttribute ("name", settings.name);
    for (auto& c : settings.slots)
    {
        auto* e = xml.createNewChildElement ("SLOT");
        e->setAttribute ("slot", c.slot);
        e->setAttribute ("card", c.card);
        e->setAttribute ("driver", c.driver);
        e->setAttribute ("first", c.firstChannel);
        e->setAttribute ("count", c.numChannels);
    }
    for (int i = 0; i < 3; ++i)
        xml.setAttribute ("x32Trigger" + juce::String (i), settings.x32Trigger[i]);
    xml.writeTo (file);
}

void ConsoleLink::loadSettingsFrom (const juce::File& file)
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr)
        return;

    Settings s;
    s.type            = (Type) juce::jlimit (0, 2, xml->getIntAttribute ("type", 0));
    s.address         = xml->getStringAttribute ("address");
    s.recallScenes    = xml->getBoolAttribute ("recallScenes", true);
    s.followSelection = xml->getBoolAttribute ("followSelection", true);
    s.x32Follow       = juce::jlimit (0, 2, xml->getIntAttribute ("x32Follow", 0));
    for (int i = 0; i < 3; ++i)
        s.x32Trigger[i] = xml->getStringAttribute ("x32Trigger" + juce::String (i), s.x32Trigger[i]);

    s.name = xml->getStringAttribute ("name");
    for (auto* e : xml->getChildWithTagNameIterator ("SLOT"))
    {
        CardSlot c;
        c.slot = e->getStringAttribute ("slot");
        c.card = juce::jlimit (0, numCardTypes - 1, e->getIntAttribute ("card", 0));
        c.driver = juce::jmax (0, e->getIntAttribute ("driver", 0));
        c.firstChannel = juce::jmax (1, e->getIntAttribute ("first", 1));
        c.numChannels = juce::jmax (0, e->getIntAttribute ("count", 0));
        s.slots.push_back (c);
    }
    if (s.slots.empty())
        s.slots = defaultSlots (s.type);

    const auto folder = xml->getStringAttribute ("sceneFolder");
    if (juce::File::isAbsolutePath (folder))
        s.sceneFolder = juce::File (folder);

    setSettings (s);
}

//==============================================================================
void ConsoleLink::channelSelected (int channel)
{
    lastSelectedChannel = channel;
    if (onChannelSelected != nullptr)
        onChannelSelected (channel);
}

juce::String ConsoleLink::getDisplayName() const
{
    if (settings.name.isNotEmpty())
        return settings.name;
    return settings.type == Type::off ? juce::String ("Console") : typeName (settings.type);
}

juce::String ConsoleLink::cardTypeName (int t)
{
    static const char* names[numCardTypes] = { "Empty", "Dante", "Waves SoundGrid", "Stage box", "Other" };
    return names[juce::jlimit (0, numCardTypes - 1, t)];
}

std::vector<ConsoleLink::CardSlot> ConsoleLink::defaultSlots (Type t)
{
    if (t == Type::yamahaRcp)
        return { { "Built-in Dante", 1, 0, 1, 64 }, { "MY Slot 1", 0, 0, 1, 0 },
                 { "MY Slot 2", 0, 0, 1, 0 }, { "MY Slot 3", 0, 0, 1, 0 } };

    if (t == Type::behringerX32)
        return { { "Card slot", 2, 0, 1, 32 }, { "AES50 A", 3, 0, 1, 0 }, { "AES50 B", 0, 0, 1, 0 } };

    return { { "Slot 1", 0, 0, 1, 0 } };
}
