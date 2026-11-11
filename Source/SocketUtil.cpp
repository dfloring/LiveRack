#include "SocketUtil.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
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
}
