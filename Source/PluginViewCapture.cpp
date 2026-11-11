#include "PluginViewCapture.h"

//==============================================================================
// Shared parts

void PluginViewCapture::windowClosed (const juce::String& why)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("t", "closed");
    o->setProperty ("msg", why);
    postMessage (juce::var (o));
    ended = true;
    stop();
}

bool PluginViewCapture::takeMessage (juce::String& json)
{
    const juce::ScopedLock sl (messageLock);
    if (messages.isEmpty())
        return false;
    json = messages[0];
    messages.remove (0);
    return true;
}

void PluginViewCapture::postMessage (const juce::var& object)
{
    const auto json = juce::JSON::toString (object, true);
    const juce::ScopedLock sl (messageLock);
    if (messages.size() < 64)
        messages.add (json);
}

void PluginViewCapture::postError (const juce::String& message)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("t", "error");
    o->setProperty ("msg", message);
    postMessage (juce::var (o));
    ended = true;
}

int PluginViewCapture::modifierFlagsFrom (const juce::String& mods)
{
    int f = 0;
    if (mods.containsChar ('s')) f |= 1;
    if (mods.containsChar ('a')) f |= 2;
    if (mods.containsChar ('c')) f |= 4;
    if (mods.containsChar ('m')) f |= 8;
    return f;
}

#if ! JUCE_MAC
//==============================================================================
// JUCE-only capture: draws the window's components into an image and sends
// events through its peer. It can't see native plugin views (those draw
// outside JUCE), so it's only used by Linux preview builds. Windows has its
// own in PluginViewCaptureWindows.cpp.
namespace
{
class JuceViewCapture : public PluginViewCapture,
                        private juce::Timer
{
public:
    JuceViewCapture (juce::DocumentWindow& w, Options o) : window (&w), options (o)
    {
        startTimerHz (25);
    }

    ~JuceViewCapture() override { stopTimer(); }

    void stop() override
    {
        stopTimer();
        window = nullptr;
    }

    bool takeFrame (juce::MemoryBlock& jpeg) override
    {
        juce::Image image;
        {
            const juce::ScopedLock sl (lock);
            if (latestNum == takenNum)
                return false;
            takenNum = latestNum;
            image = latest;
        }

        if (! image.isValid())
            return false;

        // Skip frames that didn't change
        const auto hash = hashOf (image);
        if (hash == lastHash)
            return false;
        lastHash = hash;

        juce::MemoryOutputStream out (jpeg, false);
        jpeg.reset();
        juce::JPEGImageFormat format;
        format.setQuality (quality.load());
        return format.writeImageToStream (image, out);
    }

    void input (const juce::var& e) override
    {
        auto* content = window != nullptr ? window->getContentComponent() : nullptr;
        auto* peer = window != nullptr ? window->getPeer() : nullptr;
        if (content == nullptr || peer == nullptr)
            return;

        const auto t = e["t"].toString();

        if (t == "opts")
        {
            options.scale = juce::jlimit (0.5f, 3.0f, (float) e.getProperty ("scale", options.scale));
            quality = juce::jlimit (0.2f, 0.95f, (float) e.getProperty ("q", quality.load()));
            return;
        }

        const juce::Point<float> local ((float) (double) e["x"] * (float) content->getWidth(),
                                        (float) (double) e["y"] * (float) content->getHeight());
        const auto pos = window->getLocalPoint (content, local);
        const auto time = juce::Time::currentTimeMillis();
        const int f = modifierFlagsFrom (e["mods"].toString());

        int keyMods = 0;
        if (f & 1) keyMods |= juce::ModifierKeys::shiftModifier;
        if (f & 2) keyMods |= juce::ModifierKeys::altModifier;
        if (f & 4) keyMods |= juce::ModifierKeys::ctrlModifier;
        if (f & 8) keyMods |= juce::ModifierKeys::commandModifier;

        auto send = [&] (int buttons, float pressure)
        {
            const juce::ModifierKeys mods (keyMods | buttons);
            juce::ModifierKeys::currentModifiers = mods;
            peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, pos, mods, pressure,
                                    juce::MouseInputSource::defaultOrientation, time);
        };

        if (t == "down")
        {
            button = e["b"].toString() == "r" ? juce::ModifierKeys::rightButtonModifier
                                              : juce::ModifierKeys::leftButtonModifier;
            if (! window->isActiveWindow())
                window->toFront (true); // typing goes to the window clicked in
            send (0, 0.0f);       // hover first, like a real mouse
            send (button, 1.0f);
        }
        else if (t == "move")
        {
            send (button, 1.0f);
        }
        else if (t == "up")
        {
            send (0, 0.0f);
            button = 0;
        }
        else if (t == "wheel")
        {
            juce::MouseWheelDetails wheel;
            wheel.deltaX = (float) (double) e["dx"] / 200.0f;
            wheel.deltaY = (float) (double) e["dy"] / 200.0f;
            wheel.isReversed = false;
            wheel.isSmooth = true;
            wheel.isInertial = false;
            peer->handleMouseWheel (juce::MouseInputSource::InputSourceType::mouse, pos, time, wheel);
        }
        else if (t == "key")
        {
            const auto k = e["k"].toString();
            int code = 0;
            if      (k == "Enter")      code = juce::KeyPress::returnKey;
            else if (k == "Backspace")  code = juce::KeyPress::backspaceKey;
            else if (k == "Tab")        code = juce::KeyPress::tabKey;
            else if (k == "Escape")     code = juce::KeyPress::escapeKey;
            else if (k == "ArrowLeft")  code = juce::KeyPress::leftKey;
            else if (k == "ArrowRight") code = juce::KeyPress::rightKey;
            else if (k == "ArrowUp")    code = juce::KeyPress::upKey;
            else if (k == "ArrowDown")  code = juce::KeyPress::downKey;
            if (code != 0)
                peer->handleKeyPress (juce::KeyPress (code, juce::ModifierKeys (keyMods), 0));
        }
        else if (t == "text")
        {
            for (auto c : e["s"].toString())
                peer->handleKeyPress (juce::KeyPress ((int) c, juce::ModifierKeys (keyMods), c));
        }
    }

private:
    void timerCallback() override
    {
        auto* content = window != nullptr ? window->getContentComponent() : nullptr;
        if (content == nullptr || content->getWidth() <= 0 || content->getHeight() <= 0)
            return;

        {
            const juce::ScopedLock sl (lock);
            if (latestNum != takenNum)
                return; // the remote hasn't taken the last one yet
        }

        auto image = content->createComponentSnapshot (content->getLocalBounds(), true, options.scale);
        const juce::ScopedLock sl (lock);
        latest = image;
        ++latestNum;
    }

