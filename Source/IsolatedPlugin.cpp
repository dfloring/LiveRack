#include "IsolatedPlugin.h"
#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

std::function<void (const juce::String&)> IsolatedPlugin::onEvent;

//==============================================================================
// The audio hand-off area, mapped by both processes
struct IsolatedPluginShared
{
    static constexpr int maxChannels = 8, maxBlock = 8192;

    std::atomic<juce::uint32> request { 0 }, done { 0 }; // block counters
    juce::int32 numSamples = 0, numChannels = 0, bypassed = 0, playing = 0;
    double bpm = 120.0, ppq = 0.0;
    juce::int64 timeSamples = 0;
    float audio[maxChannels][maxBlock];
};

using Shared = IsolatedPluginShared;

namespace
{
    enum Message
    {
        msgLoad = 1, msgLayout, msgPrepare, msgRelease, msgGetState, msgSetState,
        msgSetParam, msgShowEditor, msgCrash,
        msgReply = 100, evParams, evLatency, evState, evEditorClosed
    };

    juce::MemoryBlock pack (int type, int id, const juce::MemoryBlock& payload)
    {
        juce::MemoryOutputStream o;
        o.writeInt (type);
        o.writeInt (id);
        o.write (payload.getData(), payload.getSize());
        return o.getMemoryBlock();
    }

    bool unpack (const juce::MemoryBlock& mb, int& type, int& id, juce::MemoryBlock& payload)
    {
        if (mb.getSize() < 8)
            return false;

        juce::MemoryInputStream in (mb, false);
        type = in.readInt();
        id = in.readInt();
        payload.setSize (0);
        payload.append (juce::addBytesToPointer (mb.getData(), 8), mb.getSize() - 8);
        return true;
    }

    juce::AudioChannelSet setFor (int n) { return n == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo(); }
}

void Isolation::requestLayout (juce::AudioPluginInstance& plugin, int ins, int outs)
{
    if (auto* isolated = dynamic_cast<IsolatedPlugin*> (&plugin))
    {
        isolated->requestLayout (ins, outs);
        return;
    }

    auto tryLayout = [&plugin] (const juce::AudioChannelSet& in, const juce::AudioChannelSet& out)
    {
        auto layout = plugin.getBusesLayout();
        if (! layout.inputBuses.isEmpty())  layout.inputBuses.getReference (0)  = in;
        if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = out;
        for (int i = 1; i < layout.inputBuses.size(); ++i)
            layout.inputBuses.getReference (i) = juce::AudioChannelSet::disabled(); // no sidechains

        if (! plugin.checkBusesLayoutSupported (layout))
            return false;

        return plugin.setBusesLayout (layout);
    };

    if (tryLayout (setFor (ins), setFor (outs)))                return;
    if (ins != outs && tryLayout (setFor (ins), setFor (ins)))  return; // can't split here: stays as it was
    if (tryLayout (setFor (ins), setFor (ins)))                 return;
    tryLayout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
}

//==============================================================================
//==============================================================================
// LiveRack side

class IsolatedPlugin::Link final : public juce::ChildProcessCoordinator
{
public:
    explicit Link (IsolatedPlugin& o) : owner (o) {}
    void handleMessageFromWorker (const juce::MemoryBlock& mb) override { owner.messageFromHelper (mb); }
    void handleConnectionLost() override                                { owner.connectionLost(); }
    IsolatedPlugin& owner;
};

class IsolatedPlugin::Param final : public juce::AudioPluginInstance::HostedParameter
{
public:
    Param (IsolatedPlugin& o, int i) : owner (o), index (i) {}

    float getValue() const override                    { return value.load(); }
    void setValue (float v) override                   { value = v; owner.sendParam (index, v); }
    float getDefaultValue() const override             { return defaultValue; }
    juce::String getName (int max) const override      { return name.substring (0, max); }
    juce::String getLabel() const override             { return label; }
    int getNumSteps() const override                   { return steps; }
    bool isDiscrete() const override                   { return discrete; }
    bool isBoolean() const override                    { return boolean; }
    juce::String getParameterID() const override       { return paramId; }
    float getValueForText (const juce::String& t) const override { return t.getFloatValue(); }

    juce::String getText (float v, int max) const override
    {
        const juce::SpinLock::ScopedLockType sl (textLock);
        if (std::abs (v - textValue) < 1.0e-4f)
            return text.substring (0, max);
        return juce::String (v, 2);
    }

    void setFromHelper (float v, const juce::String& t)
    {
        {
            const juce::SpinLock::ScopedLockType sl (textLock);
            text = t;
            textValue = v;
        }

        if (std::abs (value.exchange (v) - v) > 1.0e-7f)
            sendValueChangedMessageToListeners (v);
    }

    IsolatedPlugin& owner;
    const int index;
    juce::String paramId, name, label;
    float defaultValue = 0.0f;
    int steps = 0x7fffffff;
    bool discrete = false, boolean = false;

private:
    std::atomic<float> value { 0.0f };
    juce::SpinLock textLock;
    juce::String text;
    float textValue = -1.0f;
};

