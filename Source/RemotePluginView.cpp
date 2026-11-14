#include "RemotePluginView.h"
#include "WebSocketClient.h"
#include "Theme.h"

RemotePluginView::RemotePluginView (const juce::String& h, int p, const juce::String& w)
    : juce::Thread ("LiveRack plugin picture"), host (h), what (w), port (p)
{
    // Sharp on a Retina screen, lighter on an ordinary one
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        captureScale = (float) juce::jlimit (1.0, 2.0, d->scale);

    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    startTimerHz (60);
    startThread();
}

RemotePluginView::~RemotePluginView()
{
    alive->store (false);
    stopped = true;
    signalThreadShouldExit();
    stopThread (3000);
    juce::PopupMenu::dismissAllActiveMenus();
}

//==============================================================================
// The network thread: connect, send what's queued, receive pictures and messages.
// A dropped link is tried again every second until the view goes.
void RemotePluginView::run()
{
    while (! threadShouldExit() && ! stopped.load())
    {
        const bool endedByServer = connectAndRun();
        if (endedByServer)
            return;

        auto flag = alive;
        juce::MessageManager::callAsync ([this, flag]
        {
            if (! flag->load() || ended)
                return;
            message = "Reconnecting...";
            repaint();
        });

        for (int i = 0; i < 10 && ! threadShouldExit(); ++i)
            wait (100);
    }
}

// True when the server ended the view (an error, or the window closed there)
bool RemotePluginView::connectAndRun()
{
    juce::StreamingSocket socket;
    if (! socket.connect (host, port, 3000))
        return false;

    WebSocketClient ws { socket };
    const auto query = "/view?" + what + "&scale=" + juce::String (captureScale, 2) + "&q=0.8";
    if (! ws.handshake (host, port, query))
        return false;

    juce::MemoryBlock data;
    auto lastPing = juce::Time::getMillisecondCounter();
    auto lastHeard = lastPing;
    auto flag = alive;

    while (! threadShouldExit())
    {
        const int ready = socket.waitUntilReady (true, 8);
        if (ready < 0)
            return false;

        for (int reads = 0; reads < 16 && socket.waitUntilReady (true, 0) > 0; ++reads)
        {
            const int type = ws.read (data);
            if (type < 0)
                return false;
            lastHeard = juce::Time::getMillisecondCounter();

            if (type == 1)
            {
                const auto text = data.toString();
                const auto t = juce::JSON::parse (text)["t"].toString();
                juce::MessageManager::callAsync ([this, flag, text] { if (flag->load()) received (text); });
                if (t == "error" || t == "closed")
                    return true;
            }
            else if (type == 2)
            {
                // A picture: decoded here, shown on the message thread, then acknowledged
                auto image = juce::ImageFileFormat::loadFrom (data.getData(), data.getSize());
                juce::MessageManager::callAsync ([this, flag, image]
                {
                    if (! flag->load())
                        return;
                    if (image.isValid())
                    {
                        picture = image;
                        const juce::Point<int> size (juce::roundToInt ((float) image.getWidth() / captureScale),
                                                     juce::roundToInt ((float) image.getHeight() / captureScale));
                        if (message.isNotEmpty() && ! ended)
                            message.clear();
                        repaint();
                        if (size != naturalSize)
                        {
                            naturalSize = size;
                            if (onNaturalSizeChanged != nullptr)
                                onNaturalSizeChanged();
                        }
                    }
                    const juce::ScopedLock sl (queueLock);
                    outgoing.add ("{\"t\":\"ack\"}");
                });
            }
        }

        juce::StringArray toSend;
        {
            const juce::ScopedLock sl (queueLock);
            toSend.swapWith (outgoing);
        }
        for (auto& m : toSend)
            if (! ws.sendText (m))
                return false;

        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastPing > 2000)
        {
            lastPing = now;
            if (! ws.sendText ("{\"t\":\"ping\"}"))
                return false;
        }
        if (now - lastHeard > 8000)
            return false;   // nothing at all, not even a pong: the link is dead
    }
    return false;
}

void RemotePluginView::received (const juce::String& text)
{
    const auto m = juce::JSON::parse (text);
    const auto t = m["t"].toString();

    if (t == "open")
    {
        if (! picture.isValid())
            message = "Waiting for the picture...";
        repaint();
    }
    else if (t == "error" || t == "closed")
    {
        ended = true;
        message = m["msg"].toString().isNotEmpty() ? m["msg"].toString() : juce::String ("The plugin window closed.");
        repaint();
    }
    else if (t == "menu")
    {
        showMenu (m["items"]);
    }
    else if (t == "menuEnd")
    {
        juce::PopupMenu::dismissAllActiveMenus();
    }
}

