#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

// A small web server so an iPad (or any phone, tablet or computer) can run
// LiveRack from a browser: open http://<this computer's IP>:<port> in Safari.
// It serves one touch page; the page polls /state and posts to /cmd.
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

    void saveSettings() const;
    void loadSettings();

private:
    void run() override;
    void handle (std::unique_ptr<juce::StreamingSocket>);
    juce::String onMessageThread (std::function<juce::String()>);
    static void respond (juce::StreamingSocket&, int status, const juce::String& type, const void* data, size_t size);

    bool enabled = false;
    int port = 8080;
    std::unique_ptr<juce::StreamingSocket> listener;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (4) };

    juce::CriticalSection clientLock;
    std::map<juce::String, juce::uint32> clients; // ip -> last request time
    std::atomic<juce::uint32> lastActivityMs { 0 };
};
