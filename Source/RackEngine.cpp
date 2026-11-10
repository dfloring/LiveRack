#include "RackEngine.h"
#include "IsolatedPlugin.h"
#include "PluginScanning.h"
#include <atomic>
#include <cmath>

static void writeScope (juce::XmlElement&, const SnapshotScope&);
static SnapshotScope scopeFromXml (const juce::XmlElement&);

//==============================================================================
static void storePeak (std::atomic<float>& slot, float value)
{
    if (value > slot.load (std::memory_order_relaxed))
        slot.store (value, std::memory_order_relaxed);
}

// Passes audio straight through and remembers the peak level
class LevelTap final : public juce::AudioProcessor
{
public:
    LevelTap() : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo())
                                                  .withOutput ("Out", juce::AudioChannelSet::stereo())) {}

    std::atomic<float> peak { 0.0f };

    const juce::String getName() const override { return "LevelTap"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        float m = 0.0f;
        for (int c = 0; c < getTotalNumInputChannels(); ++c)
            m = juce::jmax (m, buffer.getMagnitude (c, 0, buffer.getNumSamples()));
        storePeak (peak, m);
    }

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainInputChannelSet() == l.getMainOutputChannelSet() && ! l.getMainInputChannelSet().isDisabled();
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

//==============================================================================
// Plays the tempo to plugins: always "playing", in 4/4, counting samples
// since the driver started
class TempoPlayHead final : public juce::AudioPlayHead
{
public:
    TempoPlayHead (std::atomic<double>& b, std::atomic<juce::int64>& pos, double& sr)
        : bpm (b), samplePos (pos), sampleRate (sr) {}

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        const double tempo = bpm.load();
        const auto samples = samplePos.load();
        const double seconds = (double) samples / juce::jmax (1.0, sampleRate);
        const double ppq = seconds * tempo / 60.0;

        info.setBpm (tempo);
        info.setTimeSignature (TimeSignature { 4, 4 });
        info.setTimeInSamples (samples);
        info.setTimeInSeconds (seconds);
        info.setPpqPosition (ppq);
        info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        info.setBarCount ((juce::int64) (ppq / 4.0));
        info.setIsPlaying (true);
        info.setIsRecording (false);
        info.setIsLooping (false);
        return info;
    }

private:
    std::atomic<double>& bpm;
    std::atomic<juce::int64>& samplePos;
    double& sampleRate;
};

//==============================================================================
struct RackEngine::Rack
{
    using Node = juce::AudioProcessorGraph::Node;
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

    RackConfig config;
    juce::AudioProcessorGraph graph;
    Node::Ptr inNode, outNode;
    std::vector<Node::Ptr> slots;
    std::vector<Node::Ptr> taps;    // a LevelTap after each slot (same index)
    std::atomic<bool> bypassed { false };
    std::atomic<float> peaks[2][2] {}; // [input/output][channel]
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;

    // Latency-group padding, applied after the plugins
    std::atomic<int> padSamples { 0 };      // while running
    std::atomic<int> bypassDelay { 0 };     // while bypassed: the whole group latency
    static constexpr int delaySize = 1 << 17; // ~2.7 s at 48 kHz
    juce::AudioBuffer<float> delay { 2, delaySize };
    int delayWrite = 0;

    double sampleRate = 48000.0;
    int blockSize = 64;

    void configure (double sr, int bs, bool prepare)
    {
        sampleRate = sr;
        blockSize = bs;
        const int ch = config.numOutputs();

        if (inNode != nullptr)  graph.removeNode (inNode->nodeID);
        if (outNode != nullptr) graph.removeNode (outNode->nodeID);

        graph.setPlayConfigDetails (config.numInputs(), config.numOutputs(), sr, bs);
        inNode  = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioInputNode));
        outNode = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioOutputNode));

        for (auto& t : taps)
            graph.removeNode (t->nodeID);
        taps.clear();
        for (auto& s : slots)
            addTap (juce::jmax (1, s->getProcessor()->getMainBusNumOutputChannels()));

        rebuildConnections();

        buffer.setSize (ch, juce::jmax (bs, 512));
        delay.clear();
        delayWrite = 0;

        if (prepare)
            graph.prepareToPlay (sr, bs);
    }

    void addTap (int channels)
    {
        auto tap = std::make_unique<LevelTap>();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        tap->setBusesLayout (layout);
        taps.push_back (graph.addNode (std::move (tap)));
    }

    void removeTap()
    {
        if (taps.empty())
            return;

        graph.removeNode (taps.back()->nodeID);
        taps.pop_back();
    }

    // Input -> slot 1 -> tap 1 -> slot 2 -> tap 2 -> ... -> Output
    void rebuildConnections()
    {
        for (auto& c : graph.getConnections())
            graph.removeConnection (c);

        auto connect = [this] (Node& src, Node& dst)
        {
            const int outs = src.getProcessor()->getMainBusNumOutputChannels();
            const int ins  = dst.getProcessor()->getMainBusNumInputChannels();

            if (outs <= 0 || ins <= 0)
                return;

            for (int c = 0; c < ins; ++c)
                graph.addConnection ({ { src.nodeID, juce::jmin (c, outs - 1) },
                                       { dst.nodeID, c } });
        };

        Node* prev = inNode.get();

        for (size_t i = 0; i < slots.size(); ++i)
        {
            connect (*prev, *slots[i]);
            prev = slots[i].get();

            if (i < taps.size())
            {
                connect (*prev, *taps[i]);
                prev = taps[i].get();
            }
        }

        connect (*prev, *outNode);
    }

    // Audio thread
    void applyDelay (int numSamples, int amount)
    {
        const int ch = config.numOutputs();
        amount = juce::jlimit (0, delaySize - 1, amount);

        for (int c = 0; c < ch; ++c)
        {
            auto* data = buffer.getWritePointer (c);
            auto* line = delay.getWritePointer (c);
            int w = delayWrite;

            for (int i = 0; i < numSamples; ++i)
            {
                line[w] = data[i];
                data[i] = line[(w - amount) & (delaySize - 1)];
                w = (w + 1) & (delaySize - 1);
            }
        }

        delayWrite = (delayWrite + numSamples) & (delaySize - 1);
    }
};

//==============================================================================
struct RackEngine::Driver : public juce::AudioIODeviceCallback,
                            public juce::ChangeListener
{
    explicit Driver (RackEngine& e) : engine (e) {}

    ~Driver() override
    {
        deviceManager.removeChangeListener (this);
        deviceManager.removeAudioCallback (this);
        deviceManager.closeAudioDevice();
    }

    void start (const juce::XmlElement* state, bool useDefaultDevice)
    {
        deviceManager.initialise (64, 64, state, useDefaultDevice);
        deviceManager.addAudioCallback (this);
        deviceManager.addChangeListener (this);
    }

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int i = 0; i < numOutputs; ++i)
            if (outputs[i] != nullptr)
                juce::FloatVectorOperations::clear (outputs[i], numSamples);

        {
            const juce::SpinLock::ScopedLockType lock (rackLock);

            for (auto* rp : active)
            {
                auto& r = *rp;
                const int ch = r.config.numOutputs();
                const int ins = r.config.numInputs();
                r.buffer.setSize (ch, numSamples, false, false, true);

                for (int c = ins; c < ch; ++c)
                    r.buffer.clear (c, 0, numSamples);

                for (int c = 0; c < ins; ++c)
                {
                    const int src = r.config.input[c];

                    if (juce::isPositiveAndBelow (src, numInputs) && inputs[src] != nullptr)
                        r.buffer.copyFrom (c, 0, inputs[src], numSamples);
                    else
                        r.buffer.clear (c, 0, numSamples);

                    storePeak (r.peaks[0][c], r.buffer.getMagnitude (c, 0, numSamples));
                }

                if (! r.bypassed.load())
                {
                    r.midi.clear();
                    r.graph.processBlock (r.buffer, r.midi);
                    r.applyDelay (numSamples, r.padSamples.load());
                }
                else
                {
                    r.applyDelay (numSamples, r.bypassDelay.load());
                }

                for (int c = 0; c < ch; ++c)
                    storePeak (r.peaks[1][c], r.buffer.getMagnitude (c, 0, numSamples));

                // Add, so two racks returning to the same output are summed
                for (int c = 0; c < ch; ++c)
                {
                    const int dst = r.config.output[c];

                    if (juce::isPositiveAndBelow (dst, numOutputs) && outputs[dst] != nullptr)
                        juce::FloatVectorOperations::add (outputs[dst], r.buffer.getReadPointer (c), numSamples);
                }
            }
        }

        samplePos += numSamples;
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        sampleRate = device->getCurrentSampleRate();
        blockSize  = device->getCurrentBufferSizeSamples();
        samplePos = 0;

        const juce::SpinLock::ScopedLockType lock (rackLock);

        for (auto* r : active)
            r->configure (sampleRate, blockSize, true);

        running = true;
    }

    void audioDeviceStopped() override
    {
        const juce::SpinLock::ScopedLockType lock (rackLock);

        for (auto* r : active)
            r->graph.releaseResources();

        running = false;
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        engine.saveSettings();
        engine.sendChangeMessage();
    }

    RackEngine& engine;
    juce::AudioDeviceManager deviceManager;
    juce::SpinLock rackLock;
    std::vector<Rack*> active;          // swapped under rackLock

    double sampleRate = 48000.0;
    int blockSize = 64;
    bool running = false;
    std::atomic<juce::int64> samplePos { 0 };
    TempoPlayHead playHead { engine.bpm, samplePos, sampleRate };

    // Stats (message thread)
    int lastXRuns = -1, dropouts = 0;
    juce::Time lastDropout;
    double peakCpu = 0.0;
    juce::uint32 peakResetMs = 0;
};

