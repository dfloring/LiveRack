#pragma once
#include <juce_core/juce_core.h>

// A WebSocket client over a connected socket: just what LiveRack's own
// protocol needs (text and binary messages, pings). Client frames are
// masked, as the standard requires. Used by LiveRack Client for /live and
// /view; the server side is in WebRemote.
struct WebSocketClient
{
    explicit WebSocketClient (juce::StreamingSocket& s) : socket (s) {}

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

    // One message: 1 = text, 2 = binary, 0 = a control frame, -1 = closed
    int read (juce::MemoryBlock& message)
    {
        message.reset();
        int firstOpcode = 0;
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
            if (opcode != 0)
                firstOpcode = opcode;

            message.append (payload.getData(), payload.getSize());
            if (fin)
                return firstOpcode == 0x2 ? 2 : 1;
        }
    }
};