// Shown in the LiveRack panel in place of the plugin's own window
class IsolatedPlugin::Placeholder final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit Placeholder (IsolatedPlugin& o) : juce::AudioProcessorEditor (o), owner (o)
    {
        addAndMakeVisible (button);
        button.onClick = [this] { owner.showHelperWindow (true); };
        setSize (480, 150);
        owner.showHelperWindow (true);
        startTimerHz (4);
    }

    ~Placeholder() override { owner.showHelperWindow (false); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff181b1f));
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (owner.getName(), 16, 12, getWidth() - 32, 22, juce::Justification::centredLeft);
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText ("This plugin runs in its own process, so its window is separate from LiveRack.",
                          16, 38, getWidth() - 32, 36, juce::Justification::topLeft, 2);
        g.setColour (owner.isRunning() ? juce::Colour (0xff6fcf97) : juce::Colour (0xffeb5757));
        g.drawText (owner.getStatusText(), 16, 76, getWidth() - 32, 20, juce::Justification::centredLeft);
    }

    void resized() override { button.setBounds (16, getHeight() - 42, 190, 28); }

private:
    void timerCallback() override { repaint(); }

    IsolatedPlugin& owner;
    juce::TextButton button { "Show Plugin Window" };
};

//==============================================================================
IsolatedPlugin::IsolatedPlugin (const juce::PluginDescription& d)
    : juce::AudioPluginInstance (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      description (d)
{
}

IsolatedPlugin::~IsolatedPlugin()
{
    stopTimer();
    lifeToken->store (false);

    if (restartThread.joinable())
    {
        alive = false;
        replyEvent.signal(); // ends a request the restart is waiting on
        restartThread.join();
    }

    stop();
}

std::unique_ptr<IsolatedPlugin> IsolatedPlugin::create (const juce::PluginDescription& desc, double sampleRate,
                                                        int blockSize, juce::String& error)
{
    std::unique_ptr<IsolatedPlugin> p (new IsolatedPlugin (desc));
    p->preparedRate = sampleRate;
    p->preparedBlock = blockSize;

    if (! p->start (error))
        return nullptr;

    p->startTimer (500);
    return p;
}

bool IsolatedPlugin::start (juce::String& error)
{
    juce::MemoryBlock reply;
    if (! launch (error, reply))
        return false;

    applyLoadReply (reply);
    return true;
}

bool IsolatedPlugin::launch (juce::String& error, juce::MemoryBlock& reply)
{
    std::unique_ptr<juce::MemoryMappedFile> newMapping;
    Shared* newShared = nullptr;

    {
        const juce::File shm ("/dev/shm");
        const auto dir = shm.isDirectory() ? shm : juce::File::getSpecialLocation (juce::File::tempDirectory);
        sharedFile = dir.getChildFile ("liverack-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()) + ".audio");

        {
            juce::FileOutputStream out (sharedFile);
            juce::HeapBlock<char> zeros (sizeof (Shared), true);

            if (! out.openedOk() || ! out.write (zeros.get(), sizeof (Shared)))
            {
                error = "Couldn't create the shared audio buffer";
                return false;
            }
        }

        newMapping = std::make_unique<juce::MemoryMappedFile> (sharedFile, juce::MemoryMappedFile::readWrite, false);

        if (newMapping->getData() == nullptr || newMapping->getSize() < sizeof (Shared))
        {
            sharedFile.deleteFile();
            error = "Couldn't map the shared audio buffer";
            return false;
        }

        newShared = new (newMapping->getData()) Shared();
    }

    stale = false;
    stallStartMs = 0;

    pipeName = "liverack" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64());
    auto newPipe = std::make_unique<juce::NamedPipe>();

    if (! newPipe->createNewPipe (pipeName, false))
    {
        error = "Couldn't create the audio wake-up pipe";
        return false;
    }

    char b = 0;
    newPipe->read (&b, 1, 0); // opens our reading end

    link = std::make_unique<Link> (*this);

    if (! link->launchWorkerProcess (juce::File::getSpecialLocation (juce::File::currentExecutableFile),
                                     Isolation::workerUID, 0, 0))
    {
        link.reset();
        error = "Couldn't start the helper process";
        return false;
    }

    {
        const juce::SpinLock::ScopedLockType sl (audioLock);
        pipe = std::move (newPipe);
        mapping = std::move (newMapping);
        shared = newShared;
    }

    alive = true;

    juce::MemoryOutputStream o;
    o.writeString (description.createXml()->toString());
    o.writeDouble (preparedRate > 0.0 ? preparedRate : 48000.0);
    o.writeInt (preparedBlock > 0 ? preparedBlock : 512);
    o.writeString (sharedFile.getFullPathName());
    o.writeString (pipeName);

    if (! request (msgLoad, o.getMemoryBlock(), reply, 180000))
    {
        stop();
        error = "The helper process didn't load " + description.name;
        return false;
    }

    juce::MemoryInputStream in (reply, false);

    if (! in.readBool())
    {
        error = in.readString();
        stop();
        return false;
    }

    return true;
}

void IsolatedPlugin::applyLoadReply (const juce::MemoryBlock& reply)
{
    juce::MemoryInputStream in (reply, false);
    in.readBool();
    in.readString(); // error text (empty)
    const int ins = in.readInt(), outs = in.readInt();
    setLatencySamples (in.readInt());
    const int numParams = in.readInt();
    const bool firstLoad = params.isEmpty();

    for (int i = 0; i < numParams; ++i)
    {
        const auto id = in.readString(), name = in.readString(), label = in.readString();
        const float def = in.readFloat();
        const int steps = in.readInt();
        const bool discrete = in.readBool(), boolean = in.readBool();
        const float value = in.readFloat();
        const auto text = in.readString();

        if (firstLoad)
        {
            auto p = std::make_unique<Param> (*this, i);
            p->paramId = id; p->name = name; p->label = label;
            p->defaultValue = def; p->steps = steps; p->discrete = discrete; p->boolean = boolean;
            params.add (p.get());
            addHostedParameter (std::move (p));
        }

        if (auto* p = params[i])
            p->setFromHelper (value, text);
    }

    if (firstLoad)
    {
        wantedIns = juce::jlimit (1, 2, ins);
        wantedOuts = juce::jlimit (1, 2, outs);
        BusesLayout l;
        l.inputBuses.add (setFor (wantedIns));
        l.outputBuses.add (setFor (wantedOuts));
        setBusesLayout (l);
    }
}

void IsolatedPlugin::stop()
{
    expectingLoss = true;
    alive = false;
    helperPrepared = false;
    replyEvent.signal();
    link.reset(); // kills the helper

    {
        const juce::SpinLock::ScopedLockType sl (audioLock);
        pipe.reset();
        shared = nullptr;
        mapping.reset();
    }

    sharedFile.deleteFile();
    expectingLoss = false;
}

void IsolatedPlugin::restart (const juce::String& reason)
{
    if (gaveUp || restarting)
        return;

    stop();

    const auto now = juce::Time::getMillisecondCounter();
    for (int i = restartTimes.size(); --i >= 0;)
        if (now - restartTimes[i] > 60000)
            restartTimes.remove (i);

    if (restartTimes.size() >= 3)
    {
        gaveUp = true;
        if (onEvent != nullptr)
            onEvent (description.name + " " + reason + " repeatedly and is stopped: audio passes through it unprocessed");
        return;
    }

    restartTimes.add (now);
    ++restarts;

    if (restartThread.joinable())
        restartThread.join();

    // What the new helper is set up with. Anything that changes meanwhile is
    // caught up with when the restart finishes.
    juce::MemoryBlock state;
    {
        const juce::ScopedLock sl (stateLock);
        state = lastState;
        usedStateGeneration = hasState ? stateGeneration : -1;
    }
    usedIns = wantedIns; usedOuts = wantedOuts;
    usedPrepared = wantPrepared; usedRate = preparedRate; usedBlock = preparedBlock;
    restarting = true;

    auto token = lifeToken;
    restartThread = std::thread ([this, token, reason, state]
    {
        restartThreadId = std::this_thread::get_id();
        juce::String error;
        restartLoadReply.reset();
        restartStateReply.reset();
        restartLatency = -1;

        bool ok = launch (error, restartLoadReply);

        if (ok)
        {
            juce::MemoryOutputStream layout;
            layout.writeInt (usedIns);
            layout.writeInt (usedOuts);
            juce::MemoryBlock reply;
            request (msgLayout, layout.getMemoryBlock(), reply, 10000);

            if (usedStateGeneration >= 0)
                request (msgSetState, state, restartStateReply, 20000);

            if (usedPrepared)
            {
                juce::MemoryOutputStream prep;
                prep.writeDouble (usedRate);
                prep.writeInt (usedBlock);
                if (request (msgPrepare, prep.getMemoryBlock(), reply, 30000))
                {
                    juce::MemoryInputStream in (reply, false);
                    restartLatency = in.readInt();
                    helperPrepared = true; // audio goes to the new helper from here
                }
            }

            ok = alive.load(); // it may have crashed again while loading
        }

        juce::MessageManager::callAsync ([this, token, ok, error, reason]
        {
            if (token->load())
                finishRestart (ok, error, reason);
        });
    });
}

void IsolatedPlugin::finishRestart (bool ok, const juce::String& error, const juce::String& reason)
{
    if (restartThread.joinable())
        restartThread.join();

    restarting = false;

    auto tell = [this] (const juce::String& text)
    {
        if (onEvent != nullptr)
            onEvent (description.name + " " + text);
    };

    if (! ok)
    {
        stop();
        gaveUp = true;
        tell (reason + " and couldn't be restarted" + (error.isNotEmpty() ? " (" + error + ")" : juce::String())
              + ": audio passes through it unprocessed");
        return;
    }

    // The latest each parameter was set to, including while the helper was loading
    valuesBeforeRestart.clear();
    for (auto* prm : params)
        valuesBeforeRestart.push_back (prm->getValue());

    applyLoadReply (restartLoadReply);

    if (! restartStateReply.isEmpty())
    {
        juce::MemoryInputStream in (restartStateReply, false);
        applyParamChanges (in);
    }

    if (restartLatency >= 0)
        setLatencySamples (restartLatency);

    // Parameter changes the helper hadn't reported back as settings yet
    for (int i = 0; i < params.size() && i < (int) valuesBeforeRestart.size(); ++i)
        if (std::abs (params[i]->getValue() - valuesBeforeRestart[(size_t) i]) > 1.0e-6f)
            params[i]->setValueNotifyingHost (valuesBeforeRestart[(size_t) i]);

    // Catch up with anything that changed while the helper was loading
    if (wantedIns != usedIns || wantedOuts != usedOuts)
        requestLayout (wantedIns, wantedOuts);

    {
        bool newer;
        juce::MemoryBlock state;
        {
            const juce::ScopedLock sl (stateLock);
            newer = hasState && stateGeneration != usedStateGeneration;
            state = lastState;
        }
        if (newer)
            setStateInformation (state.getData(), (int) state.getSize());
    }

    if (wantPrepared && (! usedPrepared || preparedRate != usedRate || preparedBlock != usedBlock))
        prepareToPlay (preparedRate, preparedBlock);
    else if (! wantPrepared && usedPrepared)
        releaseResources();

    if (wantWindow)
        showHelperWindow (true);

    tell (reason + " and was restarted with its settings");
}

juce::String IsolatedPlugin::getStatusText() const
{
    if (gaveUp)       return "Stopped after repeated crashes. Audio passes through unprocessed.";
    if (restarting || ! alive) return "Restarting...";
    const auto timing = ", hand-off " + juce::String (avgRoundTripUs.load() / 1000.0, 2) + " ms (longest "
                      + juce::String (maxRoundTripUs.load() / 1000.0, 1) + ")";
    if (restarts > 0) return "Running, restarted " + juce::String (restarts) + (restarts == 1 ? " time" : " times") + timing;
    return "Running" + timing;
}

//==============================================================================
// While a restart is under way only the restart itself talks to the helper:
// anyone else gets an immediate "no" instead of waiting for the plugin to load
bool IsolatedPlugin::mayRequest() const
{
    return ! restarting.load() || std::this_thread::get_id() == restartThreadId;
}

bool IsolatedPlugin::request (int type, const juce::MemoryBlock& payload, juce::MemoryBlock& reply, int timeoutMs)
{
    if (! mayRequest())
        return false;

    const juce::ScopedLock oneAtATime (requestLock);

    if (! alive || link == nullptr)
        return false;

    int id;
    {
        const juce::ScopedLock sl (replyLock);
        id = waitingId = ++nextRequestId;
        gotReply = false;
    }

    replyEvent.reset();

    if (! link->sendMessageToWorker (pack (type, id, payload)))
        return false;

    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

    while (alive)
    {
        replyEvent.wait (50);

        const juce::ScopedLock sl (replyLock);
        if (gotReply)
        {
            reply = replyData;
            waitingId = 0;
            return true;
        }

        const auto now = juce::Time::getMillisecondCounter();
        const auto stalled = stallStartMs.load();

        // Don't hold LiveRack up for a helper whose audio has already stopped answering
        if (now > end || (stalled != 0 && now - stalled > 3000))
            break;
    }

    const juce::ScopedLock sl (replyLock);
    waitingId = 0;
    return false;
}

void IsolatedPlugin::send (int type, const juce::MemoryBlock& payload)
{
    if (! restarting && alive && link != nullptr)
        link->sendMessageToWorker (pack (type, 0, payload));
}

void IsolatedPlugin::sendParam (int index, float value)
{
    juce::MemoryOutputStream o;
    o.writeInt (index);
    o.writeFloat (value);
    send (msgSetParam, o.getMemoryBlock());
}

void IsolatedPlugin::messageFromHelper (const juce::MemoryBlock& mb)
{
    int type = 0, id = 0;
    juce::MemoryBlock payload;

    if (! unpack (mb, type, id, payload))
        return;

    if (type == msgReply)
    {
        const juce::ScopedLock sl (replyLock);
        if (id == waitingId)
        {
            replyData = payload;
            gotReply = true;
            replyEvent.signal();
        }
        return;
    }

    auto token = lifeToken;
    juce::MessageManager::callAsync ([this, token, type, payload]
    {
        if (token->load())
            handleEvent (type, payload);
    });
}

void IsolatedPlugin::connectionLost()
{
    if (expectingLoss)
        return;

    alive = false;
    replyEvent.signal();

    auto token = lifeToken;
    juce::MessageManager::callAsync ([this, token]
    {
        if (token->load() && ! alive)
            restart ("crashed");
    });
}

void IsolatedPlugin::handleEvent (int type, const juce::MemoryBlock& payload)
{
    juce::MemoryInputStream in (payload, false);

    if (type == evParams)
    {
        applyParamChanges (in);
    }
    else if (type == evLatency)
    {
        setLatencySamples (in.readInt());
    }
    else if (type == evState)
    {
        const juce::ScopedLock sl (stateLock);
        lastState = payload;
        hasState = true;
    }
    else if (type == evEditorClosed)
    {
        wantWindow = false;
    }
}

void IsolatedPlugin::timerCallback()
{
    const auto since = stallStartMs.load();

    if (! restarting && alive && since != 0 && juce::Time::getMillisecondCounter() - since > 3000)
        restart ("stopped responding");
}

//==============================================================================
void IsolatedPlugin::requestLayout (int ins, int outs)
{
    wantedIns = juce::jlimit (1, 2, ins);
    wantedOuts = juce::jlimit (1, 2, outs);

    juce::MemoryOutputStream o;
    o.writeInt (wantedIns);
    o.writeInt (wantedOuts);

    juce::MemoryBlock reply;
    int gotIns = wantedIns, gotOuts = wantedOuts;

    if (request (msgLayout, o.getMemoryBlock(), reply, 10000))
    {
        juce::MemoryInputStream in (reply, false);
        gotIns = juce::jlimit (1, 2, in.readInt());
        gotOuts = juce::jlimit (1, 2, in.readInt());
    }

    BusesLayout l;
    l.inputBuses.add (setFor (gotIns));
    l.outputBuses.add (setFor (gotOuts));
    setBusesLayout (l);
}

bool IsolatedPlugin::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto ok = [] (const juce::AudioChannelSet& s) { return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo(); };
    return ok (l.getMainInputChannelSet()) && ok (l.getMainOutputChannelSet());
}

