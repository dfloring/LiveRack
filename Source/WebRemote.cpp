#include "WebRemote.h"
#include "RackEngine.h"
#include "PluginViewCapture.h"
#include "BinaryData.h"

#if ! JUCE_WINDOWS
 #include <netinet/in.h>
 #include <netinet/tcp.h>
 #include <sys/socket.h>
 #include <cerrno>
#endif

namespace
{
//==============================================================================
// Sends everything, waiting while the network catches up. JUCE's non-blocking
// read() leaves the socket non-blocking, so a plain write fails with "would
// block" as soon as the send buffer fills (a big frame over Wi-Fi).
bool sendAll (juce::StreamingSocket& socket, const void* data, size_t size)
{
    const auto fd = socket.getRawSocketHandle();
    auto* p = static_cast<const char*> (data);
    auto lastProgress = juce::Time::getMillisecondCounter();

    while (size > 0)
    {
        const int chunk = (int) juce::jmin (size, (size_t) 1 << 20);
       #if JUCE_WINDOWS
        juce::ignoreUnused (fd);
        const int n = socket.write (p, chunk);
        const bool wouldBlock = n < 0 && socket.isConnected(); // a real error shows up in the wait below
       #elif JUCE_LINUX || JUCE_BSD
        const int n = (int) ::send (fd, p, (size_t) chunk, MSG_NOSIGNAL);
        const bool wouldBlock = n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
       #else
        const int n = (int) ::send (fd, p, (size_t) chunk, 0);
        const bool wouldBlock = n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
       #endif

        if (n > 0)
        {
            p += n;
            size -= (size_t) n;
            lastProgress = juce::Time::getMillisecondCounter();
            continue;
        }

        if (! wouldBlock)
            return false;

        // No room yet: wait for it, but give up on a connection that's stuck
        if (juce::Time::getMillisecondCounter() - lastProgress > 5000)
            return false;
        if (socket.waitUntilReady (false, 200) < 0)
            return false;
    }
    return true;
}

// A closed connection must not kill the app with SIGPIPE
void noSigPipe (juce::StreamingSocket& socket)
{
   #if JUCE_MAC || JUCE_IOS
    int on = 1;
    setsockopt (socket.getRawSocketHandle(), SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof (on));
   #else
    juce::ignoreUnused (socket);
   #endif
}

//==============================================================================
// SHA-1, only for the WebSocket handshake
juce::MemoryBlock sha1 (const juce::String& text)
{
    juce::MemoryBlock msg (text.toRawUTF8(), text.getNumBytesAsUTF8());
    const juce::uint64 bitLength = (juce::uint64) msg.getSize() * 8;
    msg.append ("\x80", 1);
    while (msg.getSize() % 64 != 56)
        msg.append ("\0", 1);
    for (int i = 7; i >= 0; --i)
    {
        const auto b = (char) ((bitLength >> (i * 8)) & 0xff);
        msg.append (&b, 1);
    }

    juce::uint32 h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    auto rol = [] (juce::uint32 v, int n) { return (v << n) | (v >> (32 - n)); };
    auto* data = static_cast<const juce::uint8*> (msg.getData());

    for (size_t chunk = 0; chunk < msg.getSize(); chunk += 64)
    {
        juce::uint32 w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = ((juce::uint32) data[chunk + (size_t) i * 4] << 24) | ((juce::uint32) data[chunk + (size_t) i * 4 + 1] << 16)
                 | ((juce::uint32) data[chunk + (size_t) i * 4 + 2] << 8) | (juce::uint32) data[chunk + (size_t) i * 4 + 3];
        for (int i = 16; i < 80; ++i)
            w[i] = rol (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        juce::uint32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i)
        {
            juce::uint32 f, k;
            if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }
            const juce::uint32 temp = rol (a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol (b, 30); b = a; a = temp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    juce::MemoryBlock digest;
    for (auto v : h)
        for (int i = 3; i >= 0; --i)
        {
            const auto byte = (char) ((v >> (i * 8)) & 0xff);
            digest.append (&byte, 1);
        }
    return digest;
}

//==============================================================================
// A minimal WebSocket (RFC 6455) over an accepted socket
class WebSocket
{
public:
    explicit WebSocket (juce::StreamingSocket& s) : socket (s) {}

    bool handshake (const juce::String& key)
    {
        const auto digest = sha1 (key.trim() + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        juce::String head;
        head << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << juce::Base64::toBase64 (digest.getData(), digest.getSize()) << "\r\n\r\n";
        return writeAll (head.toRawUTF8(), head.getNumBytesAsUTF8());
    }

    bool sendText (const juce::String& text)        { return sendFrame (0x1, text.toRawUTF8(), text.getNumBytesAsUTF8()); }
    bool sendBinary (const void* data, size_t size) { return sendFrame (0x2, data, size); }
    void close()                                     { sendFrame (0x8, nullptr, 0); }

    // Reads one message. Returns 1 for text, 2 for binary, 0 for control frames
    // handled here, -1 when the connection closed.
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
            const bool masked = (head[1] & 0x80) != 0;
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

            if (length > (1u << 20))
                return -1;

            juce::uint8 mask[4] = {};
            if (masked && ! readAll (mask, 4))
                return -1;

            juce::MemoryBlock payload ((size_t) length);
            if (length > 0 && ! readAll (payload.getData(), (size_t) length))
                return -1;
            auto* p = static_cast<juce::uint8*> (payload.getData());
            if (masked)
                for (size_t i = 0; i < (size_t) length; ++i)
                    p[i] ^= mask[i % 4];

            if (opcode == 0x8)                   // close
                return -1;
            if (opcode == 0x9)                   // ping
            {
                sendFrame (0xA, payload.getData(), payload.getSize());
                return 0;
            }
            if (opcode == 0xA)                   // pong
                return 0;

            if (opcode == 0x1 || opcode == 0x2)
                messageType = opcode;
            message.append (payload.getData(), payload.getSize());

            if (fin)
                return messageType == 0x2 ? 2 : 1;
        }
    }

private:
    bool sendFrame (int opcode, const void* data, size_t size)
    {
        juce::uint8 head[10];
        size_t n = 0;
        head[n++] = (juce::uint8) (0x80 | opcode);
        if (size < 126)
            head[n++] = (juce::uint8) size;
        else if (size < 65536)
        {
            head[n++] = 126;
            head[n++] = (juce::uint8) (size >> 8);
            head[n++] = (juce::uint8) size;
        }
        else
        {
            head[n++] = 127;
            for (int i = 7; i >= 0; --i)
                head[n++] = (juce::uint8) ((juce::uint64) size >> (i * 8));
        }
        return writeAll (head, n) && (size == 0 || writeAll (data, size));
    }

    bool writeAll (const void* data, size_t size) { return sendAll (socket, data, size); }

    bool readAll (void* data, size_t size)
    {
        auto* p = static_cast<char*> (data);
        while (size > 0)
        {
            if (socket.waitUntilReady (true, 5000) <= 0)
                return false;
            const int n = socket.read (p, (int) size, false);
            if (n <= 0)
                return false;
            p += n;
            size -= (size_t) n;
        }
        return true;
    }

    juce::StreamingSocket& socket;
    int messageType = 0x1;
};
} // namespace

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
    stopping = true;
    signalThreadShouldExit();
    if (listener != nullptr)
        listener->close();
    stopThread (2000);
    pool.removeAllJobs (true, 2000);
    listener.reset();
    stopping = false;

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

