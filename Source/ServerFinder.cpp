#include "ServerFinder.h"
#include "WebRemote.h"
#include "SocketUtil.h"

#if ! JUCE_WINDOWS
 #include <sys/socket.h>
#endif

ServerFinder::ServerFinder() : juce::Thread ("LiveRack server finder")
{
    startThread (juce::Thread::Priority::low);
}

ServerFinder::~ServerFinder()
{
    stopThread (2000);
}

std::vector<ServerFinder::Server> ServerFinder::getServers() const
{
    const auto now = juce::Time::getMillisecondCounter();
    std::vector<Server> list;
    {
        const juce::ScopedLock sl (lock);
        for (auto& s : servers)
            if (now - s.lastSeenMs < 7000)
                list.push_back (s);
    }
    std::sort (list.begin(), list.end(), [] (const Server& a, const Server& b) { return a.name.compareNatural (b.name) < 0; });
    return list;
}

void ServerFinder::run()
{
    while (! threadShouldExit())
    {
        juce::DatagramSocket socket (true);

       #if ! JUCE_WINDOWS
        // Another LiveRack on this computer may be listening too
        int one = 1;
        setsockopt (socket.getRawSocketHandle(), SOL_SOCKET, SO_REUSEPORT, &one, sizeof (one));
       #endif

        if (! socket.bindToPort (WebRemote::beaconPort))
        {
            listening = false;
            wait (2000);
            continue;
        }
        SocketUtil::keepInThisProcess (socket.getRawSocketHandle());
        listening = true;

        char buffer[2048];
        while (! threadShouldExit())
        {
            const int ready = socket.waitUntilReady (true, 250);
            if (ready < 0)
                break;
            if (ready == 0)
                continue;

            juce::String senderIP;
            int senderPort = 0;
            const int n = socket.read (buffer, (int) sizeof (buffer) - 1, false, senderIP, senderPort);
            if (n <= 0)
                continue;

            const auto json = juce::JSON::parse (juce::String::fromUTF8 (buffer, n));
            if (! (bool) json["liverack"] || senderIP.isEmpty())
                continue;

            Server s;
            s.name = json["name"].toString();
            s.host = senderIP;
            s.port = (int) json["port"];
            s.version = json["version"].toString();
            s.lastSeenMs = juce::Time::getMillisecondCounter();
            if (s.port <= 0)
                continue;
            if (s.name.isEmpty())
                s.name = senderIP;

            const juce::ScopedLock sl (lock);
            auto it = std::find_if (servers.begin(), servers.end(),
                                    [&s] (const Server& o) { return o.host == s.host && o.port == s.port; });
            if (it != servers.end())
                *it = s;
            else
                servers.push_back (s);
        }

        listening = false;
    }
}