    static juce::uint64 hashOf (const juce::Image& image)
    {
        const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
        juce::uint64 h = 1469598103934665603ull ^ (juce::uint64) image.getWidth() ^ ((juce::uint64) image.getHeight() << 32);
        for (int y = 0; y < data.height; y += 2)
        {
            auto* row = reinterpret_cast<const juce::uint32*> (data.getLinePointer (y));
            for (int x = 0; x < data.width; x += 3)
                h = (h ^ row[x]) * 1099511628211ull;
        }
        return h;
    }

    juce::Component::SafePointer<juce::DocumentWindow> window;
    Options options;
    std::atomic<float> quality { options.quality };
    int button = 0;

    juce::CriticalSection lock;
    juce::Image latest;
    juce::uint64 latestNum = 0, takenNum = 0, lastHash = 0;
};
} // namespace

#if JUCE_WINDOWS
std::shared_ptr<PluginViewCapture> createWindowsViewCapture (juce::DocumentWindow&, PluginViewCapture::Options, juce::String&);
#endif

bool PluginViewCapture::isSupported()
{
    return true;
}

std::shared_ptr<PluginViewCapture> PluginViewCapture::create (juce::DocumentWindow& window, Options options, juce::String& error)
{
   #if JUCE_WINDOWS
    return createWindowsViewCapture (window, options, error);
   #else
    juce::ignoreUnused (error);
    return std::make_shared<JuceViewCapture> (window, options);
   #endif
}
#endif
