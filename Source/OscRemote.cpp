#include "OscRemote.h"
#include "RackEngine.h"

namespace
{
    // Minimal OSC reader: strings, int32, float32, true/false, double, int64
    struct Reader
    {
        const char* data; int size; int pos = 0;

        bool readString (juce::String& out)
        {
            int end = pos;
            while (end < size && data[end] != 0) ++end;
            if (end >= size) return false;
            out = juce::String::fromUTF8 (data + pos, end - pos);
            pos = (end + 4) & ~3;
            return pos <= size;
        }

        bool readInt32 (juce::int32& v)
        {
            if (pos + 4 > size) return false;
            v = (juce::int32) juce::ByteOrder::bigEndianInt (data + pos);
            pos += 4;
            return true;
        }

        bool readInt64 (juce::int64& v)
        {
            if (pos + 8 > size) return false;
            v = (juce::int64) juce::ByteOrder::bigEndianInt64 (data + pos);
            pos += 8;
            return true;
        }
    };
}

OscRemote::OscRemote() : juce::Thread ("LiveRack OSC remote")
{
    loadSettings();
}

OscRemote::~OscRemote()
{
    setEnabled (false, port);
}

void OscRemote::setEnabled (bool shouldBeEnabled, int newPort)
{
    stopTimer();
    signalThreadShouldExit();
    if (socket != nullptr)
        socket->shutdown();
    stopThread (1000);
    sender.disconnect();
    socket.reset();
    clients.clear();

    enabled = shouldBeEnabled;
    port = juce::jlimit (1024, 65535, newPort);

    if (! enabled)
        return;

    socket = std::make_unique<juce::DatagramSocket> (false);
    if (! socket->bindToPort (port))
    {
        socket.reset();
        return;
    }

    sender.connectToSocket (*socket, "127.0.0.1", port);
    startThread();
    startTimerHz (30);
}

juce::String OscRemote::getStatusText() const
{
    if (! enabled)        return "OSC remote off";
    if (socket == nullptr) return "OSC port " + juce::String (port) + " is in use";
    return "OSC on port " + juce::String (port) + ", " + juce::String (clients.size())
           + (clients.size() == 1 ? " client" : " clients");
}

void OscRemote::run()
{
    juce::HeapBlock<char> buffer (65536);

    while (! threadShouldExit() && socket != nullptr)
    {
        if (socket->waitUntilReady (true, 100) <= 0)
            continue;

        juce::String ip;
        int fromPort = 0;
        const int n = socket->read (buffer.get(), 65536, false, ip, fromPort);

        if (n > 0)
            parsePacket (buffer.get(), n, ip, fromPort, 0);
    }
}

void OscRemote::parsePacket (const char* data, int size, const juce::String& ip, int fromPort, int depth)
{
    if (size < 4 || depth > 4)
        return;

    Reader r { data, size };

    if (size >= 16 && std::memcmp (data, "#bundle", 8) == 0)
    {
        r.pos = 16; // "#bundle\0" + time tag
        while (r.pos + 4 <= size)
        {
            juce::int32 len;
            if (! r.readInt32 (len) || len <= 0 || r.pos + len > size)
                return;
            parsePacket (data + r.pos, len, ip, fromPort, depth + 1);
            r.pos += len;
        }
        return;
    }

    Incoming in;
    in.ip = ip;
    in.port = fromPort;

    if (! r.readString (in.address) || ! in.address.startsWithChar ('/'))
        return;

    juce::String tags;
    if (r.pos < size && data[r.pos] == ',')
        r.readString (tags);

    for (int i = 1; i < tags.length(); ++i)
    {
        const auto t = tags[i];
        juce::int32 iv; juce::int64 lv; juce::String sv;

        if (t == 'i')      { if (! r.readInt32 (iv)) break; in.args.add ((int) iv); }
        else if (t == 'f') { if (! r.readInt32 (iv)) break; float f; std::memcpy (&f, &iv, 4); in.args.add (f); }
        else if (t == 's') { if (! r.readString (sv)) break; in.args.add (sv); }
        else if (t == 'h') { if (! r.readInt64 (lv)) break; in.args.add ((juce::int64) lv); }
        else if (t == 'd') { if (! r.readInt64 (lv)) break; double d; std::memcpy (&d, &lv, 8); in.args.add (d); }
        else if (t == 'T') in.args.add (true);
        else if (t == 'F') in.args.add (false);
        else break; // unsupported type: keep what we have
    }

    const juce::ScopedLock sl (queueLock);
    if (queue.size() < 1000)
        queue.push_back (std::move (in));
}

void OscRemote::timerCallback()
{
    std::vector<Incoming> items;
    {
        const juce::ScopedLock sl (queueLock);
        items.swap (queue);
    }

    const auto now = juce::Time::getMillisecondCounter();

    for (auto& in : items)
    {
        lastActivityMs = now;

        auto it = std::find_if (clients.begin(), clients.end(),
                                [&in] (const Client& c) { return c.ip == in.ip && c.port == in.port; });

        const bool isNew = it == clients.end();

        if (isNew)
        {
            clients.push_back ({ in.ip, in.port, now });
            it = clients.end() - 1;
        }

        it->lastSeen = now;

        // A new client (or one asking) gets the whole current state
        if (isNew || in.address == "/liverack/subscribe")
            for (auto& [address, value] : lastSent)
                sendTo (*it, address, value);

        if (onMessage != nullptr && in.address != "/liverack/ping" && in.address != "/liverack/subscribe")
            onMessage (in.address, in.args);
    }

    // Forget clients that have gone quiet for a minute
    clients.erase (std::remove_if (clients.begin(), clients.end(),
                                   [now] (const Client& c) { return now - c.lastSeen > 60000; }),
                   clients.end());
}

void OscRemote::send (const juce::String& address, const juce::var& value)
{
    auto it = lastSent.find (address);
    if (it != lastSent.end() && it->second == value)
        return;

    lastSent[address] = value;

    for (auto& c : clients)
        sendTo (c, address, value);
}

void OscRemote::sendTo (const Client& c, const juce::String& address, const juce::var& value)
{
    if (socket == nullptr)
        return;

    try
    {
        juce::OSCMessage m { juce::OSCAddressPattern (address) };

        if (value.isBool() || value.isInt() || value.isInt64())  m.addInt32 ((juce::int32) (int) value);
        else if (value.isDouble())                             m.addFloat32 ((float) (double) value);
        else                                                   m.addString (value.toString());

        sender.sendToIPAddress (c.ip, c.port, m);
    }
    catch (const juce::OSCFormatError&) {}
}

void OscRemote::saveSettings() const
{
    juce::XmlElement xml ("OSCREMOTE");
    xml.setAttribute ("enabled", enabled);
    xml.setAttribute ("port", port);
    xml.writeTo (RackEngine::getSettingsDir().getChildFile ("osc-remote.xml"));
}

void OscRemote::loadSettings()
{
    auto xml = juce::parseXML (RackEngine::getSettingsDir().getChildFile ("osc-remote.xml"));
    const bool on = xml != nullptr ? xml->getBoolAttribute ("enabled", true) : true;
    const int p = xml != nullptr ? xml->getIntAttribute ("port", 9010) : 9010;
    setEnabled (on, p);
}