void IsolatedPlugin::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedRate = sampleRate;
    preparedBlock = samplesPerBlock;
    wantPrepared = true;
    waitMs = juce::jlimit (10, 100, (int) (4000.0 * samplesPerBlock / juce::jmax (1.0, sampleRate)) + 5);

    juce::MemoryOutputStream o;
    o.writeDouble (sampleRate);
    o.writeInt (samplesPerBlock);

    juce::MemoryBlock reply;
    if (request (msgPrepare, o.getMemoryBlock(), reply, 30000))
    {
        juce::MemoryInputStream in (reply, false);
        setLatencySamples (in.readInt());
        helperPrepared = true;
    }
}

void IsolatedPlugin::releaseResources()
{
    wantPrepared = false;
    helperPrepared = false;
    juce::MemoryBlock reply;
    request (msgRelease, {}, reply, 5000);
}

void IsolatedPlugin::getStateInformation (juce::MemoryBlock& dest)
{
    juce::MemoryBlock reply;
    const bool fresh = request (msgGetState, {}, reply, 6000);

    const juce::ScopedLock sl (stateLock);
    if (fresh)
    {
        lastState = reply;
        hasState = true;
    }

    dest = lastState;
}

void IsolatedPlugin::setStateInformation (const void* data, int size)
{
    juce::MemoryBlock state (data, (size_t) juce::jmax (0, size));
    {
        const juce::ScopedLock sl (stateLock);
        lastState = state;
        hasState = true;
        ++stateGeneration;
    }
    juce::MemoryBlock reply;

    // The reply carries the parameter values the settings brought with them
    if (request (msgSetState, state, reply, 8000))
    {
        juce::MemoryInputStream in (reply, false);
        applyParamChanges (in);
    }
}

