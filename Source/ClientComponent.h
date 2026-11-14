#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class ServerFinder;

// LiveRack Client's main window: the "Connect to a server" screen, then the
// usual LiveRack screens for that server (racks, plugin panel, Devices,
// Routing, Plugins, Snapshots), all through the network. The audio, plugins
// and show file stay on the server.
class ClientComponent : public juce::Component
{
public:
    // host empty: the connect screen (with the last server offered)
    explicit ClientComponent (const juce::String& host = {}, int port = 0);
    ~ClientComponent() override;

    void connectTo (const juce::String& host, int port, const juce::String& name);
    void showConnectScreen();

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int defaultPort = 8090;

private:
    class ConnectScreen;
    class ClientView;

    std::unique_ptr<ServerFinder> finder;
    std::unique_ptr<ConnectScreen> connectScreen;
    std::unique_ptr<ClientView> view;

   #if LIVERACK_PREVIEW_HOOKS
    friend struct ClientPreview;
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClientComponent)
};
