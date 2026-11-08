#include "MidiControl.h"

juce::String MidiMapping::describeControl() const
{
    juce::String s;
    if (kind == 0)      s << "Note " << juce::MidiMessage::getMidiNoteName (number, true, true, 3);
    else if (kind == 1) s << "CC " << number;
    else                s << "Program " << (number + 1);
    s << " ch " << channel;
    if (device.isNotEmpty())
        s << " (" << device << ")";
    return s;
}

juce::String MidiMapping::describeTarget (const RackEngine& engine) const
{
    if (! isParameter)
        return action.describe (engine);

    if (followSelected)
        return "Selected rack, plugin " + juce::String (slotPosition + 1) + ": " + paramName;

    int r, s;
    if (engine.findSlot (slotId, r, s))
        if (auto* p = engine.getPlugin (r, s))
            return engine.getRackConfig (r).name + " " + p->getName() + ": " + paramName;

    return "(removed plugin): " + paramName;
}

//==============================================================================
MidiControl::MidiControl (RackEngine& e, ActionHost& h) : engine (e), host (h)
{
    load();
    openDevices();
    startTimerHz (30);
}

MidiControl::~MidiControl()
{
    stopTimer();
    for (auto& d : devices)
        if (d->input != nullptr)
            d->input->stop();
    devices.clear();
}

void MidiControl::openDevices()
{
    for (auto& d : devices)
        if (d->input != nullptr)
            d->input->stop();
    devices.clear();

    const auto inputs = juce::MidiInput::getAvailableDevices();
    const auto outputs = juce::MidiOutput::getAvailableDevices();
    lastSeenInputs.clear();
    for (auto& i : inputs)
        lastSeenInputs.add (i.name);

    for (auto& name : addedDevices)
    {
        auto d = std::make_unique<Device>();
        d->name = name;

        for (auto& info : inputs)
            if (info.name == name)
            {
                d->info = info;
                d->input = juce::MidiInput::openDevice (info.identifier, this);
                if (d->input != nullptr)
                    d->input->start();
                break;
            }

        // The output with the same name gets LED / fader feedback
        if (d->input != nullptr)
            for (auto& o : outputs)
                if (o.name == name)
                {
                    d->output = juce::MidiOutput::openDevice (o.identifier);
                    break;
                }

        devices.push_back (std::move (d));
    }

    lastFeedback.clear();
}

void MidiControl::checkForChanges()
{
    juce::StringArray now;
    for (auto& i : juce::MidiInput::getAvailableDevices())
        now.add (i.name);

    if (now != lastSeenInputs)
    {
        openDevices(); // reconnects added devices that came back
        if (onChanged != nullptr) onChanged();
    }
}

void MidiControl::rescan()
{
    openDevices();
    if (onChanged != nullptr)
        onChanged();
}

std::vector<MidiControl::DeviceInfo> MidiControl::getDevices() const
{
    std::vector<DeviceInfo> list;
    for (auto& d : devices)
    {
        DeviceInfo i;
        i.name = d->name;
        i.connected = d->input != nullptr;
        i.lastActivity = d->lastActivity;
        for (auto& m : mappings)
            if (m.device == d->name)
                ++i.mappings;
        list.push_back (i);
    }
    return list;
}

juce::StringArray MidiControl::getDevicesToAdd() const
{
    juce::StringArray names;
    for (auto& i : juce::MidiInput::getAvailableDevices())
        if (! addedDevices.contains (i.name))
            names.addIfNotAlreadyThere (i.name);
    return names;
}

void MidiControl::addDevice (const juce::String& name)
{
    addedDevices.addIfNotAlreadyThere (name);
    save();
    rescan();
}

void MidiControl::removeDevice (const juce::String& name, bool keepMappings)
{
    addedDevices.removeString (name);
    if (! keepMappings)
        mappings.erase (std::remove_if (mappings.begin(), mappings.end(),
                                        [&name] (const MidiMapping& m) { return m.device == name; }),
                        mappings.end());
    save();
    rescan();
}

