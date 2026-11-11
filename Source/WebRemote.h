#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <memory>

class PluginViewCapture;

// A small web server so an iPad (or any phone, tablet or computer) can run
// LiveRack from a browser: open http://<this computer's IP>:<port> in Safari.
// It serves one touch page. Endpoints:
//   GET  /state        the whole state as JSON (polling; still supported)
//   POST /cmd          one command as JSON
//   WS   /live         the server protocol: state pushed as it changes,
//                      meters, and commands, over one connection (see below)
//   WS   /view         a live picture of one plugin's window
//                      (JPEG frames out, touches and typing in)
// It also announces itself on the network (UDP broadcast) every couple of
// seconds, so a client can find LiveRack servers by name.
//
// The /live protocol (JSON text messages):
//   server -> client
//     {"t":"hello", "protocol":1, "app":"LiveRack", "version":"0.17.0", "server":"<computer name>"}
//     {"t":"state", "data":{...}}     the same JSON as /state for the client's query,
//                                     sent on connect and whenever it changes
//     {"t":"meters", "data":{...}}    levels, gain reduction and timing, ~20 a second
//     {"t":"reply", "id":n, "what":"...", "data":{...}}   answers to "get"
//     {"t":"pong"}
//   client -> server
//     {"t":"query", "q":"live=1&view=2:0"}   what the state should include
//     {"t":"cmd", "cmd":"...", ...}          any /cmd command (an "id" is echoed in a reply)
//     {"t":"get", "what":"library"|"shows"|"devices"|"snapshot", "number":n, "id":n}
//     {"t":"ping"}                           (a connection quiet for 15 s is closed)
class WebRemote : private juce::Thread
{
public:
    WebRemote();
    ~WebRemote() override;

    void setEnabled (bool, int port);
    bool isEnabled() const    { return enabled; }
    bool isListening() const  { return listener != nullptr; }
    int  getPort() const      { return port; }
    juce::String getUrl() const;          // http://192.168.x.x:8080
    int  getNumClients() const;           // seen in the last few seconds
    juce::uint32 getLastActivityMs() const { return lastActivityMs.load(); }
    juce::String getStatusText() const;

    // Called on the message thread. query is the URL's query string.
    std::function<juce::String (const juce::String& query)> buildState;   // JSON
    std::function<void (const juce::var& command)> onCommand;
    std::function<juce::String()> buildMeters;                            // JSON, for /live
    int getNumLiveClients() const { return numLive.load(); }

    // Found by clients on the network under this name (the computer's name by default)
    static constexpr int beaconPort = 47810;
    juce::String getServerName() const;

    struct FoundServer { juce::String name, address, version; int port = 0; };
    // Listens for servers announcing themselves (blocks for up to timeoutMs)
    static std::vector<FoundServer> findServers (int timeoutMs);

    // Message thread. Open a live view of a plugin window for a /view socket
    // (nullptr and an error if it can't), and close it when the socket ends.
    std::function<std::shared_ptr<PluginViewCapture> (const juce::String& query, juce::String& error)> openPluginView;
    std::function<void (std::shared_ptr<PluginViewCapture>)> closePluginView;
    int getNumPluginViews() const { return numViews.load(); }

    void saveSettings() const;
    void loadSettings();

private:
    void run() override;
    void handle (std::unique_ptr<juce::StreamingSocket>);
    juce::String onMessageThread (std::function<juce::String()>);
    static void respond (juce::StreamingSocket&, int status, const juce::String& type, const void* data, size_t size);
    void runPluginView (juce::StreamingSocket&, const juce::String& query, const juce::String& key);
    void runLive (juce::StreamingSocket&, const juce::String& query, const juce::String& key);

    class Beacon;
    std::unique_ptr<Beacon> beacon;

    bool enabled = false;
    int port = 8080;
    std::unique_ptr<juce::StreamingSocket> listener;
    // Each live connection and plugin view holds a thread while it's open
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (32) };
    std::atomic<bool> stopping { false };
    std::atomic<int> numViews { 0 }, numLive { 0 };

    juce::CriticalSection clientLock;
    std::map<juce::String, juce::uint32> clients; // ip -> last request time
    std::atomic<juce::uint32> lastActivityMs { 0 };
};