void IsolatedPlugin::applyParamChanges (juce::InputStream& in)
{
    const int count = in.readInt();
    for (int i = 0; i < count; ++i)
    {
        const int index = in.readInt();
        const float value = in.readFloat();
        const auto text = in.readString();
        if (auto* p = params[index])
            p->setFromHelper (value, text);
    }
}

juce::AudioProcessorEditor* IsolatedPlugin::createEditor() { return new Placeholder (*this); }

void IsolatedPlugin::showHelperWindow (bool shouldShow)
{
    wantWindow = shouldShow;
    juce::MemoryOutputStream o;
    o.writeBool (shouldShow);
    send (msgShowEditor, o.getMemoryBlock());
}

void IsolatedPlugin::crashHelper() { send (msgCrash, {}); }

//==============================================================================
void IsolatedPlugin::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)         { handOff (b, false); }
void IsolatedPlugin::processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer&) { handOff (b, true); }

void IsolatedPlugin::handOff (juce::AudioBuffer<float>& buffer, bool bypassed)
{
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (buffer.getNumChannels(), (int) Shared::maxChannels);

    // What comes out while the helper isn't there: the input, unprocessed
    auto dry = [&]
    {
        ++blocksMissed;
        const int ins = getMainBusNumInputChannels();
        for (int c = juce::jmax (1, ins); c < buffer.getNumChannels(); ++c)
            buffer.copyFrom (c, 0, buffer, 0, 0, n);
    };

    const juce::SpinLock::ScopedTryLockType lock (audioLock);

    if (! lock.isLocked() || ! alive.load() || ! helperPrepared.load() || pipe == nullptr
        || shared == nullptr || n > Shared::maxBlock || n <= 0)
    {
        dry();
        return;
    }

    char wake = 1;

    if (stale)
    {
        // The helper is still busy with an earlier block
        if (shared->done.load() != shared->request.load())
        {
            dry();
            return;
        }

        while (pipe->read (&wake, 1, 0) > 0) {}
        stale = false;
        stallStartMs = 0;
    }

    shared->numSamples = n;
    shared->numChannels = chans;
    shared->bypassed = bypassed ? 1 : 0;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            shared->bpm = pos->getBpm().orFallback (120.0);
            shared->ppq = pos->getPpqPosition().orFallback (0.0);
            shared->timeSamples = pos->getTimeInSamples().orFallback (0);
            shared->playing = pos->getIsPlaying() ? 1 : 0;
        }
    }

    for (int c = 0; c < chans; ++c)
        juce::FloatVectorOperations::copy (shared->audio[c], buffer.getReadPointer (c), n);

    const auto wanted = shared->request.fetch_add (1) + 1;
    wake = 1;
    pipe->write (&wake, 1, 5);

    const auto sentAt = juce::Time::getMillisecondCounterHiRes();
    const auto deadline = sentAt + waitMs;

    while (shared->done.load() != wanted)
    {
        const auto left = deadline - juce::Time::getMillisecondCounterHiRes();

        if (left <= 0.0 || ! alive.load())
        {
            stale = true;
            juce::uint32 none = 0;
            stallStartMs.compare_exchange_strong (none, juce::jmax ((juce::uint32) 1, juce::Time::getMillisecondCounter()));
            dry();
            return;
        }

        pipe->read (&wake, 1, juce::jmax (1, (int) std::ceil (left)));
    }

    for (int c = 0; c < chans; ++c)
        buffer.copyFrom (c, 0, shared->audio[c], n);

    ++blocksDone;
    const int us = (int) ((juce::Time::getMillisecondCounterHiRes() - sentAt) * 1000.0);
    avgRoundTripUs = (avgRoundTripUs.load() * 31 + us) / 32;
    if (us > maxRoundTripUs.load())
        maxRoundTripUs = us;
}