//==============================================================================
static void requestLayout (juce::AudioPluginInstance& plugin, int ins, int outs)
{
    Isolation::requestLayout (plugin, ins, outs);
}

// A plugin inside LiveRack, or (isolated) in a helper process of its own
static std::unique_ptr<juce::AudioPluginInstance> createInstance (juce::AudioPluginFormatManager& formats,
                                                                  const juce::PluginDescription& desc, bool isolated,
                                                                  double sampleRate, int blockSize, juce::String& error)
{
    if (isolated)
        return IsolatedPlugin::create (desc, sampleRate, blockSize, error);

    return formats.createPluginInstance (desc, sampleRate, blockSize, error);
}

static bool isMonoToStereoVersion (const juce::String& pluginName)
{
    const auto n = pluginName.toLowerCase();
    return n.contains ("mono/stereo") || n.contains ("mono-stereo") || n.contains ("m/s ") || n.endsWith ("m>s");
}

//==============================================================================
RackEngine::RackEngine()
{
    formatManager.addDefaultFormats(); // VST3 + AudioUnit
    knownPlugins.setCustomScanner (std::make_unique<PluginScanning::OutOfProcessScanner>());

    if (auto xml = juce::parseXML (getSettingsDir().getChildFile ("plugins.xml")))
        knownPlugins.recreateFromXml (*xml);

    loadDriverSettings();
    startTimerHz (5);
}

RackEngine::~RackEngine()
{
    stopTimer();
    saveSession();
    saveSettings();
    clearAll();
    drivers.clear();
}

void RackEngine::loadDriverSettings()
{
    auto dir = getSettingsDir();
    auto all = juce::parseXML (dir.getChildFile ("audio-drivers.xml"));

    if (all != nullptr && all->getNumChildElements() > 0)
    {
        for (auto* d : all->getChildWithTagNameIterator ("DRIVER"))
        {
            if ((int) drivers.size() >= maxDrivers)
                break;

            drivers.push_back (std::make_unique<Driver> (*this));
            drivers.back()->start (d->getFirstChildElement(), drivers.size() == 1);
        }
    }

    if (drivers.empty())
    {
        // First run, or settings from 0.8 (a single device)
        auto old = juce::parseXML (dir.getChildFile ("audio-device.xml"));
        drivers.push_back (std::make_unique<Driver> (*this));
        drivers.back()->start (old.get(), true);
    }
}

RackEngine::Rack* RackEngine::getRack (int index) const
{
    return juce::isPositiveAndBelow (index, (int) racks.size()) ? racks[(size_t) index].get() : nullptr;
}

RackEngine::Driver* RackEngine::getDriverFor (const Rack& r) const
{
    const int d = juce::isPositiveAndBelow (r.config.driver, (int) drivers.size()) ? r.config.driver : 0;
    return drivers[(size_t) d].get();
}

//==============================================================================
// Drivers
int RackEngine::addDriver()
{
    if ((int) drivers.size() >= maxDrivers)
        return -1;

    drivers.push_back (std::make_unique<Driver> (*this));
    drivers.back()->start (nullptr, false); // the user picks a device for it
    saveSettings();
    sendChangeMessage();
    return (int) drivers.size() - 1;
}

void RackEngine::removeDriver (int index)
{
    if (index <= 0 || index >= (int) drivers.size())
        return;

    // Racks on it go back to driver 0; racks on later drivers shift down
    for (auto& r : racks)
    {
        if (r->config.driver == index)       r->config.driver = 0;
        else if (r->config.driver > index)   --r->config.driver;
    }

    {
        auto& d = *drivers[(size_t) index];
        const juce::SpinLock::ScopedLockType lock (d.rackLock);
        d.active.clear();
    }

    drivers.erase (drivers.begin() + index);

    for (auto& r : racks)
        configureRack (*r, false);

    rebuildDriverLists();
    saveSettings();
    sendChangeMessage();
}

juce::AudioDeviceManager& RackEngine::getDeviceManager (int index)
{
    return drivers[(size_t) juce::jlimit (0, (int) drivers.size() - 1, index)]->deviceManager;
}

double RackEngine::getSampleRate (int index) const
{
    return juce::isPositiveAndBelow (index, (int) drivers.size()) ? drivers[(size_t) index]->sampleRate : 48000.0;
}

DriverStatus RackEngine::getDriverStatus (int index) const
{
    DriverStatus s;
    if (! juce::isPositiveAndBelow (index, (int) drivers.size()))
        return s;

    auto& d = *drivers[(size_t) index];
    auto& dm = d.deviceManager;
    s.typeName = dm.getCurrentAudioDeviceType();

    for (auto& r : racks)
        if (getDriverFor (*r) == &d)
            ++s.numRacks;

    if (auto* dev = dm.getCurrentAudioDevice())
    {
        s.open = true;
        s.deviceName = dev->getName();
        s.sampleRate = dev->getCurrentSampleRate();
        s.bufferSize = dev->getCurrentBufferSizeSamples();
        s.numIns = dev->getActiveInputChannels().countNumberOfSetBits();
        s.numOuts = dev->getActiveOutputChannels().countNumberOfSetBits();
        s.ioLatency = dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples();
        s.cpu = dm.getCpuUsage();
        s.peakCpu = d.peakCpu;
        s.dropouts = dev->getXRunCount() >= 0 ? d.dropouts : -1;
        s.lastDropout = d.lastDropout;
    }

    return s;
}

juce::String RackEngine::getDriverLabel (int index) const
{
    if (juce::isPositiveAndBelow (index, (int) drivers.size()))
        if (auto* dev = drivers[(size_t) index]->deviceManager.getCurrentAudioDevice())
            return dev->getName();

    return "Driver " + juce::String (index + 1);
}

void RackEngine::updateDriverStats()
{
    const auto now = juce::Time::getMillisecondCounter();

    for (auto& dp : drivers)
    {
        auto& d = *dp;
        const double cpu = d.deviceManager.getCpuUsage();

        if (now - d.peakResetMs > 10000) { d.peakCpu = cpu; d.peakResetMs = now; }
        else                             { d.peakCpu = juce::jmax (d.peakCpu, cpu); }

        if (auto* dev = d.deviceManager.getCurrentAudioDevice())
        {
            const int x = dev->getXRunCount();
            if (x >= 0)
            {
                if (d.lastXRuns >= 0 && x > d.lastXRuns)
                {
                    d.dropouts += x - d.lastXRuns;
                    d.lastDropout = juce::Time::getCurrentTime();
                }
                d.lastXRuns = x;
            }
        }
    }
}

void RackEngine::rebuildDriverLists()
{
    for (auto& dp : drivers)
    {
        std::vector<Rack*> list;

        for (auto& r : racks)
            if (getDriverFor (*r) == dp.get())
            {
                r->graph.setPlayHead (&dp->playHead);
                list.push_back (r.get());
            }

        const juce::SpinLock::ScopedLockType lock (dp->rackLock);
        dp->active.swap (list);
    }
}

// Prepare a rack for its driver's sample rate and buffer size
void RackEngine::configureRack (Rack& r, bool updateLists)
{
    auto* d = getDriverFor (r);

    {
        const juce::SpinLock::ScopedLockType lock (d->rackLock);
        r.configure (d->sampleRate, d->blockSize, d->running);
    }

    if (updateLists)
        rebuildDriverLists();
}

