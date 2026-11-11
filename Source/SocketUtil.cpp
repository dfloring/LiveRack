#include "SocketUtil.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <winsock2.h>
 #include <ws2tcpip.h>
 #include <windows.h>
 #include <iphlpapi.h>
 #pragma comment (lib, "iphlpapi.lib")
#else
 #include <ifaddrs.h>
 #include <net/if.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>
#endif

namespace SocketUtil
{
void keepInThisProcess (int rawSocketHandle)
{
   #if JUCE_WINDOWS
    if (rawSocketHandle != -1 && rawSocketHandle != 0)
        SetHandleInformation ((HANDLE) (uintptr_t) (unsigned int) rawSocketHandle, HANDLE_FLAG_INHERIT, 0);
   #else
    juce::ignoreUnused (rawSocketHandle);
   #endif
}

juce::String getLanAddress()
{
    // Private network ranges first, then anything else that can be reached.
    // Skips loopback, link-local (169.254), multicast (224-239) and broadcast
    // addresses, which Windows lists alongside the real ones.
    juce::String best, fallback;

    for (auto& a : juce::IPAddress::getAllAddresses (false))
    {
        if (a.isNull() || a.isIPv6)
            continue;

        const int b0 = a.address[0], b1 = a.address[1];
        if (b0 == 0 || b0 == 127 || b0 >= 224 || (b0 == 169 && b1 == 254))
            continue;

        const bool isPrivate = b0 == 10 || (b0 == 192 && b1 == 168) || (b0 == 172 && b1 >= 16 && b1 <= 31);

        if (isPrivate && best.isEmpty())
            best = a.toString();
        else if (fallback.isEmpty())
            fallback = a.toString();
    }

    return best.isNotEmpty() ? best : fallback;
}

static juce::String ipv4 (juce::uint32 hostOrder)
{
    return juce::String ((hostOrder >> 24) & 0xff) + "." + juce::String ((hostOrder >> 16) & 0xff) + "."
         + juce::String ((hostOrder >> 8) & 0xff) + "." + juce::String (hostOrder & 0xff);
}

std::vector<Network> getNetworks()
{
    std::vector<Network> list;

   #if JUCE_WINDOWS
    ULONG size = 16 * 1024;
    juce::HeapBlock<char> buffer;

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        buffer.malloc (size);
        const auto result = GetAdaptersAddresses (AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                                  nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*> (buffer.get()), &size);
        if (result == ERROR_BUFFER_OVERFLOW)
            continue;
        if (result != NO_ERROR)
            return list;

        for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*> (buffer.get()); a != nullptr; a = a->Next)
        {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
                continue;

            for (auto* u = a->FirstUnicastAddress; u != nullptr; u = u->Next)
            {
                if (u->Address.lpSockaddr == nullptr || u->Address.lpSockaddr->sa_family != AF_INET)
                    continue;

                const auto* in = reinterpret_cast<const sockaddr_in*> (u->Address.lpSockaddr);
                const juce::uint32 addr = ntohl (in->sin_addr.s_addr);
                const int prefix = juce::jlimit (0, 32, (int) u->OnLinkPrefixLength);
                const juce::uint32 mask = prefix == 0 ? 0u : (0xffffffffu << (32 - prefix));

                Network n;
                n.id = juce::String (a->AdapterName);
                n.name = juce::String (a->FriendlyName);
                n.address = ipv4 (addr);
                n.broadcast = prefix > 0 && prefix < 31 ? ipv4 ((addr & mask) | ~mask) : juce::String();
                n.linkLocal = (addr >> 16) == 0xa9fe;
                n.ip = addr;
                n.mask = mask;
                list.push_back (n);
            }
        }
        break;
    }
   #else
    ifaddrs* all = nullptr;
    if (getifaddrs (&all) != 0)
        return list;

    for (auto* i = all; i != nullptr; i = i->ifa_next)
    {
        if (i->ifa_addr == nullptr || i->ifa_addr->sa_family != AF_INET
            || (i->ifa_flags & IFF_UP) == 0 || (i->ifa_flags & IFF_LOOPBACK) != 0)
            continue;

        const juce::uint32 addr = ntohl (reinterpret_cast<const sockaddr_in*> (i->ifa_addr)->sin_addr.s_addr);
        const juce::uint32 mask = i->ifa_netmask != nullptr
                                    ? ntohl (reinterpret_cast<const sockaddr_in*> (i->ifa_netmask)->sin_addr.s_addr) : 0u;

        Network n;
        n.id = juce::String (i->ifa_name);
        n.name = juce::String (i->ifa_name);
        n.address = ipv4 (addr);
        n.broadcast = mask != 0 && mask != 0xffffffffu ? ipv4 ((addr & mask) | ~mask) : juce::String();
        n.linkLocal = (addr >> 16) == 0xa9fe;
        n.ip = addr;
        n.mask = mask;
        list.push_back (n);
    }

    freeifaddrs (all);
   #endif

    return list;
}

bool findNetwork (const juce::String& id, Network& result)
{
    for (auto& n : getNetworks())
        if (n.id == id)
        {
            result = n;
            return true;
        }
    return false;
}

bool isOnNetworkOrLocal (const juce::String& ip, const Network& n)
{
    juce::StringArray parts;
    parts.addTokens (ip.trim(), ".", "");
    if (parts.size() != 4)
        return ip.startsWith ("::1") || ip.startsWithIgnoreCase ("::ffff:127.");

    const juce::uint32 a = ((juce::uint32) parts[0].getIntValue() << 24) | ((juce::uint32) parts[1].getIntValue() << 16)
                         | ((juce::uint32) parts[2].getIntValue() << 8) | (juce::uint32) parts[3].getIntValue();
    if ((a >> 24) == 127)
        return true;
    for (auto& own : getNetworks())
        if (own.ip == a)
            return true; // one of this computer's own addresses
    return n.mask != 0 && (a & n.mask) == (n.ip & n.mask);
}
}