//==============================================================================
//==============================================================================
// Helper process side
namespace
{
class HelperProcess final : public Isolation::Worker,
                            private juce::ChildProcessWorker,
                            private juce::Timer,
                            private juce::Thread,
                            private juce::AudioPlayHead
{
public:
    HelperProcess() : juce::Thread ("LiveRack plugin audio")
    {
       #if JUCE_WINDOWS
        // A crash ends the helper at once, so LiveRack notices straight away,
        // instead of Windows holding it open to collect a crash report
        SetErrorMode (GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
       #endif
        formatManager.addDefaultFormats();
    }

    ~HelperProcess() override
    {
        stopTimer();
        stopAudioThread();
        window.reset();
        plugin.reset();
    }

    using juce::ChildProcessWorker::initialiseFromCommandLine;

private:
    //==========================================================================
    void handleMessageFromCoordinator (const juce::MemoryBlock& mb) override
    {
        juce::MessageManager::callAsync ([this, mb] { handle (mb); });
    }

    void handleConnectionLost() override { juce::JUCEApplicationBase::quit(); }

    void reply (int id, const juce::MemoryBlock& payload) { sendMessageToCoordinator (pack (msgReply, id, payload)); }
    void event (int type, const juce::MemoryBlock& payload) { sendMessageToCoordinator (pack (type, 0, payload)); }

    void handle (const juce::MemoryBlock& mb)
    {
        int type = 0, id = 0;
        juce::MemoryBlock payload;
        if (! unpack (mb, type, id, payload))
            return;

        juce::MemoryInputStream in (payload, false);
        juce::MemoryOutputStream out;

        switch (type)
        {
            case msgLoad:       load (in, out); reply (id, out.getMemoryBlock()); break;

            case msgLayout:
            {
                const int ins = in.readInt(), outs = in.readInt();
                if (plugin != nullptr)
                {
                    const juce::ScopedLock sl (processLock);
                    Isolation::requestLayout (*plugin, ins, outs);
                    out.writeInt (plugin->getMainBusNumInputChannels());
                    out.writeInt (plugin->getMainBusNumOutputChannels());
                }
                reply (id, out.getMemoryBlock());
                break;
            }

            case msgPrepare:
            {
                const double rate = in.readDouble();
                const int block = in.readInt();
                if (plugin != nullptr)
                {
                    const juce::ScopedLock sl (processLock);
                    if (prepared)
                        plugin->releaseResources();
                    plugin->setRateAndBufferSizeDetails (rate, block);
                    plugin->prepareToPlay (rate, block);
                    sampleRate = rate;
                    prepared = true;
                    lastLatency = plugin->getLatencySamples();
                }
                out.writeInt (lastLatency);
                reply (id, out.getMemoryBlock());
                break;
            }

            case msgRelease:
                if (plugin != nullptr)
                {
                    const juce::ScopedLock sl (processLock);
                    if (prepared)
                        plugin->releaseResources();
                    prepared = false;
                }
                reply (id, {});
                break;

            case msgGetState:
            {
                juce::MemoryBlock state;
                if (plugin != nullptr)
                    plugin->getStateInformation (state);
                reply (id, state);
                break;
            }

            case msgSetState:
                if (plugin != nullptr)
                {
                    plugin->setStateInformation (payload.getData(), (int) payload.getSize());
                    writeParamChanges (out);
                }
                reply (id, out.getMemoryBlock());
                break;

            case msgSetParam:
            {
                const int index = in.readInt();
                const float value = in.readFloat();
                if (plugin != nullptr)
                    if (auto* p = plugin->getParameters()[index])
                    {
                        p->setValueNotifyingHost (value);
                        if (juce::isPositiveAndBelow (index, (int) sent.size()))
                            sent[(size_t) index] = p->getValue();
                        sentTextFor (index, *p);
                    }
                break;
            }

            case msgShowEditor: showWindow (in.readBool()); break;

            case msgCrash:
            {
                volatile int* nowhere = nullptr;
                *nowhere = 1;
                break;
            }

            default: break;
        }
    }

    void load (juce::InputStream& in, juce::OutputStream& out)
    {
        juce::PluginDescription desc;
        juce::String error;

        if (auto xml = juce::parseXML (in.readString()))
            desc.loadFromXml (*xml);

        const double rate = in.readDouble();
        const int block = in.readInt();
        const juce::File sharedFile (in.readString());
        const auto pipeName = in.readString();

        mapping = std::make_unique<juce::MemoryMappedFile> (sharedFile, juce::MemoryMappedFile::readWrite, false);

        if (mapping->getData() == nullptr || mapping->getSize() < sizeof (Shared))
            error = "Couldn't map the shared audio buffer";
        else if (! pipe.openExisting (pipeName))
            error = "Couldn't open the audio wake-up pipe";
        else
        {
           #if LIVERACK_PREVIEW_HOOKS
            // Test builds: pretend to be a plugin that takes a while to load
            if (const int ms = juce::SystemStats::getEnvironmentVariable ("LIVERACK_TEST_SLOW_LOAD", "0").getIntValue(); ms > 0)
                juce::Thread::sleep (ms);
           #endif
            plugin = formatManager.createPluginInstance (desc, rate, block, error);
        }

        if (plugin == nullptr)
        {
            out.writeBool (false);
            out.writeString (error.isNotEmpty() ? error : "Couldn't load " + desc.name);
            return;
        }

        shared = static_cast<Shared*> (mapping->getData());
        sampleRate = rate;
        plugin->setPlayHead (this);
        lastLatency = plugin->getLatencySamples();

        out.writeBool (true);
        out.writeString ({});
        out.writeInt (plugin->getMainBusNumInputChannels());
        out.writeInt (plugin->getMainBusNumOutputChannels());
        out.writeInt (lastLatency);

        const auto& ps = plugin->getParameters();
        out.writeInt (ps.size());
        sent.assign ((size_t) ps.size(), 0.0f);

        for (int i = 0; i < ps.size(); ++i)
        {
            auto* p = ps[i];
            auto* hosted = dynamic_cast<juce::AudioPluginInstance::HostedParameter*> (p);
            out.writeString (hosted != nullptr ? hosted->getParameterID() : juce::String (i));
            out.writeString (p->getName (128));
            out.writeString (p->getLabel());
            out.writeFloat (p->getDefaultValue());
            out.writeInt (p->getNumSteps());
            out.writeBool (p->isDiscrete());
            out.writeBool (p->isBoolean());
            sent[(size_t) i] = p->getValue();
            out.writeFloat (sent[(size_t) i]);
            out.writeString (p->getCurrentValueAsText());
        }

        char b = 0;
        pipe.read (&b, 1, 0); // opens our reading end

        if (! startRealtimeThread (juce::Thread::RealtimeOptions().withPriority (9)
                                       .withApproximateAudioProcessingTime (block, rate)))
            startThread (juce::Thread::Priority::highest);

        startTimerHz (15);
    }

    //==========================================================================
    // Parameter changes made in the plugin's window go back to LiveRack, and so
    // do the plugin's settings once they've been still for a moment (they are
    // what a restarted helper gets back).
    void timerCallback() override
    {
        if (plugin == nullptr)
            return;

        juce::MemoryOutputStream o;
        const auto now = juce::Time::getMillisecondCounter();

        if (writeParamChanges (o) > 0)
        {
            event (evParams, o.getMemoryBlock());
            stateDirtySince = now;
        }

        if (stateDirtySince != 0 && now - stateDirtySince > 1000)
            pushState();

        if (const int latency = plugin->getLatencySamples(); latency != lastLatency)
        {
            lastLatency = latency;
            juce::MemoryOutputStream o;
            o.writeInt (latency);
            event (evLatency, o.getMemoryBlock());
        }
    }

    // count, then index / value / text for each parameter that changed
    int writeParamChanges (juce::OutputStream& out)
    {
        const auto& ps = plugin->getParameters();
        juce::MemoryOutputStream changes;
        int count = 0;

        for (int i = 0; i < ps.size() && i < (int) sent.size(); ++i)
        {
            const float v = ps[i]->getValue();
            if (std::abs (v - sent[(size_t) i]) > 1.0e-6f)
            {
                sent[(size_t) i] = v;
                changes.writeInt (i);
                changes.writeFloat (v);
                changes.writeString (ps[i]->getCurrentValueAsText());
                ++count;
            }
        }

        out.writeInt (count);
        out.write (changes.getData(), changes.getDataSize());
        return count;
    }

    void sentTextFor (int index, juce::AudioProcessorParameter& p)
    {
        juce::MemoryOutputStream o;
        o.writeInt (1);
        o.writeInt (index);
        o.writeFloat (p.getValue());
        o.writeString (p.getCurrentValueAsText());
        event (evParams, o.getMemoryBlock());
        stateDirtySince = juce::Time::getMillisecondCounter();
    }

    void pushState()
    {
        stateDirtySince = 0;
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        event (evState, state);
    }

    //==========================================================================
    struct Window final : public juce::DocumentWindow
    {
        Window (const juce::String& name, HelperProcess& o)
            : juce::DocumentWindow (name, juce::Colour (0xff181b1f), juce::DocumentWindow::closeButton), owner (o)
        {
            setUsingNativeTitleBar (true);
            setAlwaysOnTop (true);
        }

        void closeButtonPressed() override { owner.windowClosedByUser(); }
        HelperProcess& owner;
    };

    void showWindow (bool shouldShow)
    {
        if (! shouldShow || plugin == nullptr)
        {
            if (window != nullptr)
            {
                windowPos = window->getPosition();
                window.reset();
                pushState();
            }
            return;
        }

        if (window == nullptr)
        {
            auto* editor = plugin->hasEditor() ? plugin->createEditorIfNeeded() : nullptr;
            if (editor == nullptr)
                return;

            window = std::make_unique<Window> (plugin->getName(), *this);
            window->setContentOwned (editor, true);
            window->setResizable (editor->isResizable(), false);

            if (windowPos.x == -1)
                window->centreWithSize (window->getWidth(), window->getHeight());
            else
                window->setTopLeftPosition (windowPos);

            window->setVisible (true);
        }

        juce::Process::makeForegroundProcess();
        window->toFront (true);
    }

    void windowClosedByUser()
    {
        showWindow (false);
        event (evEditorClosed, {});
    }

    //==========================================================================
    // The audio thread: waits for LiveRack to hand over a block
    void run() override
    {
        auto handled = shared->done.load();

        while (! threadShouldExit())
        {
            char wake = 0;
            pipe.read (&wake, 1, 100);

            const auto wanted = shared->request.load();
            if (wanted == handled)
                continue;

            {
                const juce::ScopedLock sl (processLock);
                if (plugin != nullptr && prepared)
                    processOne();
            }

            handled = wanted;
            shared->done.store (wanted);
            wake = 1;
            pipe.write (&wake, 1, 20);
        }
    }

    void processOne()
    {
        const int n = juce::jlimit (0, (int) Shared::maxBlock, (int) shared->numSamples);
        const int given = juce::jlimit (0, (int) Shared::maxChannels, (int) shared->numChannels);
        const int chans = juce::jlimit (1, (int) Shared::maxChannels,
                                        juce::jmax (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()));
        float* channels[Shared::maxChannels];

        for (int c = 0; c < chans; ++c)
        {
            channels[c] = shared->audio[c];
            if (c >= given)
                juce::FloatVectorOperations::clear (channels[c], n);
        }

        juce::AudioBuffer<float> buffer (channels, chans, n);
        juce::MidiBuffer midi;
        const juce::ScopedNoDenormals noDenormals;
        const bool bypass = shared->bypassed != 0;

        // The rack's bypass decides, as it does for plugins inside LiveRack
        if (auto* bp = plugin->getBypassParameter())
        {
            if ((bp->getValue() >= 0.5f) != bypass)
                bp->setValue (bypass ? 1.0f : 0.0f);

            plugin->processBlock (buffer, midi);
        }
        else if (bypass)
        {
            plugin->processBlockBypassed (buffer, midi);
        }
        else
        {
            plugin->processBlock (buffer, midi);
        }
    }

    void stopAudioThread()
    {
        signalThreadShouldExit();
        stopThread (2000);
    }

    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        juce::AudioPlayHead::PositionInfo info;

        if (shared != nullptr)
        {
            info.setBpm (shared->bpm);
            info.setPpqPosition (shared->ppq);
            info.setTimeInSamples (shared->timeSamples);
            info.setTimeInSeconds ((double) shared->timeSamples / juce::jmax (1.0, sampleRate));
            info.setIsPlaying (shared->playing != 0);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        }

        return info;
    }

    juce::AudioPluginFormatManager formatManager;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<juce::MemoryMappedFile> mapping;
    Shared* shared = nullptr;
    juce::NamedPipe pipe;
    juce::CriticalSection processLock;
    bool prepared = false;
    double sampleRate = 48000.0;
    int lastLatency = 0;
    std::vector<float> sent;
    juce::uint32 stateDirtySince = 0;
    std::unique_ptr<Window> window;
    juce::Point<int> windowPos { -1, -1 };
};
}

std::unique_ptr<Isolation::Worker> Isolation::startWorkerIfRequested (const juce::String& commandLine)
{
    auto helper = std::make_unique<HelperProcess>();

    if (helper->initialiseFromCommandLine (commandLine, Isolation::workerUID))
        return helper;

    return nullptr;
}