//==============================================================================
// Latency groups: every rack in a group is padded to the slowest one
void RackEngine::timerCallback()
{
    bool changed = false;
    double groupMs[9] {};

    for (auto& r : racks)
    {
        const int g = juce::jlimit (0, 8, r->config.latencyGroup);
        const double ms = 1000.0 * r->graph.getLatencySamples() / juce::jmax (1.0, r->sampleRate);
        if (g > 0)
            groupMs[g] = juce::jmax (groupMs[g], ms);
    }

    for (auto& r : racks)
    {
        const int g = juce::jlimit (0, 8, r->config.latencyGroup);
        const int own = r->graph.getLatencySamples();
        int pad = 0, whenBypassed = 0;

        if (g > 0)
        {
            const int target = (int) std::lround (groupMs[g] * r->sampleRate / 1000.0);
            pad = juce::jmax (0, target - own);
            whenBypassed = target;
        }

        if (r->padSamples.exchange (pad) != pad)            changed = true;
        if (r->bypassDelay.exchange (whenBypassed) != whenBypassed) changed = true;
    }

    if (changed)
        sendChangeMessage();
}

double RackEngine::getRackTotalLatencyMs (int index) const
{
    auto* r = getRack (index);
    if (r == nullptr)
        return 0.0;

    const int samples = r->bypassed ? r->bypassDelay.load() : r->graph.getLatencySamples() + r->padSamples.load();
    return 1000.0 * samples / juce::jmax (1.0, r->sampleRate);
}

double RackEngine::getGroupLatencyMs (int group) const
{
    double ms = 0.0;
    for (auto& r : racks)
        if (r->config.latencyGroup == group && group > 0)
            ms = juce::jmax (ms, 1000.0 * r->graph.getLatencySamples() / juce::jmax (1.0, r->sampleRate));
    return ms;
}

//==============================================================================
// Tempo
void RackEngine::setBpm (double newBpm)
{
    bpm = juce::jlimit (20.0, 300.0, newBpm);
    sendChangeMessage();
}

void RackEngine::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (! tapTimes.isEmpty() && now - tapTimes.getLast() > 2000.0)
        tapTimes.clear(); // a pause starts a new count

    tapTimes.add (now);
    while (tapTimes.size() > 6)
        tapTimes.remove (0);

    if (tapTimes.size() >= 2)
    {
        const double avg = (tapTimes.getLast() - tapTimes.getFirst()) / (tapTimes.size() - 1);
        setBpm (std::round (60000.0 / avg * 10.0) / 10.0);
    }
}

#if LIVERACK_PREVIEW_HOOKS
void RackEngine::previewPrepare (double sr, int bs)
{
    for (auto& d : drivers)
    {
        d->sampleRate = sr;
        d->blockSize = bs;
        d->running = true;
    }

    for (auto& r : racks)
        configureRack (*r, false);

    rebuildDriverLists();
}

void RackEngine::previewProcess (juce::AudioBuffer<float>& in, juce::AudioBuffer<float>& out)
{
    // Each driver has its own output buffers (as real devices do); sum them here
    juce::AudioIODeviceCallbackContext context {};
    juce::AudioBuffer<float> temp (out.getNumChannels(), out.getNumSamples());
    out.clear();
    for (auto& d : drivers)
    {
        d->audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), in.getNumChannels(),
                                             temp.getArrayOfWritePointers(), temp.getNumChannels(),
                                             in.getNumSamples(), context);
        for (int c = 0; c < out.getNumChannels(); ++c)
            out.addFrom (c, 0, temp, c, 0, out.getNumSamples());
    }
}
#endif

//==============================================================================
// Racks
int RackEngine::addRack (const RackConfig& config)
{
    auto rack = std::make_unique<Rack>();
    rack->config = config;

    // A lasting id, so pages keep pointing at this rack
    while (rack->config.id <= 0 || findRackById (rack->config.id) >= 0)
        rack->config.id = 1 + juce::Random::getSystemRandom().nextInt (0x3ffffff0);

    auto* d = getDriverFor (*rack);
    rack->configure (d->sampleRate, d->blockSize, d->running); // prepared before the audio thread sees it
    rack->graph.setPlayHead (&d->playHead);
    racks.push_back (std::move (rack));
    rebuildDriverLists();

    sendChangeMessage();
    return (int) racks.size() - 1;
}

void RackEngine::removeRack (int index)
{
    if (getRack (index) == nullptr)
        return;

    for (auto& id : std::vector<int> (hotPlugins))
        for (int s = 0; s < getNumSlots (index); ++s)
            if (getSlotId (index, s) == id)
                unpinHot (id);

    auto doomed = std::move (racks[(size_t) index]);
    racks.erase (racks.begin() + index);
    rebuildDriverLists(); // the audio threads stop using it here

    doomed.reset();       // plugins are destroyed outside the locks
    sendChangeMessage();
}

void RackEngine::clearAll()
{
    std::vector<std::unique_ptr<Rack>> doomed;
    doomed.swap (racks);
    rebuildDriverLists();
    hotPlugins.clear();
    doomed.clear();
    sendChangeMessage();
}

void RackEngine::setRackConfig (int index, const RackConfig& config)
{
    if (auto* r = getRack (index))
    {
        const auto old = r->config;
        const bool formatChanged = old.stereo != config.stereo || old.monoToStereo != config.monoToStereo;
        const bool needsReconfigure = formatChanged || old.driver != config.driver;

        if (needsReconfigure)
        {
            // Take it off its old driver before touching its graph
            auto* oldDriver = getDriverFor (*r);
            {
                const juce::SpinLock::ScopedLockType lock (oldDriver->rackLock);
                auto& a = oldDriver->active;
                a.erase (std::remove (a.begin(), a.end(), r), a.end());
            }

            r->config = config;

            if (formatChanged)
            {
                relayoutRack (*r);
            }
            else
            {
                auto* d = getDriverFor (*r);
                r->configure (d->sampleRate, d->blockSize, d->running);
                rebuildDriverLists();
            }
        }
        else
        {
            // Routing and labels only: no audio interruption
            auto* d = getDriverFor (*r);
            const juce::SpinLock::ScopedLockType lock (d->rackLock);
            r->config = config;
        }
    }

    sendChangeMessage();
}

RackConfig RackEngine::getRackConfig (int index) const
{
    if (auto* r = getRack (index))
        return r->config;
    return {};
}

void RackEngine::setRackBypassed (int index, bool shouldBeBypassed)
{
    if (auto* r = getRack (index))
    {
        r->bypassed = shouldBeBypassed;
        sendChangeMessage();
    }
}

void RackEngine::setRackRecallSafe (int index, bool safe)
{
    if (auto* r = getRack (index))
    {
        r->config.recallSafe = safe;
        sendChangeMessage();
    }
}

void RackEngine::setRackColour (int index, int colourIndex)
{
    if (auto* r = getRack (index))
    {
        r->config.colourIndex = colourIndex;
        sendChangeMessage();
    }
}

void RackEngine::setRackName (int index, const juce::String& name)
{
    if (auto* r = getRack (index))
    {
        r->config.name = name.isEmpty() ? juce::String ("Rack") : name;
        sendChangeMessage();
    }
}

void RackEngine::setRackLatencyGroup (int index, int group)
{
    if (auto* r = getRack (index))
    {
        r->config.latencyGroup = juce::jlimit (0, 8, group);
        timerCallback();
        sendChangeMessage();
    }
}

int RackEngine::getRackId (int index) const
{
    auto* r = getRack (index);
    return r != nullptr ? r->config.id : 0;
}

int RackEngine::findRackById (int id) const
{
    for (int i = 0; i < (int) racks.size(); ++i)
        if (racks[(size_t) i]->config.id == id && id > 0)
            return i;
    return -1;
}

//==============================================================================
// Pages
void RackEngine::setPages (std::vector<RackPage> newPages)
{
    pages = std::move (newPages);
    currentPage = juce::jlimit (-1, (int) pages.size() - 1, currentPage);
    sendChangeMessage();
}

void RackEngine::setCurrentPage (int page)
{
    page = juce::jlimit (-1, (int) pages.size() - 1, page);
    if (page != currentPage)
    {
        currentPage = page;
        sendChangeMessage();
    }
}

std::vector<int> RackEngine::getVisibleRacks() const
{
    std::vector<int> v;

    if (! juce::isPositiveAndBelow (currentPage, (int) pages.size()))
    {
        for (int i = 0; i < (int) racks.size(); ++i)
            v.push_back (i);
        return v;
    }

    for (int id : pages[(size_t) currentPage].rackIds)
        if (const int r = findRackById (id); r >= 0)
            v.push_back (r);

    return v;
}

bool RackEngine::isRackOnPage (int rack, int page) const
{
    if (! juce::isPositiveAndBelow (page, (int) pages.size()))
        return page < 0; // everything is on All
    const auto& ids = pages[(size_t) page].rackIds;
    return std::find (ids.begin(), ids.end(), getRackId (rack)) != ids.end();
}

