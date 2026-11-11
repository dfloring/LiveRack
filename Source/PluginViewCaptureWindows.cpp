#include "PluginViewCapture.h"

#if JUCE_WINDOWS
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// Plugin windows on the web remote, Windows.
//
// While the remote shows a plugin, its window is kept on top, and that part
// of the screen is copied about 25 times a second. Copying the screen (rather
// than asking the window to draw itself) picks up plugins that draw with the
// graphics card and popup menus opened over the window.
//
// Taps, drags, scrolls and typing are played back as real mouse and keyboard
// input. That moves this computer's mouse pointer, which is fine on a server
// nobody is using.
//
// The screen must be on and Windows signed in and unlocked: a locked screen
// can't be copied.
namespace
{
class WindowsViewCapture final : public PluginViewCapture,
                                 private juce::Timer
{
public:
    WindowsViewCapture (juce::DocumentWindow& w, Options o) : window (&w), options (o)
    {
        wasOnTop = w.isAlwaysOnTop();
        w.setAlwaysOnTop (true);
        keepOnScreen();
        w.toFront (false);
        startTimerHz (25);
    }

    ~WindowsViewCapture() override
    {
        stopTimer();
        releaseEverything();
    }

    void stop() override
    {
        stopTimer();
        releaseEverything();

        if (window != nullptr && ! wasOnTop)
            window->setAlwaysOnTop (false);

        window = nullptr;
    }

    //==========================================================================
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

        const auto hash = hashOf (image);
        if (hash == lastHash)
            return false; // nothing changed on screen
        lastHash = hash;

        jpeg.reset();
        juce::MemoryOutputStream out (jpeg, false);
        juce::JPEGImageFormat format;
        format.setQuality (quality.load());
        return format.writeImageToStream (image, out);
    }

    //==========================================================================
    void input (const juce::var& e) override
    {
        const auto t = e["t"].toString();

        if (t == "opts")
        {
            options.scale = juce::jlimit (0.5f, 3.0f, (float) e.getProperty ("scale", options.scale));
            quality = juce::jlimit (0.2f, 0.95f, (float) e.getProperty ("q", quality.load()));
            return;
        }

        if (t == "menu" || t == "menuCancel")
            return; // Windows menus are part of the picture and are tapped directly

        RECT r;
        if (! contentRect (r))
            return;

        const int x = r.left + juce::jlimit (0, juce::jmax (0, (int) (r.right - r.left) - 1),
                                             juce::roundToInt ((double) e["x"] * (r.right - r.left)));
        const int y = r.top + juce::jlimit (0, juce::jmax (0, (int) (r.bottom - r.top) - 1),
                                            juce::roundToInt ((double) e["y"] * (r.bottom - r.top)));
        const int flags = modifierFlagsFrom (e["mods"].toString());

        if (t == "down")
        {
            bringToFront();
            setModifiers (flags);
            SetCursorPos (x, y);
            rightButton = e["b"].toString() == "r";
            mouseButton (true);
            buttonDown = true;
        }
        else if (t == "move")
        {
            SetCursorPos (x, y);
        }
        else if (t == "up")
        {
            SetCursorPos (x, y);
            if (buttonDown)
                mouseButton (false);
            buttonDown = false;
            setModifiers (0);
        }
        else if (t == "wheel")
        {
            SetCursorPos (x, y);
            setModifiers (flags);
            // The remote sends pixels; Windows counts 120 per notch of about 40 pixels
            if (const int dy = juce::roundToInt ((double) e["dy"] * 3.0); dy != 0) wheel (MOUSEEVENTF_WHEEL, dy);
            if (const int dx = juce::roundToInt ((double) e["dx"] * -3.0); dx != 0) wheel (MOUSEEVENTF_HWHEEL, dx);
            setModifiers (0);
        }
        else if (t == "key" || t == "text")
        {
            bringToFront();
            setModifiers (flags);
            if (t == "key")
                sendKey (e["k"].toString());
            else
                sendText (e["s"].toString());
            setModifiers (0);
        }
    }

private:
    //==========================================================================
    HWND windowHandle() const
    {
        auto* peer = window != nullptr ? window->getPeer() : nullptr;
        return peer != nullptr ? (HWND) peer->getNativeHandle() : nullptr;
    }

