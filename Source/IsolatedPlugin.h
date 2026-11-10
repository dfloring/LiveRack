#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

// Plugin isolation (experimental, per plugin, off by default).
//
// An isolated plugin runs in a helper process: a second, hidden copy of
// LiveRack started with a special command line. The rack holds an
// IsolatedPlugin in its place. Audio goes across in shared memory, one block
// at a time, and comes back within the same block, so no latency is added.
// Settings, parameters and the plugin window are handled over a control
// connection.
//
// If the helper crashes or stops responding, only that plugin is affected:
// audio passes through it unprocessed while a fresh helper is started and the
// last known settings are put back.
//
// Limits of this first stage: the plugin's window is a separate window owned
// by the helper (it can't be shown inside the LiveRack panel or on the iPad),
// MIDI isn't passed, and VST3 gain-reduction metering isn't available.
namespace Isolation
{
    constexpr const char* workerUID = "liverackpluginhost";

    // Ask a plugin for ins -> outs channels (1 or 2), falling back sensibly
    void requestLayout (juce::AudioPluginInstance&, int ins, int outs);

    // Helper process side. Returns an object to keep alive if this process was
    // started as a plugin helper, or nullptr for the normal app.
    struct Worker { virtual ~Worker() = default; };
    std::unique_ptr<Worker> startWorkerIfRequested (const juce::String& commandLine);
}

struct IsolatedPluginShared;

class IsolatedPlugin final : public juce::AudioPluginInstance,
                             private juce::Timer
{
public:
    // Starts a helper and loads the plugin in it. nullptr and an error on failure.
    static std::unique_ptr<IsolatedPlugin> create (const juce::PluginDescription&, double sampleRate,
                                                   int blockSize, juce::String& error);
    ~IsolatedPlugin() override;

    // Called on the message thread: "<plugin> crashed and was restarted" etc.
    static std::function<void (const juce::String&)> onEvent;

    void requestLayout (int ins, int outs);
    void crashHelper();                 // test: makes the helper crash
    bool isRunning() const              { return alive.load(); }
    int  getRestartCount() const        { return restarts; }
    int  getBlocksProcessed() const     { return blocksDone.load(); }   // by the helper
    int  getAverageHandOffUs() const    { return avgRoundTripUs.load(); }
    int  getMaxHandOffUs() const        { return maxRoundTripUs.load(); }
    int  getBlocksMissed() const        { return blocksMissed.load(); } // passed through unprocessed
    juce::String getStatusText() const;
    void showHelperWindow (bool shouldShow);

    //==========================================================================
    const juce::String getName() const override                    { return description.name; }
    void prepareToPlay (double sampleRate, int blockSize) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    double getTailLengthSeconds() const override                   { return 0.0; }
    bool acceptsMidi() const override                              { return false; }
    bool producesMidi() const override                             { return false; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                                { return true; }
    int getNumPrograms() override                                  { return 1; }
    int getCurrentProgram() override                               { return 0; }
    void setCurrentProgram (int) override                          {}
    const juce::String getProgramName (int) override               { return {}; }
    void changeProgramName (int, const juce::String&) override     {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    void fillInPluginDescription (juce::PluginDescription& d) const override { d = description; }

private:
    class Link;
    class Param;
    class Placeholder;
    friend class Link;
    friend class Param;
    friend class Placeholder;

    explicit IsolatedPlugin (const juce::PluginDescription&);

    bool start (juce::String& error);                   // launch + load
    void stop();                                        // kill the helper
    void restart (const juce::String& reason);
    bool request (int type, const juce::MemoryBlock& payload, juce::MemoryBlock& reply, int timeoutMs);
    void send (int type, const juce::MemoryBlock& payload);
    void messageFromHelper (const juce::MemoryBlock&);  // connection thread
    void connectionLost();                              // connection thread
    void handleEvent (int type, const juce::MemoryBlock& payload); // message thread
    void handOff (juce::AudioBuffer<float>&, bool bypassed);
    void sendParam (int index, float value);
    void applyParamChanges (juce::InputStream&);
    void timerCallback() override;

    juce::PluginDescription description;
    std::unique_ptr<Link> link;
    std::shared_ptr<std::atomic<bool>> lifeToken { std::make_shared<std::atomic<bool>> (true) };

    // Sync requests
    juce::CriticalSection requestLock, replyLock;
    juce::WaitableEvent replyEvent;
    int nextRequestId = 0, waitingId = 0;
    bool gotReply = false;
    juce::MemoryBlock replyData;

    // Audio hand-off
    juce::File sharedFile;
    std::unique_ptr<juce::MemoryMappedFile> mapping;
    IsolatedPluginShared* shared = nullptr;
    juce::SpinLock audioLock;                           // guards the pipe against restarts
    std::unique_ptr<juce::NamedPipe> pipe;
    juce::String pipeName;
    std::atomic<bool> alive { false }, helperPrepared { false };
    bool stale = false;                                 // audio thread
    std::atomic<juce::uint32> stallStartMs { 0 };
    int waitMs = 20;
    std::atomic<int> blocksDone { 0 }, blocksMissed { 0 }, avgRoundTripUs { 0 }, maxRoundTripUs { 0 };

    // State kept for restarts
    juce::MemoryBlock lastState;
    bool hasState = false;
    double preparedRate = 0.0;
    int preparedBlock = 0, wantedIns = 2, wantedOuts = 2;
    bool wantPrepared = false, wantWindow = false, gaveUp = false;
    std::atomic<bool> expectingLoss { false };
    int restarts = 0;
    juce::Array<juce::uint32> restartTimes;
    juce::Array<Param*> params;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IsolatedPlugin)
};