    if (sendAll (s, head.toRawUTF8(), head.getNumBytesAsUTF8()) && size > 0)
        sendAll (s, data, size);
}

void WebRemote::handle (std::unique_ptr<juce::StreamingSocket> socket)
{
    noSigPipe (*socket);

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
    juce::String webSocketKey;
    for (int i = 1; i < lines.size(); ++i)
    {
        if (lines[i].startsWithIgnoreCase ("content-length:"))
            contentLength = lines[i].fromFirstOccurrenceOf (":", false, false).trim().getIntValue();
        if (lines[i].startsWithIgnoreCase ("sec-websocket-key:"))
            webSocketKey = lines[i].fromFirstOccurrenceOf (":", false, false).trim();
    }

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

    if (method == "GET" && path == "/view" && webSocketKey.isNotEmpty())
    {
        runPluginView (*socket, query, webSocketKey);
    }
    else if (method == "GET" && (path == "/" || path == "/index.html"))
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
// A live plugin window: JPEG frames out, input in. The page acknowledges each
// frame it draws; with at most two frames unacknowledged, the picture never
// falls behind on a slow network (frames are skipped instead).
void WebRemote::runPluginView (juce::StreamingSocket& socket, const juce::String& query, const juce::String& key)
{
    WebSocket ws (socket);
    if (! ws.handshake (key))
        return;

   #if ! JUCE_WINDOWS
    int noDelay = 1;
    setsockopt (socket.getRawSocketHandle(), IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof (noDelay));
   #endif

    // Open the view on the message thread
    struct Opening
    {
        juce::CriticalSection lock;
        std::shared_ptr<PluginViewCapture> capture;
        juce::String error;
        bool abandoned = false;
        juce::WaitableEvent done;
    };
    auto opening = std::make_shared<Opening>();
    auto openFn = openPluginView;
    auto closeFn = closePluginView;

    juce::MessageManager::callAsync ([opening, openFn, closeFn, query]
    {
        juce::String error;
        auto capture = openFn != nullptr ? openFn (query, error) : nullptr;
        if (capture == nullptr && error.isEmpty())
            error = "Plugin windows aren't available.";

        const juce::ScopedLock sl (opening->lock);
        if (opening->abandoned)
        {
            if (capture != nullptr && closeFn != nullptr)
                closeFn (std::move (capture));
        }
        else
        {
            opening->capture = std::move (capture);
            opening->error = error;
        }
        opening->done.signal();
    });

    const bool opened = opening->done.wait (5000);
    std::shared_ptr<PluginViewCapture> capture;
    juce::String error;
    {
        const juce::ScopedLock sl (opening->lock);
        opening->abandoned = true;
        capture = std::move (opening->capture);
        error = opened ? opening->error : juce::String ("LiveRack didn't respond.");
    }

    auto sendJson = [&ws] (const juce::String& type, const juce::String& msg)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("t", type);
        if (msg.isNotEmpty())
            o->setProperty ("msg", msg);
        return ws.sendText (juce::JSON::toString (juce::var (o), true));
    };

    if (capture == nullptr)
    {
        sendJson ("error", error);
        ws.close();
        return;
    }

    ++numViews;
    sendJson ("open", {});

    std::weak_ptr<PluginViewCapture> weak = capture;
    juce::MemoryBlock frame, message;
    int inFlight = 0;
    auto lastHeard = juce::Time::getMillisecondCounter();
    bool alive = true;

    while (alive && ! stopping.load())
    {
        int ready = socket.waitUntilReady (true, 4);
        if (ready < 0)
            break;

        // Read everything the page has sent so far
        bool lost = false;
        for (int reads = 0; ready > 0 && reads < 64; ++reads, ready = socket.waitUntilReady (true, 0))
        {
            const int type = ws.read (message);
            if (type < 0)
            {
                lost = true;
                break;
            }

            if (type == 1)
            {
                lastHeard = juce::Time::getMillisecondCounter();
                lastActivityMs = lastHeard;
                const auto event = juce::JSON::parse (message.toString());
                const auto t = event["t"].toString();

                if (t == "ack")
                    inFlight = juce::jmax (0, inFlight - 1);
                else if (t == "ping")
                    lost = ! ws.sendText ("{\"t\":\"pong\"}"); // lets the page tell a live link from a dead one
                else
                    juce::MessageManager::callAsync ([weak, event] { if (auto c = weak.lock()) c->input (event); });
            }
        }
        if (lost || ready < 0)
            break;

        juce::String text;
        while (capture->takeMessage (text))
            alive = alive && ws.sendText (text);

        if (capture->hasEnded())
            break;

        if (inFlight < 2 && capture->takeFrame (frame))
        {
            if (! ws.sendBinary (frame.getData(), frame.getSize()))
                break;
            ++inFlight;
        }

        if (juce::Time::getMillisecondCounter() - lastHeard > 10000)
            break; // the page went away without closing
    }

    juce::String text;
    while (capture->takeMessage (text))
        ws.sendText (text);
    ws.close();
    --numViews;

    // Close on the message thread; the capture is released there too
    juce::MessageManager::callAsync ([closeFn, c = std::move (capture)]() mutable
    {
        if (closeFn != nullptr)
            closeFn (std::move (c));
        else if (c != nullptr)
            c->stop();
        c.reset();
    });
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
