#include "ServerCheck.h"
#include "RackEngine.h"
#include "WebRemote.h"
#include "ServerMode.h"
#include "SocketUtil.h"
#include "Theme.h"
#include <thread>

namespace ServerCheck
{
namespace
{
    juce::String run (const juce::StringArray& args)
    {
        juce::ChildProcess p;
        if (! p.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
            return {};
        auto out = p.readAllProcessOutput();
        p.waitForProcessToFinish (4000);
        return out;
    }

    void runAndRefresh (const juce::StringArray& args)
    {
        std::thread ([args] { run (args); juce::MessageManager::callAsync ([] { refreshSoon(); }); }).detach();
    }

    void open (const juce::String& what) { juce::Process::openDocument (what, {}); }

    // The first number after a label, in "label   123" output
    int numberAfter (const juce::String& text, const juce::String& label, int fallback)
    {
        for (auto& line : juce::StringArray::fromLines (text))
        {
            const auto t = line.trim();
            if (t.startsWithIgnoreCase (label + " ") || t.startsWithIgnoreCase (label + "\t"))
                return t.substring (label.length()).trim().getIntValue();
        }
        return fallback;
    }

    std::vector<Item> checkSystem()
    {
        std::vector<Item> items;

       #if JUCE_WINDOWS
        // Power plan
        {
            const auto out = run ({ "powercfg", "/getactivescheme" });
            const auto name = out.fromLastOccurrenceOf ("(", false, false).upToFirstOccurrenceOf (")", false, false).trim();
            const auto lower = out.toLowerCase();
            const bool fast = lower.contains ("8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c") || lower.contains ("e9a42b02-d5df-448d-aa00-03f14749eb61")
                           || name.containsIgnoreCase ("performance");
            Item i;
            i.state = fast ? Item::good : Item::lookAt;
            i.title = "Power plan: " + (name.isNotEmpty() ? name : juce::String ("unknown"));
            i.detail = fast ? "The processor runs at full speed." : "Balanced and power saving plans slow the processor down between bursts, which causes dropouts at small buffers.";
            if (! fast)
            {
                i.fixLabel = "Use High performance";
                i.fix = [] { runAndRefresh ({ "powercfg", "/setactive", "SCHEME_MIN" }); };
            }
            items.push_back (i);
        }

        // Sleep
        {
            const auto out = run ({ "powercfg", "/query", "SCHEME_CURRENT", "SUB_SLEEP", "STANDBYIDLE" });
            int seconds = -1;
            for (auto& line : juce::StringArray::fromLines (out))
                if (line.containsIgnoreCase ("Current AC Power Setting Index"))
                    seconds = (int) line.fromLastOccurrenceOf ("0x", false, true).getHexValue32();
            Item i;
            i.state = seconds == 0 ? Item::good : Item::lookAt;
            i.title = seconds == 0 ? "Sleep: never" : seconds > 0 ? "Sleep: after " + juce::String (seconds / 60) + " minutes" : "Sleep: unknown";
            i.detail = seconds == 0 ? "The computer stays awake." : "A computer that sleeps stops the audio. Set it to never sleep.";
            if (seconds != 0)
            {
                i.fixLabel = "Never sleep";
                i.fix = [] { runAndRefresh ({ "powercfg", "/change", "standby-timeout-ac", "0" }); };
            }
            items.push_back (i);
        }

        // Screen lock
        {
            const bool saver = juce::WindowsRegistry::getValue ("HKEY_CURRENT_USER\\Control Panel\\Desktop\\ScreenSaveActive") == "1";
            const bool secure = juce::WindowsRegistry::getValue ("HKEY_CURRENT_USER\\Control Panel\\Desktop\\ScreenSaverIsSecure") == "1";
            Item i;
            i.state = saver && secure ? Item::lookAt : Item::good;
            i.title = saver && secure ? "Screen locks when idle" : "Screen doesn't lock when idle";
            i.detail = saver && secure ? "Plugin windows can't be shown on the remote while the screen is locked."
                                       : "Plugin windows stay available to the remote.";
            if (saver && secure)
            {
                i.fixLabel = "Screen saver settings";
                i.fix = [] { open ("control.exe desk.cpl,,@screensaver"); };
            }
            items.push_back (i);
        }

        // Windows Update
        {
            const auto until = juce::WindowsRegistry::getValue ("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\WindowsUpdate\\UX\\Settings\\PauseUpdatesExpiryTime");
            const auto when = juce::Time::fromISO8601 (until);
            const bool paused = until.isNotEmpty() && when > juce::Time::getCurrentTime();
            Item i;
            i.state = paused ? Item::good : Item::lookAt;
            i.title = paused ? "Windows Update paused until " + when.toString (true, false) : "Windows Update isn't paused";
            i.detail = paused ? "Updates won't restart the computer before then."
                              : "Updates can download in the background and restart the computer. Pause them before a service, or set active hours.";
            i.fixLabel = "Windows Update settings";
            i.fix = [] { open ("ms-settings:windowsupdate"); };
            items.push_back (i);
        }

        // Logging in by itself
        {
            const bool autoLogin = juce::WindowsRegistry::getValue ("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon\\AutoAdminLogon") == "1";
            Item i;
            i.state = autoLogin ? Item::good : Item::lookAt;
            i.title = autoLogin ? "Logs in automatically" : "Doesn't log in automatically";
            i.detail = autoLogin ? "After a power cut or restart, Windows logs in and LiveRack starts."
                                 : "After a restart, Windows waits at the login screen and LiveRack doesn't start.";
            if (! autoLogin)
            {
                i.fixLabel = "Open netplwiz";
                i.fix = [] { juce::File::getSpecialLocation (juce::File::windowsSystemDirectory).getChildFile ("netplwiz.exe").startAsProcess(); };
            }
            items.push_back (i);
        }

        // Antivirus
        {
            const bool avast = juce::File ("C:\\Program Files\\Avast Software\\Avast").isDirectory()
                            || juce::File ("C:\\Program Files\\AVG\\Antivirus").isDirectory();
            if (avast)
            {
                Item i;
                i.state = Item::note;
                i.title = "Avast / AVG is installed";
                i.detail = "Add an exception for LiveRack's folder (" + juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getFullPathName()
                         + ") and your plugin folders, so it doesn't scan or hold them during a service.";
                items.push_back (i);
            }
        }
       #elif JUCE_MAC
        {
            const auto out = run ({ "pmset", "-g" });
            const int sleepMinutes = numberAfter (out, "sleep", -1);
            Item i;
            i.state = sleepMinutes == 0 ? Item::good : Item::lookAt;
            i.title = sleepMinutes == 0 ? "Sleep: never" : sleepMinutes > 0 ? "Sleep: after " + juce::String (sleepMinutes) + " minutes" : "Sleep: unknown";
            i.detail = sleepMinutes == 0 ? "The Mac stays awake." : "A Mac that sleeps stops the audio. Set it to never sleep (System Settings > Energy, or Battery > Options on a laptop).";
            if (sleepMinutes != 0)
            {
                i.fixLabel = "Energy settings";
                i.fix = [] { open ("x-apple.systempreferences:com.apple.Energy-Saver-Settings.extension"); };
            }
            items.push_back (i);
        }
        {
            const auto user = run ({ "defaults", "read", "/Library/Preferences/com.apple.loginwindow", "autoLoginUser" }).trim();
            const bool autoLogin = user.isNotEmpty() && ! user.containsIgnoreCase ("does not exist");
            Item i;
            i.state = autoLogin ? Item::good : Item::lookAt;
            i.title = autoLogin ? "Logs in automatically" : "Doesn't log in automatically";
            i.detail = autoLogin ? "After a power cut or restart, the Mac logs in and LiveRack starts."
                                 : "After a restart, the Mac waits at the login window. Turn on automatic login in System Settings > Users & Groups.";
            if (! autoLogin)
            {
                i.fixLabel = "Users & Groups";
                i.fix = [] { open ("x-apple.systempreferences:com.apple.Users-Groups-Settings.extension"); };
            }
            items.push_back (i);
        }
       #else
        juce::ignoreUnused (numberAfter);
       #endif

        return items;
    }

    // Results, kept on the message thread
    struct Cache : private juce::Timer
    {
        Cache() { startTimer (60000); refresh(); }
        void timerCallback() override { refresh(); }

        void refresh()
        {
            if (busy.exchange (true))
                return;
            std::thread ([]
            {
                auto items = checkSystem();
                juce::MessageManager::callAsync ([items]
                {
                    auto& c = cache();
                    c.items = items;
                    c.busy = false;
                    if (onUpdated() != nullptr)
                        onUpdated()();
                });
            }).detach();
        }

        std::vector<Item> items;
        std::atomic<bool> busy { false };

        static Cache& cache()
        {
            static Cache c;
            return c;
        }
    };
}

std::function<void()>& onUpdated()
{
    static std::function<void()> f;
    return f;
}

std::vector<Item> getSystemItems() { return Cache::cache().items; }
void refreshSoon()                 { Cache::cache().refresh(); }

std::vector<Item> getAppItems (RackEngine& engine, WebRemote& web)
{
    std::vector<Item> items;

    {
        Item i;
        const bool on = ServerMode::startsAtLogin();
        i.state = on ? Item::good : Item::lookAt;
        i.title = on ? "LiveRack starts with the computer" : "LiveRack doesn't start with the computer";
        i.detail = on ? "After a restart it opens with the show it had." : "After a restart nothing processes audio until someone opens LiveRack.";
        if (! on && ServerMode::startAtLoginProblem().isEmpty())
        {
            i.fixLabel = "Turn on";
            i.fix = [] { ServerMode::setStartAtLogin (true); };
        }
        items.push_back (i);
    }
    {
        Item i;
        const bool on = ServerMode::restartsAfterCrash();
        i.state = on ? Item::good : Item::lookAt;
        i.title = on ? "Restarts after a crash" : "Doesn't restart after a crash";
        i.detail = on ? "A watcher starts LiveRack again, with its show, if it quits unexpectedly." : "If LiveRack crashes it stays closed.";
        if (! on)
        {
            i.fixLabel = "Turn on";
            i.fix = [] { ServerMode::setRestartAfterCrash (true); };
        }
        items.push_back (i);
    }
    if (RackEngine::getNumRackThreads() > 1)
    {
        Item i;
        const bool on = engine.isMultiCore();
        i.state = on ? Item::good : Item::lookAt;
        i.title = on ? "Racks run on " + juce::String (RackEngine::getNumRackThreads()) + " CPU cores" : "Racks run on one CPU core";
        i.detail = on ? "Heavy racks are shared out between the cores." : "Turning it on gives more headroom before dropouts.";
        if (! on)
        {
            i.fixLabel = "Turn on";
            auto* e = &engine;
            i.fix = [e] { e->setMultiCore (true); };
        }
        items.push_back (i);
    }
    for (int d = 0; d < engine.getNumDrivers(); ++d)
    {
        const auto st = engine.getDriverStatus (d);
        Item i;
        const bool recentDrops = st.dropouts > 0 && juce::Time::getCurrentTime() - st.lastDropout < juce::RelativeTime::hours (1);
        i.state = ! st.open || recentDrops || st.worstBlock > 0.8 ? Item::lookAt : Item::good;
        i.title = "Audio driver " + juce::String (d + 1) + ": " + (st.open ? st.deviceName : engine.getDriverWaitingFor (d).isNotEmpty()
                                                                                         ? "waiting for " + engine.getDriverWaitingFor (d) : juce::String ("no device"));
        i.detail = ! st.open ? "Nothing is processed until the device is there."
                 : recentDrops ? juce::String (st.dropouts) + " dropouts, the last at " + st.lastDropout.toString (false, true, false)
                 : st.worstBlock > 0.8 ? "The longest block recently took " + juce::String ((int) std::ceil (st.worstBlock * 100.0)) + "% of its time: close to a dropout."
                 : "Running, longest block " + juce::String ((int) std::ceil (st.worstBlock * 100.0)) + "% of its time.";
        items.push_back (i);
    }
    {
        bool audioNetwork = false;
        for (auto& n : SocketUtil::getNetworks())
            audioNetwork = audioNetwork || n.linkLocal;
        if (web.isEnabled() && web.getNetworkId().isEmpty() && audioNetwork)
        {
            Item i;
            i.state = Item::note;
            i.title = "The web remote answers on every network";
            i.detail = "This computer is also on a self-assigned (audio) network. Pick your control network in the web remote's settings to keep remote traffic off it.";
            items.push_back (i);
        }
    }

    return items;
}

//==============================================================================
namespace
{
class Panel : public juce::Component, private juce::Timer
{
public:
    Panel (RackEngine& e, WebRemote& w) : engine (e), web (w)
    {
        list.owner = this;
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (recheck);
        recheck.onClick = [] { refreshSoon(); };
        onUpdated() = [safe = juce::Component::SafePointer<Panel> (this)] { if (safe != nullptr) safe->rebuild(); };
        setSize (640, 600);
        rebuild();
        startTimer (3000);
    }