//==============================================================================
// A menu the plugin opened on the server, shown here; the choice goes back
void RemotePluginView::showMenu (const juce::var& items)
{
    auto paths = std::make_shared<std::map<int, juce::Array<juce::var>>>();
    int nextId = 1;

    std::function<juce::PopupMenu (const juce::var&, juce::Array<juce::var>)> build;
    build = [&] (const juce::var& list, juce::Array<juce::var> path)
    {
        juce::PopupMenu menu;
        if (auto* a = list.getArray())
            for (int i = 0; i < a->size(); ++i)
            {
                const auto& it = a->getReference (i);
                if ((bool) it["hidden"])
                    continue;
                if ((bool) it["sep"])
                {
                    menu.addSeparator();
                    continue;
                }
                auto here = path;
                here.add (i);
                if (it["sub"].isArray())
                {
                    menu.addSubMenu (it["title"].toString(), build (it["sub"], here), (bool) it["enabled"], nullptr, (bool) it["checked"]);
                    continue;
                }
                const int id = nextId++;
                (*paths)[id] = here;
                menu.addItem (id, it["title"].toString(), (bool) it["enabled"], (bool) it["checked"]);
            }
        return menu;
    };

    auto menu = build (items, {});
    const auto where = localAreaToGlobal (juce::Rectangle<int> (lastMouse.x, lastMouse.y, 1, 1));
    juce::Component::SafePointer<RemotePluginView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (where), [safe, paths] (int result)
    {
        if (safe == nullptr)
            return;
        auto* o = new juce::DynamicObject();
        if (result > 0 && paths->count (result) > 0)
        {
            o->setProperty ("t", "menu");
            o->setProperty ("path", (*paths)[result]);
        }
        else
        {
            o->setProperty ("t", "menuCancel");
        }
        safe->send (juce::var (o));
    });
}

//==============================================================================
void RemotePluginView::send (const juce::var& event)
{
    const juce::ScopedLock sl (queueLock);
    outgoing.add (juce::JSON::toString (event, true));
}

juce::String RemotePluginView::modsOf (juce::ModifierKeys k)
{
    juce::String s;
    if (k.isShiftDown())   s << "s";
    if (k.isAltDown())     s << "a";
    if (k.isCtrlDown())    s << "c";
    if (k.isCommandDown() && ! k.isCtrlDown()) s << "m";
    return s;
}

juce::Rectangle<float> RemotePluginView::pictureArea() const
{
    if (naturalSize.x <= 0 || naturalSize.y <= 0)
        return {};

    const auto b = getLocalBounds().toFloat();
    const float s = juce::jmin (1.0f, b.getWidth() / (float) naturalSize.x, b.getHeight() / (float) naturalSize.y);
    const float w = (float) naturalSize.x * s, h = (float) naturalSize.y * s;
    return { b.getX() + (b.getWidth() - w) / 2.0f, centred ? b.getY() + (b.getHeight() - h) / 2.0f : b.getY(), w, h };
}

juce::var RemotePluginView::pointerEvent (const juce::String& type, const juce::MouseEvent& e) const
{
    const auto area = pictureArea();
    auto* o = new juce::DynamicObject();
    o->setProperty ("t", type);
    o->setProperty ("x", area.isEmpty() ? 0.0 : juce::jlimit (0.0, 1.0, (double) ((e.position.x - area.getX()) / area.getWidth())));
    o->setProperty ("y", area.isEmpty() ? 0.0 : juce::jlimit (0.0, 1.0, (double) ((e.position.y - area.getY()) / area.getHeight())));
    o->setProperty ("mods", modsOf (e.mods));
    return juce::var (o);
}

void RemotePluginView::paint (juce::Graphics& g)
{
    const auto area = pictureArea();
    if (picture.isValid() && ! area.isEmpty())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (picture, area);
    }

    if (message.isNotEmpty())
    {
        auto box = getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - 20, 460), 64).toFloat();
        if (! picture.isValid())
            box = box.withY (juce::jmin ((float) getHeight() / 2.0f - 32.0f, 60.0f));
        g.setColour (Theme::raised.withAlpha (0.95f));
        g.fillRoundedRectangle (box, 8.0f);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText (message, box.reduced (14.0f, 6.0f).toNearestInt(), juce::Justification::centred, 3);
    }
}

