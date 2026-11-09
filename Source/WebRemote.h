#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <memory>

class PluginViewCapture;

// A small web server so an iPad (or any phone, tablet or computer) can run
// LiveRack from a browser: open http://<this computer's IP>:<port> in Safari.
// It serves one touch page; the page polls /state and posts to /cmd.
// /view is a WebSocket carrying a live picture of one plugin's window
// (JPEG frames out, touches and typing in).
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

    bool enabled = false;
    int port = 8080;
    std::unique_ptr<juce::StreamingSocket> listener;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (8) };
    std::atomic<bool> stopping { false };
    std::atomic<int> numViews { 0 };

    juce::CriticalSection clientLock;
    std::map<juce::String, juce::uint32> clients; // ip -> last request time
    std::atomic<juce::uint32> lastActivityMs { 0 };
};