void RackEngine::setRackOnPage (int rack, int page, bool on)
{
    if (! juce::isPositiveAndBelow (page, (int) pages.size()) || getRack (rack) == nullptr)
        return;

    auto& ids = pages[(size_t) page].rackIds;
    const int id = getRackId (rack);
    ids.erase (std::remove (ids.begin(), ids.end(), id), ids.end());
    if (on)
        ids.push_back (id);
    sendChangeMessage();
}

//==============================================================================
// Mono-to-stereo racks
void RackEngine::detachRack (Rack& r)
{
    for (auto& d : drivers)
    {
        const juce::SpinLock::ScopedLockType lock (d->rackLock);
        auto& a = d->active;
        a.erase (std::remove (a.begin(), a.end(), &r), a.end());
    }
}

// Walk the chain: each plugin gets the width coming into it. In a mono-to-
// stereo rack, the stereo point (a Mono/Stereo version, or a plugin marked as
// the point) turns it stereo; everything after it runs stereo.
void RackEngine::relayoutRack (Rack& r)
{
    detachRack (r);
    r.graph.releaseResources();

    int width = r.config.numInputs();

    for (auto& node : r.slots)
    {
        auto* p = dynamic_cast<juce::AudioPluginInstance*> (node->getProcessor());
        if (p == nullptr)
            continue;

        juce::PluginDescription desc;
        if (auto xml = juce::parseXML (node->properties["liverackDescription"].toString()))
            desc.loadFromXml (*xml);

        const bool point = (bool) node->properties["stereoPoint"] || isMonoToStereoVersion (desc.name);
        const int outs = (r.config.isMonoToStereo() && width == 1 && point) ? 2 : width;

        requestLayout (*p, width, outs);
        width = juce::jmax (1, p->getMainBusNumOutputChannels());
    }

    auto* d = getDriverFor (r);
    r.configure (d->sampleRate, d->blockSize, d->running);
    rebuildDriverLists();
}

int RackEngine::getSlotOutputChannels (int index, int slot) const
{
    auto* r = getRack (index);
    if (r == nullptr || ! juce::isPositiveAndBelow (slot, (int) r->slots.size()))
        return 0;
    return r->slots[(size_t) slot]->getProcessor()->getMainBusNumOutputChannels();
}

int RackEngine::getStereoPoint (int index) const
{
    auto* r = getRack (index);
    if (r == nullptr || ! r->config.isMonoToStereo())
        return -1;

    for (int i = 0; i < (int) r->slots.size(); ++i)
    {
        auto* p = r->slots[(size_t) i]->getProcessor();
        if (p->getMainBusNumInputChannels() == 1 && p->getMainBusNumOutputChannels() == 2)
            return i;
        if (p->getMainBusNumInputChannels() == 2)
            return i; // a stereo-only plugin is where it goes stereo
    }
    return -1;
}

void RackEngine::setStereoPoint (int index, int slot)
{
    auto* r = getRack (index);
    if (r == nullptr || ! r->config.isMonoToStereo())
        return;

    for (int i = 0; i < (int) r->slots.size(); ++i)
        r->slots[(size_t) i]->properties.set ("stereoPoint", i == slot);

    relayoutRack (*r);
    sendChangeMessage();
}

bool RackEngine::isSlotIsolated (int rack, int slot) const
{
    return dynamic_cast<IsolatedPlugin*> (getPlugin (rack, slot)) != nullptr;
}

bool RackEngine::setSlotIsolated (int rack, int slot, bool isolated, juce::String& error)
{
    if (getPlugin (rack, slot) == nullptr || isSlotIsolated (rack, slot) == isolated)
        return true;

    return replacePlugin (rack, slot, getSlotDescription (rack, slot), error, isolated ? 1 : 0);
}

void RackEngine::crashIsolatedSlot (int rack, int slot)
{
    if (auto* p = dynamic_cast<IsolatedPlugin*> (getPlugin (rack, slot)))
        p->crashHelper();
}

bool RackEngine::replacePlugin (int index, int slot, const juce::PluginDescription& desc, juce::String& error, int isolate)
{
    auto* r = getRack (index);
    auto* old = getPlugin (index, slot);
    if (r == nullptr || old == nullptr)
    {
        error = "Nothing to replace";
        return false;
    }

    const bool isolated = isolate < 0 ? isSlotIsolated (index, slot) : isolate > 0;
    auto instance = createInstance (formatManager, desc, isolated, r->sampleRate, r->blockSize, error);
    if (instance == nullptr)
    {
        if (error.isEmpty()) error = "Couldn't load " + desc.name;
        return false;
    }

    juce::MemoryBlock state;
    old->getStateInformation (state);
    instance->setStateInformation (state.getData(), (int) state.getSize());

    auto& oldNode = r->slots[(size_t) slot];
    const auto props = oldNode->properties;
    const bool bypassed = oldNode->isBypassed();

    detachRack (*r);
    auto node = r->graph.addNode (std::move (instance));
    if (node == nullptr)
    {
        rebuildDriverLists();
        error = "The audio graph refused the plugin";
        return false;
    }

    node->properties = props;
    if (auto xml = desc.createXml())
        node->properties.set ("liverackDescription", xml->toString());
    node->setBypassed (bypassed);

    const auto oldId = oldNode->nodeID;
    r->slots[(size_t) slot] = node;
    r->graph.removeNode (oldId);

    relayoutRack (*r);
    sendChangeMessage();
    return true;
}

float RackEngine::consumePeak (int index, bool output, int channel)
{
    auto* r = getRack (index);
    if (r == nullptr || ! juce::isPositiveAndBelow (channel, 2))
        return 0.0f;

    return r->peaks[output ? 1 : 0][channel].exchange (0.0f);
}

bool RackEngine::isRackBypassed (int index) const
{
    auto* r = getRack (index);
    return r != nullptr && r->bypassed.load();
}

int RackEngine::getRackLatencySamples (int index) const
{
    auto* r = getRack (index);
    return r != nullptr ? r->graph.getLatencySamples() : 0;
}

//==============================================================================
// Plugins
// Ids are random, so ones stored in snapshots, User Keys and MIDI mappings
// keep pointing at the same plugin across sessions
int RackEngine::attachSlotId (juce::AudioProcessorGraph::Node& node, int preferredId)
{
    int r, s;
    int id = preferredId;
    while (id <= 0 || findSlot (id, r, s))
        id = 1 + juce::Random::getSystemRandom().nextInt (0x3ffffff0);

    node.properties.set ("slotId", id);
    node.properties.set ("inScope", true);
    return id;
}

bool RackEngine::addPlugin (int index, const juce::PluginDescription& desc, juce::String& error, int slotId, bool isolated)
{
    auto* r = getRack (index);

    if (r == nullptr)
    {
        error = "No rack selected";
        return false;
    }

    auto instance = createInstance (formatManager, desc, isolated, r->sampleRate, r->blockSize, error);

    if (instance == nullptr)
    {
        if (error.isEmpty())
            error = "Unknown error creating plugin";
        return false;
    }

    // Channels at the end of the chain decide the new plugin's layout
    int width = r->config.numInputs();
    for (auto& s : r->slots)
        width = juce::jmax (1, s->getProcessor()->getMainBusNumOutputChannels());

    const bool split = r->config.isMonoToStereo() && width == 1 && isMonoToStereoVersion (desc.name);
    requestLayout (*instance, width, split ? 2 : width);

    auto node = r->graph.addNode (std::move (instance));

    if (node == nullptr)
    {
        error = "The audio graph refused the plugin";
        return false;
    }

    // Remember exactly what this was loaded from: a loaded plugin's own
    // description can differ (e.g. a file path inside the plugin bundle)
    if (auto xml = desc.createXml())
        node->properties.set ("liverackDescription", xml->toString());

    attachSlotId (*node, slotId);
    r->slots.push_back (node);
    r->addTap (juce::jmax (1, node->getProcessor()->getMainBusNumOutputChannels()));
    r->rebuildConnections();
    sendChangeMessage();
    return true;
}

void RackEngine::removePlugin (int index, int slot)
{
    auto* r = getRack (index);

    if (r == nullptr || ! juce::isPositiveAndBelow (slot, (int) r->slots.size()))
        return;

    unpinHot (getSlotId (index, slot));

    auto id = r->slots[(size_t) slot]->nodeID;
    r->slots.erase (r->slots.begin() + slot);
    r->graph.removeNode (id);

    if (r->config.isMonoToStereo())
    {
        relayoutRack (*r);
    }
    else
    {
        // Taps follow each slot's width
        for (auto& t : r->taps) r->graph.removeNode (t->nodeID);
        r->taps.clear();
        for (auto& s : r->slots) r->addTap (juce::jmax (1, s->getProcessor()->getMainBusNumOutputChannels()));
        r->rebuildConnections();
    }
    sendChangeMessage();
}