    ~Panel() override { onUpdated() = nullptr; }

    void timerCallback() override { rebuild(); }

    void rebuild()
    {
        items = getSystemItems();
        for (auto& i : getAppItems (engine, web))
            items.push_back (i);

        buttons.clear();
        int y = 0;
        rows.clear();
        for (auto& i : items)
        {
            const int h = 30 + juce::jmax (1, (int) std::ceil (juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (12.5f), i.detail) / 420.0f)) * 17 + 14;
            rows.push_back ({ y, h });
            if (i.fixLabel.isNotEmpty() && i.fix != nullptr)
            {
                auto* b = buttons.add (new juce::TextButton (i.fixLabel));
                auto fix = i.fix;
                b->onClick = [fix, safe = juce::Component::SafePointer<Panel> (this)]
                {
                    fix();
                    if (safe != nullptr) juce::Timer::callAfterDelay (300, [safe] { if (safe != nullptr) safe->rebuild(); });
                };
                b->setBounds (getWidth() - 220, y + h / 2 - 15, 190, 30);
                list.addAndMakeVisible (b);
            }
            y += h;
        }
        list.setSize (getWidth() - 12, y);
        list.repaint();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::background);
        int look = 0;
        for (auto& i : items) if (i.state == Item::lookAt) ++look;
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (look == 0 ? "Everything looks ready for a service" : juce::String (look) + (look == 1 ? " thing" : " things") + " to look at",
                    16, 10, getWidth() - 160, 26, juce::Justification::centredLeft);
    }