void MidiControl::removeMapping (int index)
{
    if (juce::isPositiveAndBelow (index, (int) mappings.size()))
    {
        mappings.erase (mappings.begin() + index);
        lastFeedback.clear();
        save();
        if (onChanged != nullptr) onChanged();
    }
}

void MidiControl::clearMappings()
{
    mappings.clear();
    lastFeedback.clear();
    save();
    if (onChanged != nullptr) onChanged();
}

//==============================================================================
void MidiControl::learnAction (const Action& a)
{
    learnTemplate = {};
    learnTemplate.action = a;
    learning = true;
    learnStartedMs = juce::Time::getMillisecondCounter();
    if (onChanged != nullptr) onChanged();
}

void MidiControl::learnParameter (int rack, int slot, int paramIndex, bool followSelected)
{
    learnTemplate = {};
    learnTemplate.isParameter = true;
    learnTemplate.followSelected = followSelected;
    learnTemplate.slotId = engine.getSlotId (rack, slot);
    learnTemplate.slotPosition = slot;
    learnTemplate.paramIndex = paramIndex;

    if (auto* p = engine.getPlugin (rack, slot))
        if (auto* param = p->getParameters()[paramIndex])
            learnTemplate.paramName = param->getName (40);

    learning = true;
    learnStartedMs = juce::Time::getMillisecondCounter();
    if (onChanged != nullptr) onChanged();
}

void MidiControl::cancelLearn()
{
    learning = false;
    if (onChanged != nullptr) onChanged();
}

juce::String MidiControl::getLearnText() const
{
    if (! learning)
        return {};
    return "MIDI Learn: move a control for \"" + learnTemplate.describeTarget (engine) + "\"";
}

//==============================================================================
void MidiControl::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m)
{
    if (! (m.isNoteOn() || m.isController() || m.isProgramChange()))
        return;

    const juce::ScopedLock sl (queueLock);
    if (queue.size() < 512)
        queue.push_back ({ source != nullptr ? source->getName() : juce::String(), m });
}

void MidiControl::timerCallback()
{
    std::vector<Incoming> items;
    {
        const juce::ScopedLock sl (queueLock);
        items.swap (queue);
    }

    for (auto& in : items)
        handle (in);

    if (learning && juce::Time::getMillisecondCounter() - learnStartedMs > 30000)
        cancelLearn();

    if (++ticks % 3 == 0)
        sendFeedback();

    if (ticks % 60 == 0)
        checkForChanges();
}

void MidiControl::handle (const Incoming& in)
{
    const auto& m = in.message;

    for (auto& d : devices)
        if (d->name == in.device)
            d->lastActivity = juce::Time::getMillisecondCounter();

    MidiMapping control;
    control.device = in.device;
    control.channel = m.getChannel();
    control.kind = m.isNoteOn() ? 0 : m.isController() ? 1 : 2;
    control.number = m.isNoteOn() ? m.getNoteNumber() : m.isController() ? m.getControllerNumber() : m.getProgramChangeNumber();

    if (learning)
    {
        // Ignore a CC going back to 0 (button release) while learning
        if (control.kind == 1 && m.getControllerValue() == 0 && ! learnTemplate.isParameter)
            return;

        auto mapping = learnTemplate;
        mapping.device = control.device;
        mapping.kind = control.kind;
        mapping.channel = control.channel;
        mapping.number = control.number;

        mappings.erase (std::remove_if (mappings.begin(), mappings.end(),
                                        [&mapping] (const MidiMapping& x) { return x.sameControl (mapping); }),
                        mappings.end());
        mappings.push_back (mapping);
        learning = false;
        lastFeedback.clear();
        save();
        if (onChanged != nullptr) onChanged();
        return;
    }

    for (auto& mapping : mappings)
        if ((mapping.device.isEmpty() || mapping.device == control.device)
            && mapping.kind == control.kind && mapping.channel == control.channel && mapping.number == control.number)
            apply (mapping, m);
}