void RackEngine::moveSlot (int index, int from, int to)
{
    auto* r = getRack (index);

    if (r == nullptr
        || ! juce::isPositiveAndBelow (from, (int) r->slots.size())
        || ! juce::isPositiveAndBelow (to,   (int) r->slots.size()))
        return;

    std::swap (r->slots[(size_t) from], r->slots[(size_t) to]);
    if (r->config.isMonoToStereo()) relayoutRack (*r);
    else                            r->rebuildConnections();
    sendChangeMessage();
}

void RackEngine::moveSlotTo (int index, int from, int toIndex)
{
    auto* r = getRack (index);

    if (r == nullptr || ! juce::isPositiveAndBelow (from, (int) r->slots.size()))
        return;

    toIndex = juce::jlimit (0, (int) r->slots.size() - 1, toIndex);
    if (toIndex == from)
        return;

    auto node = r->slots[(size_t) from];
    r->slots.erase (r->slots.begin() + from);
    r->slots.insert (r->slots.begin() + toIndex, node);
    if (r->config.isMonoToStereo()) relayoutRack (*r);
    else                            r->rebuildConnections();
    sendChangeMessage();
}

bool RackEngine::copyPlugin (int fromRack, int fromSlot, int toRack, int toIndex, juce::String& error)
{
    auto* source = getPlugin (fromRack, fromSlot);

    if (source == nullptr || getRack (toRack) == nullptr)
    {
        error = "Nothing to copy";
        return false;
    }

    const auto desc = getSlotDescription (fromRack, fromSlot);
    juce::MemoryBlock state;
    source->getStateInformation (state);
    const bool bypassed = isSlotBypassed (fromRack, fromSlot);
    const bool inScope = isSlotInScope (fromRack, fromSlot);

    if (! addPlugin (toRack, desc, error))
        return false;

    const int newSlot = getNumSlots (toRack) - 1;
    if (auto* copy = getPlugin (toRack, newSlot))
        copy->setStateInformation (state.getData(), (int) state.getSize());
    setSlotBypassed (toRack, newSlot, bypassed);
    setSlotInScope (toRack, newSlot, inScope);
    moveSlotTo (toRack, newSlot, toIndex);
    return true;
}

float RackEngine::consumeSlotOutputPeak (int index, int slot)
{
    auto* r = getRack (index);

    if (r == nullptr || ! juce::isPositiveAndBelow (slot, (int) r->taps.size()))
        return 0.0f;

    if (auto* tap = dynamic_cast<LevelTap*> (r->taps[(size_t) slot]->getProcessor()))
        return tap->peak.exchange (0.0f);

    return 0.0f;
}

void RackEngine::setSlotBypassed (int index, int slot, bool shouldBeBypassed)
{
    auto* r = getRack (index);

    if (r != nullptr && juce::isPositiveAndBelow (slot, (int) r->slots.size()))
    {
        r->slots[(size_t) slot]->setBypassed (shouldBeBypassed);
        sendChangeMessage();
    }
}

bool RackEngine::isSlotBypassed (int index, int slot) const
{
    auto* r = getRack (index);
    return r != nullptr && juce::isPositiveAndBelow (slot, (int) r->slots.size())
           && r->slots[(size_t) slot]->isBypassed();
}

int RackEngine::getNumSlots (int index) const
{
    auto* r = getRack (index);
    return r != nullptr ? (int) r->slots.size() : 0;
}

int RackEngine::getSlotId (int index, int slot) const
{
    auto* r = getRack (index);
    if (r == nullptr || ! juce::isPositiveAndBelow (slot, (int) r->slots.size()))
        return 0;
    return (int) r->slots[(size_t) slot]->properties["slotId"];
}

bool RackEngine::findSlot (int slotId, int& rackOut, int& slotOut) const
{
    if (slotId <= 0)
        return false;

    for (int ri = 0; ri < (int) racks.size(); ++ri)
        for (int s = 0; s < (int) racks[(size_t) ri]->slots.size(); ++s)
            if ((int) racks[(size_t) ri]->slots[(size_t) s]->properties["slotId"] == slotId)
            {
                rackOut = ri;
                slotOut = s;
                return true;
            }

    return false;
}

juce::PluginDescription RackEngine::getSlotDescription (int index, int slot) const
{
    auto* r = getRack (index);
    juce::PluginDescription desc;

    if (r == nullptr || ! juce::isPositiveAndBelow (slot, (int) r->slots.size()))
        return desc;

    auto& node = r->slots[(size_t) slot];

    if (auto xml = juce::parseXML (node->properties["liverackDescription"].toString()))
        if (desc.loadFromXml (*xml))
            return desc;

    if (auto* plugin = dynamic_cast<juce::AudioPluginInstance*> (node->getProcessor()))
        return plugin->getPluginDescription();

    return desc;
}

juce::AudioPluginInstance* RackEngine::getPlugin (int index, int slot) const
{
    auto* r = getRack (index);

    if (r == nullptr || ! juce::isPositiveAndBelow (slot, (int) r->slots.size()))
        return nullptr;

    return dynamic_cast<juce::AudioPluginInstance*> (r->slots[(size_t) slot]->getProcessor());
}

//==============================================================================
// Scope and Hot Plugins
bool RackEngine::isSlotInScope (int index, int slot) const
{
    auto* r = getRack (index);
    if (r == nullptr || ! juce::isPositiveAndBelow (slot, (int) r->slots.size()))
        return false;

    const auto& v = r->slots[(size_t) slot]->properties["inScope"];
    return v.isVoid() || (bool) v;
}

void RackEngine::setSlotInScope (int index, int slot, bool in)
{
    auto* r = getRack (index);
    if (r != nullptr && juce::isPositiveAndBelow (slot, (int) r->slots.size()))
    {
        r->slots[(size_t) slot]->properties.set ("inScope", in);
        sendChangeMessage();
    }
}

void RackEngine::setScope (const SnapshotScope& s)
{
    scope = s;
    sendChangeMessage();
}

void RackEngine::setHotPlugins (std::vector<int> ids)
{
    std::vector<int> clean;
    for (int id : ids)
    {
        int r, s;
        if (findSlot (id, r, s) && std::find (clean.begin(), clean.end(), id) == clean.end()
            && (int) clean.size() < maxHotPlugins)
            clean.push_back (id);
    }

    hotPlugins.swap (clean);
    sendChangeMessage();
}

bool RackEngine::isHot (int slotId) const
{
    return slotId > 0 && std::find (hotPlugins.begin(), hotPlugins.end(), slotId) != hotPlugins.end();
}

void RackEngine::pinHot (int slotId)
{
    if (slotId <= 0 || isHot (slotId) || (int) hotPlugins.size() >= maxHotPlugins)
        return;

    hotPlugins.push_back (slotId);
    sendChangeMessage();
}

void RackEngine::unpinHot (int slotId)
{
    auto it = std::find (hotPlugins.begin(), hotPlugins.end(), slotId);
    if (it != hotPlugins.end())
    {
        hotPlugins.erase (it);
        sendChangeMessage();
    }
}

//==============================================================================
// Snapshots
static void writeChannel (juce::XmlElement& x, const char* name, int ch) { x.setAttribute (name, ch + 1); } // 0 = none
static int  readChannel  (const juce::XmlElement& x, const char* name, int fallback)
{
    return x.hasAttribute (name) ? x.getIntAttribute (name) - 1 : fallback;
}

