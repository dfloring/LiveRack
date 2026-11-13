#include "RemoteConnection.h"

namespace
{
// A WebSocket client over a connected socket: just what the /live protocol
// needs (text messages, pings). Client frames are masked, as the standard requires.
struct ClientSocket
{
    juce::StreamingSocket& socket;
    juce::Random random;

    bool writeAll (const void* data, size_t size)
    {
        auto* p = static_cast<const char*> (data);
        while (size > 0)
        {
            const int n = socket.write (p, (int) juce::jmin (size, (size_t) 65536));
            if (n <= 0)
                return false;
            p += n;
            size -= (size_t) n;
        }
        return true;
    }

    bool readAll (void* data, size_t size)
    {
        auto* p = static_cast<char*> (data);
        while (size > 0)
        {
            const int n = socket.read (p, (int) juce::jmin (size, (size_t) 65536), true);
            if (n <= 0)
                return false;
            p += n;
            size -= (size_t) n;
        }
        return true;
    }

    bool handshake (const juce::String& host, int port, const juce::String& pathAndQuery)
    {
        juce::uint8 nonce[16];
        for (auto& b : nonce)
            b = (juce::uint8) random.nextInt (256);

        juce::String request;
        request << "GET " << pathAndQuery << " HTTP/1.1\r\n"
                << "Host: " << host << ":" << port << "\r\n"
                << "Upgrade: websocket\r\n"
                << "Connection: Upgrade\r\n"
                << "Sec-WebSocket-Key: " << juce::Base64::toBase64 (nonce, sizeof (nonce)) << "\r\n"
                << "Sec-WebSocket-Version: 13\r\n\r\n";
        if (! writeAll (request.toRawUTF8(), request.getNumBytesAsUTF8()))
            return false;

        // The reply's headers, up to the blank line
        juce::String head;
        char c = 0;
        while (! head.endsWith ("\r\n\r\n"))
        {
            if (head.length() > 8192 || socket.waitUntilReady (true, 3000) <= 0 || socket.read (&c, 1, true) != 1)
                return false;
            head += juce::String::charToString ((juce::juce_wchar) (juce::uint8) c);
        }
        return head.upToFirstOccurrenceOf ("\r\n", false, false).contains (" 101");
    }

    bool sendText (const juce::String& text)
    {
        const auto size = (size_t) text.getNumBytesAsUTF8();
        juce::MemoryOutputStream frame;
        frame.writeByte ((char) 0x81);

        if (size < 126)
        {
            frame.writeByte ((char) (0x80 | size));
        }
        else if (size < 65536)
        {
            frame.writeByte ((char) (0x80 | 126));
            frame.writeByte ((char) (size >> 8));
            frame.writeByte ((char) (size & 0xff));
        }
        else
        {
            frame.writeByte ((char) (0x80 | 127));
            for (int i = 7; i >= 0; --i)
                frame.writeByte ((char) ((juce::uint64) size >> (8 * i)));
        }

        juce::uint8 mask[4];
        for (auto& b : mask)
            b = (juce::uint8) random.nextInt (256);
        frame.write (mask, 4);

        const auto* p = reinterpret_cast<const juce::uint8*> (text.toRawUTF8());
        for (size_t i = 0; i < size; ++i)
            frame.writeByte ((char) (p[i] ^ mask[i % 4]));

        return writeAll (frame.getData(), frame.getDataSize());
    }

    // One message: 1 = text, 0 = a control frame, -1 = closed
    int read (juce::MemoryBlock& message)
    {
        message.reset();
        for (;;)
        {
            juce::uint8 head[2];
            if (! readAll (head, 2))
                return -1;

            const bool fin = (head[0] & 0x80) != 0;
            const int opcode = head[0] & 0x0f;
            juce::uint64 length = head[1] & 0x7f;

            if (length == 126)
            {
                juce::uint8 ext[2];
                if (! readAll (ext, 2)) return -1;
                length = ((juce::uint64) ext[0] << 8) | ext[1];
            }
            else if (length == 127)
            {
                juce::uint8 ext[8];
                if (! readAll (ext, 8)) return -1;
                length = 0;
                for (auto b : ext)
                    length = (length << 8) | b;
            }

            if (length > (64u << 20))
                return -1;

            juce::MemoryBlock payload ((size_t) length);
            if (length > 0 && ! readAll (payload.getData(), (size_t) length))
                return -1;

            if (opcode == 0x8)
                return -1;
            if (opcode == 0x9 || opcode == 0xA)
                return 0;

            message.append (payload.getData(), payload.getSize());
            if (fin)
                return opcode == 0x2 ? 0 : 1;
        }
    }
};

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

    ClientSocket ws { socket };
    if (! ws.handshake (host, port, "/live?live=1"))
        return false;

    connected = true;
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
    else if (t == "hello")
    {
        juce::MessageManager::callAsync ([this, flag, name = message["server"].toString()]
        {
            if (flag->load())
                serverName = name;
        });
    }
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
    cfg.recallSafe = (bool) r["safe"];
    cfg.colourIndex = r.hasProperty ("colourIndex") ? (int) r["colourIndex"] : -1;
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