void RemotePluginView::mouseMove (const juce::MouseEvent& e)
{
    lastMouse = e.getPosition();
    if (pictureArea().contains (e.position))
        pendingMove = pointerEvent ("move", e);
}

void RemotePluginView::mouseDown (const juce::MouseEvent& e)
{
    lastMouse = e.getPosition();
    grabKeyboardFocus();
    buttonDown = e.mods.isPopupMenu() ? "r" : "l";
    auto ev = pointerEvent ("down", e);
    ev.getDynamicObject()->setProperty ("b", buttonDown);
    ev.getDynamicObject()->setProperty ("n", juce::jlimit (1, 3, e.getNumberOfClicks()));
    pendingMove = juce::var();
    send (ev);
}

void RemotePluginView::mouseDrag (const juce::MouseEvent& e)
{
    lastMouse = e.getPosition();
    pendingMove = pointerEvent ("move", e);
}

void RemotePluginView::mouseUp (const juce::MouseEvent& e)
{
    lastMouse = e.getPosition();
    if (! pendingMove.isVoid())
    {
        send (pendingMove);
        pendingMove = juce::var();
    }
    auto ev = pointerEvent ("up", e);
    ev.getDynamicObject()->setProperty ("b", buttonDown);
    ev.getDynamicObject()->setProperty ("n", juce::jlimit (1, 3, e.getNumberOfClicks()));
    send (ev);
}

void RemotePluginView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    auto ev = pointerEvent ("wheel", e);
    ev.getDynamicObject()->setProperty ("dx", wheel.deltaX * 200.0f);
    ev.getDynamicObject()->setProperty ("dy", wheel.deltaY * 200.0f);
    send (ev);
}

bool RemotePluginView::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();

    // The app's own shortcuts (save, open...) and Esc stay with LiveRack
    if (mods.isCommandDown() || key == juce::KeyPress::escapeKey)
        return false;

    auto* o = new juce::DynamicObject();
    o->setProperty ("mods", modsOf (mods));
    const int code = key.getKeyCode();
    juce::String name;
    if      (code == juce::KeyPress::returnKey)    name = "Enter";
    else if (code == juce::KeyPress::backspaceKey) name = "Backspace";
    else if (code == juce::KeyPress::deleteKey)    name = "Backspace";
    else if (code == juce::KeyPress::tabKey)       name = "Tab";
    else if (code == juce::KeyPress::leftKey)      name = "ArrowLeft";
    else if (code == juce::KeyPress::rightKey)     name = "ArrowRight";
    else if (code == juce::KeyPress::upKey)        name = "ArrowUp";
    else if (code == juce::KeyPress::downKey)      name = "ArrowDown";

    if (name.isNotEmpty())
    {
        o->setProperty ("t", "key");
        o->setProperty ("k", name);
    }
    else if (key.getTextCharacter() >= ' ')
    {
        o->setProperty ("t", "text");
        o->setProperty ("s", juce::String::charToString (key.getTextCharacter()));
    }
    else
    {
        return false;
    }

    send (juce::var (o));
    return true;
}

void RemotePluginView::timerCallback()
{
    if (! pendingMove.isVoid())
    {
        send (pendingMove);
        pendingMove = juce::var();
    }
}

//==============================================================================
RemotePluginWindow::RemotePluginWindow (const juce::String& title, const juce::String& host, int port,
                                        const juce::String& w, std::function<void()> closed)
    : juce::DocumentWindow (title, Theme::background, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      what (w), onClose (std::move (closed))
{
    setUsingNativeTitleBar (true);
    auto* view = new RemotePluginView (host, port, w);
    view->setCentred (true);
    view->setSize (480, 320);
    setContentOwned (view, true);
    setResizable (true, false);
    centreWithSize (480, 320);

    juce::Component::SafePointer<RemotePluginWindow> safe (this);
    view->onNaturalSizeChanged = [safe, view]
    {
        // Opens at the plugin's own size, once; after that the user decides
        if (safe == nullptr || safe->sized)
            return;
        safe->sized = true;
        const auto n = view->getNaturalSize();
        safe->setContentComponentSize (n.x, n.y);
    };
    setVisible (true);
}

void RemotePluginWindow::closeButtonPressed()
{
    if (onClose != nullptr)
        onClose();
}