std::unique_ptr<juce::XmlElement> RackEngine::createSnapshot() const
{
    auto xml = std::make_unique<juce::XmlElement> ("LIVERACK");
    xml->setAttribute ("version", 3);
    xml->setAttribute ("bpm", bpm.load());

    writeScope (*xml->createNewChildElement ("SCOPE"), scope);

    for (int rackIndex = 0; rackIndex < (int) racks.size(); ++rackIndex)
    {
        auto& r = racks[(size_t) rackIndex];
        auto& c = r->config;
        auto* rackXml = xml->createNewChildElement ("RACK");
        rackXml->setAttribute ("name", c.name);
        rackXml->setAttribute ("stereo", c.stereo);
        rackXml->setAttribute ("monoToStereo", c.monoToStereo);
        rackXml->setAttribute ("id", c.id);
        rackXml->setAttribute ("driver", c.driver);
        writeChannel (*rackXml, "input", c.input[0]);    // 1-based in the file, 0 = none
        writeChannel (*rackXml, "inputR", c.input[1]);
        writeChannel (*rackXml, "output", c.output[0]);
        writeChannel (*rackXml, "outputR", c.output[1]);
        rackXml->setAttribute ("consoleChannel", c.consoleChannel);
        rackXml->setAttribute ("console", c.console);
        rackXml->setAttribute ("recallSafe", c.recallSafe);
        rackXml->setAttribute ("colour", c.colourIndex);
        rackXml->setAttribute ("latencyGroup", c.latencyGroup);
        rackXml->setAttribute ("bypassed", r->bypassed.load());

        for (int i = 0; i < (int) r->slots.size(); ++i)
        {
            auto& s = r->slots[(size_t) i];
            auto* plugin = dynamic_cast<juce::AudioPluginInstance*> (s->getProcessor());
            if (plugin == nullptr)
                continue;

            auto* slotXml = rackXml->createNewChildElement ("SLOT");
            slotXml->setAttribute ("bypassed", s->isBypassed());
            slotXml->setAttribute ("inScope", isSlotInScope (rackIndex, i));
            slotXml->setAttribute ("id", getSlotId (rackIndex, i));
            if ((bool) s->properties["stereoPoint"])
                slotXml->setAttribute ("stereoPoint", true);
            if (isSlotIsolated (rackIndex, i))
                slotXml->setAttribute ("isolated", true);
            slotXml->addChildElement (getSlotDescription (rackIndex, i).createXml().release());

            juce::MemoryBlock state;
            plugin->getStateInformation (state);
            slotXml->createNewChildElement ("STATE")->addTextElement (state.toBase64Encoding());
        }
    }

    auto* hot = xml->createNewChildElement ("HOT");
    for (int id : hotPlugins)
    {
        int ri, si;
        if (findSlot (id, ri, si))
        {
            auto* p = hot->createNewChildElement ("PIN");
            p->setAttribute ("rack", ri);
            p->setAttribute ("slot", si);
        }
    }

    return xml;
}

static void writeScope (juce::XmlElement& sc, const SnapshotScope& s)
{
    sc.setAttribute ("pluginSettings", s.pluginSettings);
    sc.setAttribute ("pluginBypass", s.pluginBypass);
    sc.setAttribute ("rackBypass", s.rackBypass);
    sc.setAttribute ("bpm", s.bpm);
}

static SnapshotScope scopeFromXml (const juce::XmlElement& sc)
{
    SnapshotScope s;
    s.pluginSettings = sc.getBoolAttribute ("pluginSettings", true);
    s.pluginBypass   = sc.getBoolAttribute ("pluginBypass", true);
    s.rackBypass     = sc.getBoolAttribute ("rackBypass", true);
    s.bpm            = sc.getBoolAttribute ("bpm", true);
    return s;
}

void RackEngine::readScope (const juce::XmlElement& xml)
{
    if (auto* sc = xml.getChildByName ("SCOPE"))
    {
        scope = scopeFromXml (*sc);
    }
    else
    {
        scope = {};
    }
}

void RackEngine::readHot (const juce::XmlElement& xml)
{
    std::vector<int> ids;
    if (auto* hot = xml.getChildByName ("HOT"))
        for (auto* p : hot->getChildWithTagNameIterator ("PIN"))
        {
            const int id = getSlotId (p->getIntAttribute ("rack", -1), p->getIntAttribute ("slot", -1));
            if (id > 0)
                ids.push_back (id);
        }

    setHotPlugins (ids);
}

static RackConfig configFromXml (const juce::XmlElement& rackXml)
{
    RackConfig config;
    config.name        = rackXml.getStringAttribute ("name", "Rack");
    config.stereo      = rackXml.getBoolAttribute ("stereo", false);
    config.monoToStereo = rackXml.getBoolAttribute ("monoToStereo", false);
    config.id          = rackXml.getIntAttribute ("id", 0);
    config.driver      = juce::jmax (0, rackXml.getIntAttribute ("driver", 0));
    config.input[0]    = readChannel (rackXml, "input", 0);
    config.input[1]    = readChannel (rackXml, "inputR", config.input[0] + 1);
    config.output[0]   = readChannel (rackXml, "output", 0);
    config.output[1]   = readChannel (rackXml, "outputR", config.output[0] + 1);
    config.consoleChannel = juce::jmax (0, rackXml.getIntAttribute ("consoleChannel", 0));
    config.console     = juce::jmax (0, rackXml.getIntAttribute ("console", 0));
    config.recallSafe  = rackXml.getBoolAttribute ("recallSafe", false);
    config.colourIndex = rackXml.getIntAttribute ("colour", -1);
    config.latencyGroup = juce::jlimit (0, 8, rackXml.getIntAttribute ("latencyGroup", 0));
    return config;
}

bool RackEngine::loadRig (const juce::XmlElement& xml, juce::String& error)
{
    if (! xml.hasTagName ("LIVERACK"))
    {
        error = "This isn't a LiveRack show file.";
        return false;
    }

    if (snapshotMatchesCurrentLayout (xml))
    {
        applyShowInPlace (xml);
        error.clear();
        return true;
    }

    clearAll();
    juce::StringArray failures;

    auto loadSlots = [this, &failures] (int rackIndex, const juce::XmlElement& parent)
    {
        for (auto* slotXml : parent.getChildWithTagNameIterator ("SLOT"))
        {
            juce::PluginDescription desc;
            auto* descXml = slotXml->getChildByName ("PLUGIN");

            if (descXml == nullptr || ! desc.loadFromXml (*descXml))
            {
                failures.add ("(unreadable plugin entry)");
                continue;
            }

            juce::String err;
            if (! addPlugin (rackIndex, desc, err, slotXml->getIntAttribute ("id", 0), slotXml->getBoolAttribute ("isolated", false)))
            {
                failures.add (desc.name + ": " + err);
                continue;
            }

            const int slot = getNumSlots (rackIndex) - 1;

            if (auto* stateXml = slotXml->getChildByName ("STATE"))
            {
                juce::MemoryBlock state;
                if (state.fromBase64Encoding (stateXml->getAllSubText().trim()))
                    getPlugin (rackIndex, slot)->setStateInformation (state.getData(), (int) state.getSize());
            }

            setSlotBypassed (rackIndex, slot, slotXml->getBoolAttribute ("bypassed", false));
            setSlotInScope (rackIndex, slot, slotXml->getBoolAttribute ("inScope", true));

            if (slotXml->getBoolAttribute ("stereoPoint", false))
                setStereoPoint (rackIndex, slot);
        }
    };

    if (xml.getChildByName ("RACK") == nullptr)
    {
        // Version 1 file: a single stereo rack on channels 1-2
        RackConfig config;
        config.name = "Rack 1";
        config.stereo = true;
        loadSlots (addRack (config), xml);
    }
    else
    {
        for (auto* rackXml : xml.getChildWithTagNameIterator ("RACK"))
        {
            auto config = configFromXml (*rackXml);
            if (config.driver >= getNumDrivers())
                config.driver = 0;

            const int index = addRack (config);
            setRackBypassed (index, rackXml->getBoolAttribute ("bypassed", false));
            loadSlots (index, *rackXml);
        }
    }

    readScope (xml);
    readHot (xml);
    if (xml.hasAttribute ("bpm"))
        bpm = juce::jlimit (20.0, 300.0, xml.getDoubleAttribute ("bpm", 120.0));

    timerCallback();
    sendChangeMessage();
    error = failures.joinIntoString ("\n");
    return failures.isEmpty();
}

// Same racks (count and format) with the same plugins in the same order
bool RackEngine::snapshotMatchesCurrentLayout (const juce::XmlElement& xml, bool checkIds) const
{
    if (! xml.hasTagName ("LIVERACK") || xml.getChildByName ("RACK") == nullptr)
        return false;

    int rackIndex = 0;

    for (auto* rackXml : xml.getChildWithTagNameIterator ("RACK"))
    {
        auto* r = getRack (rackIndex++);

        if (r == nullptr || r->config.stereo != rackXml->getBoolAttribute ("stereo", false)
            || r->config.monoToStereo != rackXml->getBoolAttribute ("monoToStereo", false)
            || (checkIds && rackXml->getIntAttribute ("id", 0) != r->config.id))
            return false;

        int slot = 0;

        for (auto* slotXml : rackXml->getChildWithTagNameIterator ("SLOT"))
        {
            if (slot >= (int) r->slots.size())
                return false;

            juce::PluginDescription desc;
            auto* descXml = slotXml->getChildByName ("PLUGIN");

            if (descXml == nullptr || ! desc.loadFromXml (*descXml)
                || getSlotDescription (rackIndex - 1, slot).createIdentifierString() != desc.createIdentifierString()
                || (checkIds && slotXml->getIntAttribute ("id", 0) != getSlotId (rackIndex - 1, slot))
                || (checkIds && slotXml->getBoolAttribute ("isolated", false) != isSlotIsolated (rackIndex - 1, slot)))
                return false;

            ++slot;
        }

        if (slot != (int) r->slots.size())
            return false;
    }

    return rackIndex == getNumRacks();
}

