#include "WebRemote.h"
#include "RackEngine.h"
#include "BinaryData.h"

WebRemote::WebRemote() : juce::Thread ("LiveRack web remote")
{
    loadSettings();
}

WebRemote::~WebRemote()
{
    setEnabled (false, port);
}

void WebRemote::setEnabled (bool shouldBeEnabled, int newPort)
{
    signalThreadShouldExit();
    if (listener != nullptr)
        listener->close();
    stopThread (2000);
    pool.removeAllJobs (true, 2000);
    listener.reset();

    enabled = shouldBeEnabled;
    port = juce::jlimit (1024, 65535, newPort);

    if (! enabled)
        return;

    auto socket = std::make_unique<juce::StreamingSocket>();
    if (! socket->createListener (port))
        return;

    listener = std::move (socket);
    startThread();
}

juce::String WebRemote::getUrl() const
{
    juce::String ip = "127.0.0.1";
    for (auto& a : juce::IPAddress::getAllAddresses (false))
    {
        const auto s = a.toString();
        if (! a.isNull() && s != "127.0.0.1" && ! s.startsWith ("169.254."))
        {
            ip = s;
            break;
        }
    }
    return "http://" + ip + ":" + juce::String (port);
}

int WebRemote::getNumClients() const
{
    const juce::ScopedLock sl (clientLock);
    const auto now = juce::Time::getMillisecondCounter();
    int n = 0;
    for (auto& [ip, t] : clients)
        if (now - t < 5000)
            ++n;
    return n;
}

juce::String WebRemote::getStatusText() const
{
    if (! enabled)       return "Web remote off";
    if (! isListening()) return "Port " + juce::String (port) + " is in use";
    const int n = getNumClients();
    return getUrl() + "  |  " + juce::String (n) + (n == 1 ? " device" : " devices");
}

//==============================================================================
void WebRemote::run()
{
    while (! threadShouldExit() && listener != nullptr)
    {
        std::unique_ptr<juce::StreamingSocket> client (listener->waitForNextConnection());
        if (client == nullptr || threadShouldExit())
            continue;

        auto* raw = client.release();
        pool.addJob ([this, raw] { handle (std::unique_ptr<juce::StreamingSocket> (raw)); });
    }
}

juce::String WebRemote::onMessageThread (std::function<juce::String()> fn)
{
    auto result = std::make_shared<juce::String>();
    auto done = std::make_shared<juce::WaitableEvent>();

    juce::MessageManager::callAsync ([fn, result, done]
    {
        *result = fn();
        done->signal();
    });

    if (! done->wait (1500))
        return {};
    return *result;
}

void WebRemote::respond (juce::StreamingSocket& s, int status, const juce::String& type, const void* data, size_t size)
{
    const juce::String reason = status == 200 ? "OK" : status == 404 ? "Not Found" : "Bad Request";
    juce::String head;
    head << "HTTP/1.1 " << status << " " << reason << "\r\n"
         << "Content-Type: " << type << "\r\n"
         << "Content-Length: " << (int) size << "\r\n"
         << "Cache-Control: no-store\r\n"
         << "Connection: close\r\n\r\n";

    s.write (head.toRawUTF8(), (int) head.getNumBytesAsUTF8());
    if (size > 0)
        s.write (data, (int) size);
}

void WebRemote::handle (std::unique_ptr<juce::StreamingSocket> socket)
{
    // Read the request head
    juce::MemoryBlock buffer;
    char chunk[4096];
    int headerEnd = -1;

    while (headerEnd < 0 && buffer.getSize() < 65536)
    {
        if (socket->waitUntilReady (true, 3000) <= 0)
            return;
        const int n = socket->read (chunk, sizeof (chunk), false);
        if (n <= 0)
            return;
        buffer.append (chunk, (size_t) n);
        headerEnd = buffer.toString().indexOf ("\r\n\r\n");
    }

    if (headerEnd < 0)
        return;

    const auto all = buffer.toString();
    const auto head = all.substring (0, headerEnd);
    juce::StringArray lines;
    lines.addLines (head);

    juce::StringArray requestLine;
    requestLine.addTokens (lines[0], " ", "");
    const auto method = requestLine[0];
    const auto target = requestLine[1];
    const auto path = target.upToFirstOccurrenceOf ("?", false, false);
    const auto query = target.fromFirstOccurrenceOf ("?", false, false);

    int contentLength = 0;
    for (int i = 1; i < lines.size(); ++i)
        if (lines[i].startsWithIgnoreCase ("content-length:"))
            contentLength = lines[i].fromFirstOccurrenceOf (":", false, false).trim().getIntValue();

    // The body (for POST)
    juce::MemoryBlock body;
    const auto bodyStart = (size_t) headerEnd + 4;
    if (buffer.getSize() > bodyStart)
        body.append (static_cast<const char*> (buffer.getData()) + bodyStart, buffer.getSize() - bodyStart);

    while ((int) body.getSize() < juce::jmin (contentLength, 1 << 20))
    {
        if (socket->waitUntilReady (true, 3000) <= 0)
            break;
        const int n = socket->read (chunk, sizeof (chunk), false);
        if (n <= 0)
            break;
        body.append (chunk, (size_t) n);
    }

    {
        const juce::ScopedLock sl (clientLock);
        clients[socket->getHostName()] = juce::Time::getMillisecondCounter();
    }
    lastActivityMs = juce::Time::getMillisecondCounter();

    if (method == "GET" && (path == "/" || path == "/index.html"))
    {
        respond (*socket, 200, "text/html; charset=utf-8", BinaryData::remote_html, (size_t) BinaryData::remote_htmlSize);
    }
    else if (method == "GET" && path == "/icon.png")
    {
        respond (*socket, 200, "image/png", BinaryData::LiveRackIcon_png, (size_t) BinaryData::LiveRackIcon_pngSize);
    }
    else if (method == "GET" && path == "/state")
    {
        const auto json = onMessageThread ([this, query] { return buildState != nullptr ? buildState (query) : juce::String ("{}"); });
        respond (*socket, 200, "application/json", json.toRawUTF8(), json.getNumBytesAsUTF8());
    }
    else if (method == "POST" && path == "/cmd")
    {
        const auto command = juce::JSON::parse (body.toString());
        juce::MessageManager::callAsync ([this, command] { if (onCommand != nullptr) onCommand (command); });
        const juce::String ok ("{\"ok\":true}");
        respond (*socket, 200, "application/json", ok.toRawUTF8(), ok.getNumBytesAsUTF8());
    }
    else
    {
        const juce::String nf ("Not found");
        respond (*socket, 404, "text/plain", nf.toRawUTF8(), nf.getNumBytesAsUTF8());
    }
}

//==============================================================================
void WebRemote::saveSettings() const
{
    juce::XmlElement xml ("WEBREMOTE");
    xml.setAttribute ("enabled", enabled);
    xml.setAttribute ("port", port);
    xml.writeTo (RackEngine::getSettingsDir().getChildFile ("web-remote.xml"));
}

void WebRemote::loadSettings()
{
    auto xml = juce::parseXML (RackEngine::getSettingsDir().getChildFile ("web-remote.xml"));
    const bool on = xml != nullptr ? xml->getBoolAttribute ("enabled", true) : true;
    const int p = xml != nullptr ? xml->getIntAttribute ("port", 8080) : 8080;
    setEnabled (on, p);
}