void MidiControl::apply (const MidiMapping& mapping, const juce::MidiMessage& m)
{
    if (! mapping.isParameter)
    {
        // Buttons: a note-on, a CC above 0 or a program change is a press
        if (m.isController() && m.getControllerValue() == 0)
            return;
        host.performAction (mapping.action, -1);
        return;
    }

    int rack = -1, slot = -1;
    if (mapping.followSelected) { rack = host.getSelectedRack(); slot = mapping.slotPosition; }
    else if (! engine.findSlot (mapping.slotId, rack, slot)) return;

    auto* plugin = engine.getPlugin (rack, slot);
    if (plugin == nullptr)
        return;

    auto* param = plugin->getParameters()[mapping.paramIndex];
    if (param == nullptr)
        return;

    float value = 0.0f;
    if (m.isController())  value = m.getControllerValue() / 127.0f;
    else if (m.isNoteOn()) value = param->getValue() > 0.5f ? 0.0f : 1.0f; // notes toggle
    else                   return;

    param->beginChangeGesture();
    param->setValueNotifyingHost (value);
    param->endChangeGesture();
}

void MidiControl::sendFeedback()
{
    for (int i = 0; i < (int) mappings.size(); ++i)
    {
        const auto& mapping = mappings[(size_t) i];
        if (mapping.isParameter || mapping.kind == 2)
            continue;

        const bool lit = host.isActionActive (mapping.action);
        auto it = lastFeedback.find (i);
        if (it != lastFeedback.end() && it->second == lit)
            continue;

        lastFeedback[i] = lit;

        for (auto& d : devices)
        {
            if (d->output == nullptr || (mapping.device.isNotEmpty() && mapping.device != d->name))
                continue;

            const auto msg = mapping.kind == 0
                ? juce::MidiMessage::noteOn (mapping.channel, mapping.number, (juce::uint8) (lit ? 127 : 0))
                : juce::MidiMessage::controllerEvent (mapping.channel, mapping.number, lit ? 127 : 0);
            d->output->sendMessageNow (msg);
        }
    }
}

//==============================================================================
void MidiControl::save() const
{
    juce::XmlElement xml ("MIDICONTROL");

    for (auto& name : addedDevices)
        xml.createNewChildElement ("DEVICE")->setAttribute ("name", name);

    for (auto& m : mappings)
    {
        auto* e = xml.createNewChildElement ("MAP");
        e->setAttribute ("device", m.device);
        e->setAttribute ("kind", m.kind);
        e->setAttribute ("channel", m.channel);
        e->setAttribute ("number", m.number);
        e->setAttribute ("isParameter", m.isParameter);
        e->setAttribute ("followSelected", m.followSelected);
        e->setAttribute ("slotId", m.slotId);
        e->setAttribute ("slotPosition", m.slotPosition);
        e->setAttribute ("paramIndex", m.paramIndex);
        e->setAttribute ("paramName", m.paramName);
        m.action.writeTo (*e);
    }

    xml.writeTo (RackEngine::getSettingsDir().getChildFile ("midi-control.xml"));
}

void MidiControl::load()
{
    auto xml = juce::parseXML (RackEngine::getSettingsDir().getChildFile ("midi-control.xml"));
    if (xml == nullptr)
        return;

    for (auto* e : xml->getChildWithTagNameIterator ("DEVICE"))
        addedDevices.addIfNotAlreadyThere (e->getStringAttribute ("name"));

    for (auto* e : xml->getChildWithTagNameIterator ("MAP"))
    {
        MidiMapping m;
        m.device = e->getStringAttribute ("device");
        m.kind = juce::jlimit (0, 2, e->getIntAttribute ("kind", 1));
        m.channel = juce::jlimit (1, 16, e->getIntAttribute ("channel", 1));
        m.number = e->getIntAttribute ("number", 0);
        m.isParameter = e->getBoolAttribute ("isParameter", false);
        m.followSelected = e->getBoolAttribute ("followSelected", true);
        m.slotId = e->getIntAttribute ("slotId", 0);
        m.slotPosition = e->getIntAttribute ("slotPosition", 0);
        m.paramIndex = e->getIntAttribute ("paramIndex", 0);
        m.paramName = e->getStringAttribute ("paramName");
        m.action = Action::readFrom (*e);
        mappings.push_back (m);
    }
}