// Same racks and plugins (and ids) as the file: everything in it is applied
// without reloading a plugin, so audio keeps running
void RackEngine::applyShowInPlace (const juce::XmlElement& xml)
{
    readScope (xml);
    int rackIndex = 0;

    for (auto* rackXml : xml.getChildWithTagNameIterator ("RACK"))
    {
        const int ri = rackIndex++;
        auto* r = getRack (ri);

        if (r == nullptr)
            continue;

        auto stored = configFromXml (*rackXml);
        stored.id = r->config.id;
        if (stored.driver >= getNumDrivers())
            stored.driver = r->config.driver;

        setRackConfig (ri, stored);
        r->bypassed = rackXml->getBoolAttribute ("bypassed", false);

        int slot = 0;

        for (auto* slotXml : rackXml->getChildWithTagNameIterator ("SLOT"))
        {
            auto& node = r->slots[(size_t) slot++];
            node->properties.set ("inScope", slotXml->getBoolAttribute ("inScope", true));

            if (auto* stateXml = slotXml->getChildByName ("STATE"))
            {
                juce::MemoryBlock state;
                if (state.fromBase64Encoding (stateXml->getAllSubText().trim()))
                    node->getProcessor()->setStateInformation (state.getData(), (int) state.getSize());
            }

            node->setBypassed (slotXml->getBoolAttribute ("bypassed", false));
        }
    }

    readHot (xml);

    if (xml.hasAttribute ("bpm"))
        bpm = juce::jlimit (20.0, 300.0, xml.getDoubleAttribute ("bpm", 120.0));

    timerCallback();
    sendChangeMessage();
}

//==============================================================================
// Snapshots stored in the show
std::unique_ptr<juce::XmlElement> RackEngine::captureSnapshotData() const
{
    auto x = std::make_unique<juce::XmlElement> ("SNAPSHOT");
    x->setAttribute ("bpm", bpm.load());
    writeScope (*x->createNewChildElement ("SCOPE"), scope);

    for (int ri = 0; ri < (int) racks.size(); ++ri)
    {
        auto& r = racks[(size_t) ri];
        auto* rx = x->createNewChildElement ("RACK");
        rx->setAttribute ("id", r->config.id);
        rx->setAttribute ("bypassed", r->bypassed.load());

        for (int si = 0; si < (int) r->slots.size(); ++si)
        {
            auto& node = r->slots[(size_t) si];
            auto* plugin = dynamic_cast<juce::AudioPluginInstance*> (node->getProcessor());
            if (plugin == nullptr)
                continue;

            auto* sx = rx->createNewChildElement ("SLOT");
            sx->setAttribute ("id", getSlotId (ri, si));
            sx->setAttribute ("bypassed", node->isBypassed());
            sx->setAttribute ("inScope", isSlotInScope (ri, si));

            juce::MemoryBlock state;
            plugin->getStateInformation (state);
            sx->createNewChildElement ("STATE")->addTextElement (state.toBase64Encoding());
        }
    }

    return x;
}

const ShowSnapshot* RackEngine::getSnapshotInfo (int number) const
{
    for (auto& s : snapshots)
        if (s.number == number)
            return &s;
    return nullptr;
}

static void putSnapshot (std::vector<ShowSnapshot>& list, ShowSnapshot snap)
{
    for (auto& s : list)
        if (s.number == snap.number)
        {
            if (snap.name.isEmpty())
                snap.name = s.name;
            s = std::move (snap);
            return;
        }

    auto at = std::find_if (list.begin(), list.end(), [&] (const ShowSnapshot& s) { return s.number > snap.number; });
    list.insert (at, std::move (snap));
}

void RackEngine::storeSnapshot (int number, const juce::String& name)
{
    ShowSnapshot snap;
    snap.number = juce::jmax (0, number);
    snap.name = name.trim();
    snap.data = std::shared_ptr<juce::XmlElement> (captureSnapshotData().release());
    putSnapshot (snapshots, std::move (snap));
    sendChangeMessage();
}

bool RackEngine::recallSnapshot (int number, juce::String& note)
{
    note.clear();
    auto* snap = getSnapshotInfo (number);
    if (snap == nullptr || snap->data == nullptr)
        return false;

    const auto data = snap->data; // keeps it alive if the list changes
    const auto sc = getScopeFor (number);
    int missing = 0;

    for (int ri = 0; ri < (int) racks.size(); ++ri)
    {
        auto& r = racks[(size_t) ri];
        if (r->config.recallSafe)
            continue;

        const juce::XmlElement* rx = nullptr;
        for (auto* e : data->getChildWithTagNameIterator ("RACK"))
            if (e->getIntAttribute ("id") == r->config.id)
                rx = e;

        if (rx == nullptr)
        {
            missing += (int) r->slots.size();
            continue;
        }

        if (sc.rackBypass)
            r->bypassed = rx->getBoolAttribute ("bypassed", false);

        for (int si = 0; si < (int) r->slots.size(); ++si)
        {
            const juce::XmlElement* sx = nullptr;
            const int id = getSlotId (ri, si);
            for (auto* e : rx->getChildWithTagNameIterator ("SLOT"))
                if (e->getIntAttribute ("id") == id)
                    sx = e;

            if (sx == nullptr)
            {
                ++missing;
                continue;
            }

            if (! sx->getBoolAttribute ("inScope", isSlotInScope (ri, si)))
                continue;

            auto& node = r->slots[(size_t) si];
            // A plugin's own bypass parameter is part of its state: keep the
            // current bypass when the scope leaves it alone
            const bool bypassWas = node->isBypassed();

            if (sc.pluginSettings)
                if (auto* stateXml = sx->getChildByName ("STATE"))
                {
                    juce::MemoryBlock state;
                    if (state.fromBase64Encoding (stateXml->getAllSubText().trim()))
                        node->getProcessor()->setStateInformation (state.getData(), (int) state.getSize());
                }

            node->setBypassed (sc.pluginBypass ? sx->getBoolAttribute ("bypassed", false) : bypassWas);
        }
    }

    if (sc.bpm && data->hasAttribute ("bpm"))
        bpm = juce::jlimit (20.0, 300.0, data->getDoubleAttribute ("bpm", 120.0));

    if (missing > 0)
        note = juce::String (missing) + (missing == 1 ? " plugin isn't" : " plugins aren't") + " in this snapshot, left as is";

    timerCallback();
    sendChangeMessage();
    return true;
}

static juce::XmlElement* findSnapshotSlot (juce::XmlElement& data, int rackId, int slotId)
{
    for (auto* rx : data.getChildWithTagNameIterator ("RACK"))
        if (rx->getIntAttribute ("id") == rackId)
            for (auto* sx : rx->getChildWithTagNameIterator ("SLOT"))
                if (sx->getIntAttribute ("id") == slotId)
                    return sx;
    return nullptr;
}

SnapshotScope RackEngine::getScopeFor (int snapshot) const
{
    auto* snap = getSnapshotInfo (snapshot);
    if (snap == nullptr || snap->data == nullptr)
        return scope;
    auto* sc = snap->data->getChildByName ("SCOPE");
    return sc != nullptr ? scopeFromXml (*sc) : scope;   // 0.13.0 snapshots: the show's scope
}

void RackEngine::setScopeFor (int snapshot, const SnapshotScope& s)
{
    auto* snap = getSnapshotInfo (snapshot);
    if (snap == nullptr || snap->data == nullptr)
    {
        setScope (s);
        return;
    }
    if (auto* old = snap->data->getChildByName ("SCOPE"))
        snap->data->removeChildElement (old, true);
    writeScope (*snap->data->createNewChildElement ("SCOPE"), s);
    sendChangeMessage();
}

bool RackEngine::snapshotHasSlot (int snapshot, int rack, int slot) const
{
    auto* snap = getSnapshotInfo (snapshot);
    if (snap == nullptr || snap->data == nullptr)
        return true;
    return findSnapshotSlot (*snap->data, getRackId (rack), getSlotId (rack, slot)) != nullptr;
}

bool RackEngine::isSlotInScopeFor (int snapshot, int rack, int slot) const
{
    auto* snap = getSnapshotInfo (snapshot);
    if (snap == nullptr || snap->data == nullptr)
        return isSlotInScope (rack, slot);
    auto* sx = findSnapshotSlot (*snap->data, getRackId (rack), getSlotId (rack, slot));
    return sx != nullptr && sx->getBoolAttribute ("inScope", isSlotInScope (rack, slot));
}

