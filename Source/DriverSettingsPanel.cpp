#include "DriverSettingsPanel.h"
#include "Theme.h"

DriverSettingsPanel::DriverSettingsPanel (LiveConnection& c, int d) : live (c), driver (d)
{
    for (auto* box : { &type, &device, &rate, &buffer })
        addAndMakeVisible (box);

    type.setTextWhenNothingSelected ("Device type");
    device.setTextWhenNothingSelected ("Choose a device");
    rate.setTextWhenNothingSelected ("-");
    buffer.setTextWhenNothingSelected ("-");

    type.onChange   = [this] { apply (true, false); };
    device.onChange = [this] { apply (false, true); };
    rate.onChange   = [this] { apply (false, false); };
    buffer.onChange = [this] { apply (false, false); };

    live.addChangeListener (this);
    live.refreshDriverOptions();
    fill();
    setSize (520, 300);
}

DriverSettingsPanel::~DriverSettingsPanel()
{
    live.removeChangeListener (this);
}

void DriverSettingsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    fill();
}

// A change takes the server a moment: ask what the choices are now
void DriverSettingsPanel::timerCallback()
{
    stopTimer();
    waiting = false;
    live.refreshDriverOptions();
    fill();
}

void DriverSettingsPanel::fill()
{
    if (waiting)
        return;   // boxes stay as picked until the server has answered

    const auto o = live.getDriverOptions (driver);

    type.clear (juce::dontSendNotification);
    device.clear (juce::dontSendNotification);
    rate.clear (juce::dontSendNotification);
    buffer.clear (juce::dontSendNotification);

    for (int i = 0; i < (int) o.types.size(); ++i)
    {
        type.addItem (o.types[(size_t) i].name, i + 1);
        if (o.types[(size_t) i].name == o.type)
        {
            type.setSelectedId (i + 1, juce::dontSendNotification);
            const auto& names = o.types[(size_t) i].devices;
            for (int n = 0; n < names.size(); ++n)
                device.addItem (names[n], n + 1);
            device.setSelectedId (names.indexOf (o.device) + 1, juce::dontSendNotification);
        }
    }

    for (int i = 0; i < o.rates.size(); ++i)
    {
        rate.addItem (juce::String (o.rates[i] / 1000.0, 1) + " kHz", i + 1);
        if (std::abs (o.rates[i] - o.rate) < 1.0)
            rate.setSelectedId (i + 1, juce::dontSendNotification);
    }

    for (int i = 0; i < o.buffers.size(); ++i)
    {
        juce::String text = juce::String (o.buffers[i]) + " samples";
        if (o.rate > 0)
            text << "  (" << juce::String (o.buffers[i] * 1000.0 / o.rate, 1) << " ms)";
        buffer.addItem (text, i + 1);
        if (o.buffers[i] == o.buffer)
            buffer.setSelectedId (i + 1, juce::dontSendNotification);
    }

    const auto st = live.getDriverStatus (driver);
    status = ! live.isConnected() ? "Not connected to " + live.getServerName()
           : ! o.known            ? "Asking " + live.getServerName() + juce::String::fromUTF8 ("\xe2\x80\xa6")
           : st.open              ? "Running: " + juce::String (st.numIns) + " in, " + juce::String (st.numOuts) + " out"
                                  : "No device open";
    repaint();
}

void DriverSettingsPanel::apply (bool typeChanged, bool deviceChanged)
{
    const auto o = live.getDriverOptions (driver);

    // A new type or device brings its own rates and buffer sizes
    const auto newType = typeChanged ? type.getText() : juce::String();
    const auto newDevice = deviceChanged ? device.getText() : juce::String();
    const double newRate = (typeChanged || deviceChanged) ? 0.0 : o.rates[rate.getSelectedId() - 1];
    const int newBuffer = (typeChanged || deviceChanged) ? 0 : o.buffers[buffer.getSelectedId() - 1];

    const auto error = live.setDriver (driver, newType, newDevice, newRate, newBuffer);
    if (error.isNotEmpty())
        status = error;

    if (live.isLocal())
    {
        fill();
        if (error.isNotEmpty())
            status = error;
        repaint();
    }
    else
    {
        waiting = true;
        status = "Changing" + juce::String::fromUTF8 ("\xe2\x80\xa6");
        repaint();
        startTimer (1200);
    }
}

void DriverSettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    auto area = getLocalBounds().reduced (20);

    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (12.0f));
    const char* labels[] = { "Device type", "Device", "Sample rate", "Buffer size" };
    for (auto* label : labels)
    {
        g.drawText (label, area.removeFromTop (34).removeFromLeft (120), juce::Justification::centredLeft);
        area.removeFromTop (10);
    }

    area.removeFromTop (6);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText (status, area.removeFromTop (40), juce::Justification::topLeft, 2);

    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawFittedText ("Changes apply straight away on " + live.getServerName() + ". Audio stops for a moment while the driver restarts.",
                      area, juce::Justification::topLeft, 2);
}

void DriverSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (20);
    for (auto* box : { &type, &device, &rate, &buffer })
    {
        box->setBounds (area.removeFromTop (34).withTrimmedLeft (130));
        area.removeFromTop (10);
    }
}