    void resized() override
    {
        recheck.setBounds (getWidth() - 116, 10, 100, 28);
        viewport.setBounds (getLocalBounds().withTrimmedTop (46));
        rebuild();
    }

private:
    struct Rows : public juce::Component
    {
        Panel* owner = nullptr;
        void paint (juce::Graphics& g) override
        {
            for (size_t n = 0; n < owner->items.size() && n < owner->rows.size(); ++n)
            {
                auto& i = owner->items[n];
                auto r = juce::Rectangle<int> (0, owner->rows[n].first, getWidth(), owner->rows[n].second);
                g.setColour (Theme::border.withAlpha (0.5f));
                g.fillRect (r.withHeight (1));
                auto c = r.reduced (16, 8);
                auto dot = c.removeFromLeft (18).removeFromTop (22).withSizeKeepingCentre (10, 10).toFloat();
                g.setColour (i.state == Item::good ? Theme::green : i.state == Item::lookAt ? Theme::amber : Theme::accent);
                g.fillEllipse (dot);
                c.removeFromRight (230);
                g.setColour (Theme::text);
                g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
                g.drawText (i.title, c.removeFromTop (22), juce::Justification::centredLeft, true);
                g.setColour (Theme::textDim);
                g.setFont (juce::FontOptions (12.5f));
                g.drawFittedText (i.detail, c, juce::Justification::topLeft, 4, 1.0f);
            }
        }
    };

    RackEngine& engine;
    WebRemote& web;
    std::vector<Item> items;
    std::vector<std::pair<int, int>> rows;
    Rows list;
    juce::Viewport viewport;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::TextButton recheck { "Check again" };
};
}

void showWindow (RackEngine& engine, WebRemote& web)
{
    refreshSoon();
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new Panel (engine, web));
    o.dialogTitle = "Server check";
    o.dialogBackgroundColour = Theme::background;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}
}