void RackEngine::setSlotInScopeFor (int snapshot, int rack, int slot, bool in)
{
    auto* snap = getSnapshotInfo (snapshot);
    if (snap == nullptr || snap->data == nullptr)
    {
        setSlotInScope (rack, slot, in);
        return;
    }
    if (auto* sx = findSnapshotSlot (*snap->data, getRackId (rack), getSlotId (rack, slot)))
    {
        sx->setAttribute ("inScope", in);
        sendChangeMessage();
    }
}

void RackEngine::renameSnapshot (int number, const juce::String& name)
{
    for (auto& s : snapshots)
        if (s.number == number)
            s.name = name.trim();
    sendChangeMessage();
}

void RackEngine::deleteSnapshot (int number)
{
    snapshots.erase (std::remove_if (snapshots.begin(), snapshots.end(),
                                     [number] (const ShowSnapshot& s) { return s.number == number; }),
                     snapshots.end());
    sendChangeMessage();
}

bool RackEngine::importSceneFile (const juce::XmlElement& full, int number, const juce::String& name, juce::String& error)
{
    if (! snapshotMatchesCurrentLayout (full, false))
    {
        error = "its racks and plugins don't match this show";
        return false;
    }

    auto x = std::make_unique<juce::XmlElement> ("SNAPSHOT");
    x->setAttribute ("bpm", full.getDoubleAttribute ("bpm", bpm.load()));
    writeScope (*x->createNewChildElement ("SCOPE"),
                full.getChildByName ("SCOPE") != nullptr ? scopeFromXml (*full.getChildByName ("SCOPE")) : scope);
    int ri = 0;

    for (auto* rackXml : full.getChildWithTagNameIterator ("RACK"))
    {
        auto* rx = x->createNewChildElement ("RACK");
        rx->setAttribute ("id", getRackId (ri));
        rx->setAttribute ("bypassed", rackXml->getBoolAttribute ("bypassed", false));
        int si = 0;

        for (auto* slotXml : rackXml->getChildWithTagNameIterator ("SLOT"))
        {
            auto* sx = rx->createNewChildElement ("SLOT");
            sx->setAttribute ("id", getSlotId (ri, si++));
            sx->setAttribute ("bypassed", slotXml->getBoolAttribute ("bypassed", false));
            sx->setAttribute ("inScope", slotXml->getBoolAttribute ("inScope", true));
            if (auto* st = slotXml->getChildByName ("STATE"))
                sx->addChildElement (new juce::XmlElement (*st));
        }
        ++ri;
    }

    ShowSnapshot snap;
    snap.number = number;
    snap.name = name;
    snap.data = std::shared_ptr<juce::XmlElement> (x.release());
    putSnapshot (snapshots, std::move (snap));
    sendChangeMessage();
    error.clear();
    return true;
}

//==============================================================================
// The show
static bool isEngineSection (const juce::String& tag)
{
    return tag == "SCOPE" || tag == "RACK" || tag == "HOT" || tag == "PAGES" || tag == "SNAPSHOTS"
        || tag == "SLOT" || tag == "PLUGIN" || tag == "STATE";
}

const juce::XmlElement* RackEngine::getShowExtra (const juce::String& tag) const
{
    for (auto* e : showExtras)
        if (e->hasTagName (tag))
            return e;
    return nullptr;
}

void RackEngine::setShowExtra (const juce::XmlElement& xml)
{
    jassert (! isEngineSection (xml.getTagName()));
    for (int i = showExtras.size(); --i >= 0;)
        if (showExtras[i]->hasTagName (xml.getTagName()))
            showExtras.remove (i);
    showExtras.add (new juce::XmlElement (xml));
}

std::unique_ptr<juce::XmlElement> RackEngine::createShow() const
{
    auto xml = createSnapshot();
    xml->setAttribute ("version", 4);

    auto* pagesXml = xml->createNewChildElement ("PAGES");
    pagesXml->setAttribute ("current", currentPage);
    for (auto& p : pages)
    {
        auto* e = pagesXml->createNewChildElement ("PAGE");
        e->setAttribute ("name", p.name);
        juce::StringArray ids;
        for (int id : p.rackIds) ids.add (juce::String (id));
        e->setAttribute ("racks", ids.joinIntoString (","));
    }

    auto* snapsXml = xml->createNewChildElement ("SNAPSHOTS");
    for (auto& s : snapshots)
        if (s.data != nullptr)
        {
            auto* e = new juce::XmlElement (*s.data);
            e->setAttribute ("number", s.number);
            e->setAttribute ("name", s.name);
            snapsXml->addChildElement (e);
        }

    for (auto* e : showExtras)
        xml->addChildElement (new juce::XmlElement (*e));

    return xml;
}

bool RackEngine::loadShow (const juce::XmlElement& xml, juce::String& error)
{
    if (! xml.hasTagName ("LIVERACK"))
    {
        error = "This isn't a LiveRack show file.";
        return false;
    }

    const bool ok = loadRig (xml, error);

    // A pre-0.13 file has no pages: keep the ones there are
    if (auto* pagesXml = xml.getChildByName ("PAGES"))
    {
        std::vector<RackPage> loaded;
        for (auto* e : pagesXml->getChildWithTagNameIterator ("PAGE"))
        {
            RackPage p;
            p.name = e->getStringAttribute ("name", "Page");
            juce::StringArray ids;
            ids.addTokens (e->getStringAttribute ("racks"), ",", "");
            for (auto& id : ids)
                if (id.getIntValue() > 0)
                    p.rackIds.push_back (id.getIntValue());
            loaded.push_back (p);
        }
        setPages (loaded);
        setCurrentPage (pagesXml->getIntAttribute ("current", -1));
    }

    snapshots.clear();
    if (auto* snapsXml = xml.getChildByName ("SNAPSHOTS"))
        for (auto* e : snapsXml->getChildWithTagNameIterator ("SNAPSHOT"))
        {
            ShowSnapshot s;
            s.number = juce::jmax (0, e->getIntAttribute ("number"));
            s.name = e->getStringAttribute ("name");
            s.data = std::make_shared<juce::XmlElement> (*e);
            putSnapshot (snapshots, std::move (s));
        }

    showExtras.clear();
    for (auto* e : xml.getChildIterator())
        if (! isEngineSection (e->getTagName()))
            showExtras.add (new juce::XmlElement (*e));

    sendChangeMessage();
    return ok;
}

void RackEngine::newShow()
{
    clearAll();
    setPages ({});
    setCurrentPage (-1);
    snapshots.clear();
    showExtras.clear();
    scope = {};
    bpm = 120.0;
    showFile = juce::File();
    showEdited = false;
    sendChangeMessage();
}

//==============================================================================
// Settings and session
juce::File RackEngine::getSettingsDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    dir = dir.getChildFile ("LiveRack");
    dir.createDirectory();
    return dir;
}

juce::File RackEngine::getDeadMansPedalFile() const
{
    return getSettingsDir().getChildFile ("scan-crash-list.txt");
}

void RackEngine::saveSettings()
{
    auto dir = getSettingsDir();

    if (auto xml = knownPlugins.createXml())
        xml->writeTo (dir.getChildFile ("plugins.xml"));

    juce::XmlElement all ("AUDIODRIVERS");
    for (auto& d : drivers)
    {
        auto* e = all.createNewChildElement ("DRIVER");
        if (auto state = d->deviceManager.createStateXml())
            e->addChildElement (state.release());
    }
    all.writeTo (dir.getChildFile ("audio-drivers.xml"));

    // Keep the 0.8 file current too, in case of going back a version
    if (! drivers.empty())
        if (auto xml = drivers.front()->deviceManager.createStateXml())
            xml->writeTo (dir.getChildFile ("audio-device.xml"));
}

void RackEngine::saveSession()
{
    if (auto xml = createShow())
    {
        xml->setAttribute ("showFile", showFile.getFullPathName());
        xml->setAttribute ("edited", showEdited);
        xml->writeTo (getSettingsDir().getChildFile ("last-session.liverack"));
    }
}

bool RackEngine::restoreSession (juce::String& error)
{
    auto file = getSettingsDir().getChildFile ("last-session.liverack");

    if (! file.existsAsFile())
        return false;

    if (auto xml = juce::parseXML (file))
    {
        const bool ok = loadShow (*xml, error);
        const auto path = xml->getStringAttribute ("showFile");
        showFile = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
        showEdited = xml->getBoolAttribute ("edited", false);
        return ok;
    }

    error = "Couldn't read the last session file.";
    return false;
}