    // The plugin's part of the window, in screen pixels
    bool contentRect (RECT& r) const
    {
        auto* content = window != nullptr ? window->getContentComponent() : nullptr;
        const auto hwnd = windowHandle();

        if (content == nullptr || hwnd == nullptr || IsIconic (hwnd) || window->getWidth() <= 0)
            return false;

        RECT client;
        if (! GetClientRect (hwnd, &client))
            return false;

        POINT origin { 0, 0 };
        ClientToScreen (hwnd, &origin);

        // JUCE works in points; the screen in pixels (display scaling)
        const double ratio = (double) (client.right - client.left) / (double) window->getWidth();
        const auto area = window->getLocalArea (content, content->getLocalBounds());

        r.left   = origin.x + juce::roundToInt (area.getX() * ratio);
        r.top    = origin.y + juce::roundToInt (area.getY() * ratio);
        r.right  = r.left + juce::roundToInt (area.getWidth() * ratio);
        r.bottom = r.top + juce::roundToInt (area.getHeight() * ratio);
        return r.right > r.left && r.bottom > r.top;
    }

    // Only what's on screen can be copied: move the window fully onto its display
    void keepOnScreen()
    {
        if (window == nullptr)
            return;

        if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (window->getScreenBounds()))
        {
            const auto area = display->userArea;
            auto b = window->getBounds();
            b.setPosition (juce::jlimit (area.getX(), juce::jmax (area.getX(), area.getRight() - b.getWidth()), b.getX()),
                           juce::jlimit (area.getY(), juce::jmax (area.getY(), area.getBottom() - b.getHeight()), b.getY()));
            window->setBounds (b);
        }
    }

    void bringToFront()
    {
        if (window == nullptr)
            return;

        window->toFront (true);

        if (const auto hwnd = windowHandle(); hwnd != nullptr && GetForegroundWindow() != hwnd)
        {
            // Windows only lets the active program take the focus; a tap of Alt
            // counts as the user's own input and lifts that restriction
            keyEvent (VK_MENU, true);
            keyEvent (VK_MENU, false);
            SetForegroundWindow (hwnd);
        }
    }

    //==========================================================================
    void timerCallback() override
    {
        if (window == nullptr || ended.load())
        {
            stopTimer();
            return;
        }

        {
            const juce::ScopedLock sl (lock);
            if (latestNum != takenNum)
                return; // the remote hasn't taken the last one yet
        }

        RECT r;
        if (! contentRect (r))
            return;

        juce::Image image;
        if (! grab (r, image))
        {
            if (++failures == 50)
                postError ("Can't copy the screen of the LiveRack computer. It needs to be signed in, unlocked, and have a display on (a dummy display plug works).");
            return;
        }
        failures = 0;

        // Smaller pictures when the remote asked for them
        const auto area = window->getLocalArea (window->getContentComponent(), window->getContentComponent()->getLocalBounds());
        const int wantedWidth = juce::roundToInt (area.getWidth() * options.scale);
        if (wantedWidth > 0 && image.getWidth() > wantedWidth * 1.05)
            image = image.rescaled (wantedWidth, juce::roundToInt (image.getHeight() * (double) wantedWidth / image.getWidth()),
                                    juce::Graphics::mediumResamplingQuality);

        const juce::ScopedLock sl (lock);
        latest = image;
        ++latestNum;
    }

    static bool grab (const RECT& r, juce::Image& result)
    {
        const int w = (int) (r.right - r.left), h = (int) (r.bottom - r.top);
        if (w <= 0 || h <= 0 || w > 8192 || h > 8192)
            return false;

        HDC screen = GetDC (nullptr);
        if (screen == nullptr)
            return false;

        HDC memory = CreateCompatibleDC (screen);

        BITMAPINFO info {};
        info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h; // top row first
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection (screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        bool ok = false;

        if (bitmap != nullptr && bits != nullptr)
        {
            auto old = SelectObject (memory, bitmap);
            ok = BitBlt (memory, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT) != FALSE;
            GdiFlush();

            if (ok)
            {
                // Windows' pixels are blue, green, red, unused: the same order as JUCE's ARGB
                result = juce::Image (juce::Image::ARGB, w, h, false);
                const juce::Image::BitmapData data (result, juce::Image::BitmapData::writeOnly);
                auto* src = static_cast<const juce::uint32*> (bits);

                for (int y = 0; y < h; ++y)
                {
                    auto* dst = reinterpret_cast<juce::uint32*> (data.getLinePointer (y));
                    for (int x = 0; x < w; ++x)
                        dst[x] = src[(size_t) y * (size_t) w + (size_t) x] | 0xff000000u;
                }
            }

            SelectObject (memory, old);
        }

        if (bitmap != nullptr)
            DeleteObject (bitmap);
        DeleteDC (memory);
        ReleaseDC (nullptr, screen);
        return ok;
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

    //==========================================================================
    static void send (INPUT& in) { SendInput (1, &in, sizeof (INPUT)); }

    void mouseButton (bool down)
    {
        INPUT in {};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = rightButton ? (down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP)
                                    : (down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
        send (in);
    }

    static void wheel (DWORD flag, int amount)
    {
        INPUT in {};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = flag;
        in.mi.mouseData = (DWORD) amount;
        send (in);
    }

    static void keyEvent (WORD vk, bool down)
    {
        INPUT in {};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN || vk == VK_DELETE)
            in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        send (in);
    }

    // Holds Shift / Alt (Option) / Ctrl (the remote's Ctrl and Cmd both) as asked
    void setModifiers (int flags)
    {
        const bool want[3] = { (flags & 1) != 0, (flags & 2) != 0, (flags & (4 | 8)) != 0 };
        const WORD keys[3] = { VK_SHIFT, VK_MENU, VK_CONTROL };

        for (int i = 0; i < 3; ++i)
            if (want[i] != held[i])
            {
                keyEvent (keys[i], want[i]);
                held[i] = want[i];
            }
    }

    void sendKey (const juce::String& k)
    {
        WORD vk = 0;
        if      (k == "Enter")      vk = VK_RETURN;
        else if (k == "Tab")        vk = VK_TAB;
        else if (k == "Backspace")  vk = VK_BACK;
        else if (k == "Escape")     vk = VK_ESCAPE;
        else if (k == "Delete")     vk = VK_DELETE;
        else if (k == "ArrowLeft")  vk = VK_LEFT;
        else if (k == "ArrowRight") vk = VK_RIGHT;
        else if (k == "ArrowUp")    vk = VK_UP;
        else if (k == "ArrowDown")  vk = VK_DOWN;
        else return;

        keyEvent (vk, true);
        keyEvent (vk, false);
    }

    static void sendText (const juce::String& s)
    {
        for (auto* p = s.toWideCharPointer(); *p != 0; ++p)
            for (bool down : { true, false })
            {
                INPUT in {};
                in.type = INPUT_KEYBOARD;
                in.ki.wScan = (WORD) *p;
                in.ki.dwFlags = KEYEVENTF_UNICODE | (down ? 0 : KEYEVENTF_KEYUP);
                send (in);
            }
    }

    // Never leave a button or a modifier held down
    void releaseEverything()
    {
        if (buttonDown)
            mouseButton (false);
        buttonDown = false;
        setModifiers (0);
    }

    //==========================================================================
    juce::Component::SafePointer<juce::DocumentWindow> window;
    Options options;
    std::atomic<float> quality { options.quality };
    bool wasOnTop = false, buttonDown = false, rightButton = false;
    bool held[3] { false, false, false };
    int failures = 0;

    juce::CriticalSection lock;
    juce::Image latest;
    juce::uint64 latestNum = 0, takenNum = 0, lastHash = 0;
};
} // namespace

std::shared_ptr<PluginViewCapture> createWindowsViewCapture (juce::DocumentWindow& window, PluginViewCapture::Options options,
                                                             juce::String&)
{
    return std::make_shared<WindowsViewCapture> (window, options);
}
#endif
