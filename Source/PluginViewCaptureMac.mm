// Mac capture of a plugin window for the web remote.
// Built without ARC (see CMakeLists.txt): retains and releases are manual.

#include "PluginViewCapture.h"

#if JUCE_MAC
#import <AppKit/AppKit.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ImageIO/ImageIO.h>
#include <vector>
#include <algorithm>

namespace
{
const char* permissionText =
    "LiveRack needs Screen Recording permission to show plugin windows. On the Mac: System Settings > "
    "Privacy & Security > Screen & System Audio Recording > turn on LiveRack, then quit and reopen LiveRack. "
    "(Each new LiveRack version needs this again: turn it off and on.)";

// Latest frame from ScreenCaptureKit, shared with its callback queue
struct FrameStore
{
    juce::CriticalSection lock;
    CVPixelBufferRef latest = nullptr;
    juce::uint64 number = 0;
    juce::String error;
    bool failed = false;

    ~FrameStore()
    {
        if (latest != nullptr)
            CVPixelBufferRelease (latest);
    }

    void put (CVPixelBufferRef buffer)
    {
        CVPixelBufferRetain (buffer);
        CVPixelBufferRef old = nullptr;
        {
            const juce::ScopedLock sl (lock);
            old = latest;
            latest = buffer;
            ++number;
        }
        if (old != nullptr)
            CVPixelBufferRelease (old);
    }

    void fail (const juce::String& why)
    {
        const juce::ScopedLock sl (lock);
        failed = true;
        error = why;
    }
};

juce::String toJuce (NSString* s) { return s != nil ? juce::String::fromUTF8 (s.UTF8String) : juce::String(); }
} // namespace

@interface LiveRackCaptureSink : NSObject <SCStreamOutput, SCStreamDelegate>
{
@public
    std::shared_ptr<FrameStore> store;
}
@end

@implementation LiveRackCaptureSink
- (void) stream: (SCStream*) stream didOutputSampleBuffer: (CMSampleBufferRef) sampleBuffer ofType: (SCStreamOutputType) type
{
    juce::ignoreUnused (stream);
    if (type != SCStreamOutputTypeScreen || store == nullptr || ! CMSampleBufferIsValid (sampleBuffer))
        return;

    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray (sampleBuffer, false);
    if (attachments == nullptr || CFArrayGetCount (attachments) == 0)
        return;

    NSDictionary* info = (NSDictionary*) CFArrayGetValueAtIndex (attachments, 0);
    NSNumber* status = info[SCStreamFrameInfoStatus];
    if (status == nil || status.integerValue != SCFrameStatusComplete)
        return; // idle (nothing changed), blank or suspended

    if (CVImageBufferRef buffer = CMSampleBufferGetImageBuffer (sampleBuffer))
        store->put (buffer);
}

- (void) stream: (SCStream*) stream didStopWithError: (NSError*) error
{
    juce::ignoreUnused (stream);
    if (store != nullptr)
        store->fail ("Capture stopped: " + toJuce (error.localizedDescription));
}
@end

namespace
{
class MacViewCapture : public PluginViewCapture,
                       private juce::Timer
{
public:
    MacViewCapture (juce::DocumentWindow& w, Options o)
        : juceWindow (&w), options (o), quality (o.quality)
    {
        store = std::make_shared<FrameStore>();

        if (auto* view = (NSView*) w.getWindowHandle())
            window = [view window];

        sink = [[LiveRackCaptureSink alloc] init];
        sink->store = store;
        queue = dispatch_queue_create ("LiveRack plugin view", DISPATCH_QUEUE_SERIAL);
    }

    ~MacViewCapture() override
    {
        stop();
        [sink release];
        dispatch_release (queue);
    }

    // Message thread
    void begin()
    {
        if (window == nil)
        {
            postError ("The plugin window isn't open.");
            return;
        }

        if (! CGPreflightScreenCaptureAccess())
        {
            CGRequestScreenCaptureAccess(); // shows the system prompt the first time
            postError (permissionText);
            return;
        }

        // Keep the window visible and drawing while it's on the iPad
        savedLevel = window.level;
        if (window.level < NSFloatingWindowLevel)
            [window setLevel: NSFloatingWindowLevel];
        if (window.isMiniaturized)
            [window deminiaturize: nil];
        [window orderFront: nil];

        activity = [[[NSProcessInfo processInfo] beginActivityWithOptions: NSActivityUserInitiated | NSActivityLatencyCritical
                                                                   reason: @"Showing a plugin window on the web remote"] retain];

        startMenuWatch();
        refresh();
        startTimerHz (5);
    }

    void stop() override
    {
        stopTimer();
        stopMenuWatch();

        if (stream != nil)
        {
            [stream stopCaptureWithCompletionHandler: ^(NSError*) {}];
            [stream release];
            stream = nil;
        }

        if (window != nil && savedLevel >= 0 && window.level == NSFloatingWindowLevel)
            [window setLevel: savedLevel];
        savedLevel = -1;
        window = nil;
        [target release];
        target = nil;

        if (activity != nil)
        {
            [[NSProcessInfo processInfo] endActivity: activity];
            [activity release];
            activity = nil;
        }
    }

    //==========================================================================
    bool takeFrame (juce::MemoryBlock& jpeg) override
    {
        CVPixelBufferRef buffer = nullptr;
        {
            const juce::ScopedLock sl (store->lock);

            if (store->failed && ! ended.load())
            {
                const auto why = store->error;
                store->failed = false;
                juce::String text = why;
                if (why.containsIgnoreCase ("declined") || why.containsIgnoreCase ("permission") || why.containsIgnoreCase ("not authorized"))
                    text = permissionText;
                postError (text);
                return false;
            }

            if (store->latest == nullptr || store->number == taken)
                return false;

            taken = store->number;
            buffer = store->latest;
            CVPixelBufferRetain (buffer);
        }

        bool ok = false;
        @autoreleasepool { ok = encode (buffer, jpeg); }
        CVPixelBufferRelease (buffer);
        return ok;
    }

    //==========================================================================
    void input (const juce::var& e) override
    {
        @autoreleasepool { handleInput (e); }
    }

    void handleInput (const juce::var& e)
    {
        if (window == nil)
            return;

        const auto t = e["t"].toString();
        lastInputMs = juce::Time::getMillisecondCounter();

        if (t == "opts")
        {
            options.scale = juce::jlimit (0.5f, 3.0f, (float) e.getProperty ("scale", options.scale));
            quality = juce::jlimit (0.2f, 0.95f, (float) e.getProperty ("q", quality.load()));
            geometryKey = {}; // reconfigure on the next refresh
            refresh();
            return;
        }

        if (t == "menu" || t == "menuCancel")
        {
            chooseMenuItem (t == "menu" ? e["path"] : juce::var());
            return;
        }

        const int f = modifierFlagsFrom (e["mods"].toString());
        NSEventModifierFlags flags = 0;
        if (f & 1) flags |= NSEventModifierFlagShift;
        if (f & 2) flags |= NSEventModifierFlagOption;
        if (f & 4) flags |= NSEventModifierFlagControl;
        if (f & 8) flags |= NSEventModifierFlagCommand;

        if (t == "key" || t == "text")
        {
            sendKeys (e, flags);
            return;
        }

        // Where on the screen the touch is
        const NSPoint global = NSMakePoint (captured.origin.x + (CGFloat) (double) e["x"] * captured.size.width,
                                            captured.origin.y + (1.0 - (CGFloat) (double) e["y"]) * captured.size.height);

        if (t == "wheel")
        {
            sendWheel (global, (double) e["dx"], (double) e["dy"], flags);
            return;
        }

        if (t == "down")
        {
            rightButton = e["b"].toString() == "r";
            clicks = juce::jlimit (1, 3, (int) e.getProperty ("n", 1));

            [target release];
            target = [windowAt (global) retain]; // drags stay with the window they started in

            if (target.canBecomeKeyWindow && ! target.isKeyWindow)
                [target makeKeyWindow];

            post (NSEventTypeMouseMoved, global, flags, 0, 0.0f);
            post (rightButton ? NSEventTypeRightMouseDown : NSEventTypeLeftMouseDown, global, flags, clicks, 1.0f);
            buttonDown = true;
        }
        else if (t == "move")
        {
            if (buttonDown)
                post (rightButton ? NSEventTypeRightMouseDragged : NSEventTypeLeftMouseDragged, global, flags, clicks, 1.0f);
        }
        else if (t == "up")
        {
            if (buttonDown)
                post (rightButton ? NSEventTypeRightMouseUp : NSEventTypeLeftMouseUp, global, flags, clicks, 0.0f);
            buttonDown = false;
        }
    }

private:
    //==========================================================================
    // Capture setup

    void timerCallback() override { @autoreleasepool { refresh(); } }

    // The plugin window plus our popups over it (JUCE menus, tooltips, child windows)
    std::vector<CGWindowID> windowsToShow() const
    {
        std::vector<CGWindowID> ids { (CGWindowID) window.windowNumber };
        const NSRect frame = window.frame;

        for (NSWindow* w in [NSApp orderedWindows])
        {
            if (w == window || ! w.isVisible || w.windowNumber <= 0)
                continue;

            const bool popup = w.level > window.level || w.parentWindow == window;
            if (popup && NSIntersectsRect (w.frame, frame))
                ids.push_back ((CGWindowID) w.windowNumber);
        }

        std::sort (ids.begin(), ids.end());
        return ids;
    }

    void refresh()
    {
        if (window == nil || ended.load())
            return;

        if (window.screen == nil)
        {
            postError ("Move the plugin window onto a screen on the Mac.");
            return;
        }

        // Window moved or resized: point the capture at its new place
        NSRect area;
        CGDirectDisplayID display = 0;
        const CGRect source = sourceRect (area, display);

        const auto key = juce::String ((int) display) + ":" + juce::String (source.origin.x) + "," + juce::String (source.origin.y)
                       + "," + juce::String (source.size.width) + "," + juce::String (source.size.height)
                       + "@" + juce::String (options.scale);

        if (display != shownDisplay)
        {
            shownIds.clear(); // new screen: new filter
            shownDisplay = display;
        }

        if (key != geometryKey && stream != nil)
        {
            geometryKey = key;
            captured = area;
            SCStreamConfiguration* config = makeConfig (source);
            [stream updateConfiguration: config completionHandler: ^(NSError*) {}];
            [config release];
        }

        const auto ids = windowsToShow();
        if (ids == shownIds || fetching)
            return;

        // Which windows to show changed (or first time): ask the system for them
        fetching = true;
        std::weak_ptr<PluginViewCapture> weak = weak_from_this();

        [SCShareableContent getShareableContentExcludingDesktopWindows: NO
                                                   onScreenWindowsOnly: YES
                                                     completionHandler: ^(SCShareableContent* content, NSError* error)
        {
            [content retain];
            [error retain];
            juce::MessageManager::callAsync ([weak, content, error, ids, display]
            {
                @autoreleasepool
                {
                    if (auto self = weak.lock())
                        static_cast<MacViewCapture*> (self.get())->apply (content, error, ids, display);
                }
                [content release];
                [error release];
            });
        }];
    }

    void apply (SCShareableContent* content, NSError* error, const std::vector<CGWindowID>& ids, CGDirectDisplayID displayId)
    {
        fetching = false;
        if (window == nil || ended.load())
            return;

        if (error != nil || content == nil)
        {
            postError (error != nil && error.code == SCStreamErrorUserDeclined ? juce::String (permissionText)
                                                                              : "Can't capture the screen: " + toJuce (error.localizedDescription));
            return;
        }

        SCDisplay* display = nil;
        for (SCDisplay* d in content.displays)
            if (d.displayID == displayId)
                display = d;

        NSMutableArray<SCWindow*>* windows = [NSMutableArray array];
        for (SCWindow* w in content.windows)
            if (std::find (ids.begin(), ids.end(), w.windowID) != ids.end())
                [windows addObject: w];

        if (display == nil || windows.count == 0)
            return; // not on screen yet: the next refresh tries again

        shownIds = ids;
        SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay: display includingWindows: windows];

        if (stream == nil)
        {
            NSRect area;
            CGDirectDisplayID unused = 0;
            const CGRect source = sourceRect (area, unused);
            captured = area;
            SCStreamConfiguration* config = makeConfig (source);

            stream = [[SCStream alloc] initWithFilter: filter configuration: config delegate: sink];
            [config release];

            NSError* addError = nil;
            if (! [stream addStreamOutput: sink type: SCStreamOutputTypeScreen sampleHandlerQueue: queue error: &addError])
                store->fail ("Can't capture: " + toJuce (addError.localizedDescription));

            auto storeCopy = store;
            [stream startCaptureWithCompletionHandler: ^(NSError* startError)
            {
                if (startError != nil)
                    storeCopy->fail (toJuce (startError.localizedDescription));
            }];
        }
        else
        {
            [stream updateContentFilter: filter completionHandler: ^(NSError*) {}];
        }

        [filter release];
    }

    // The window's content area in its display's points (top-left origin),
    // plus the same area in global screen coordinates for mapping touches.
    CGRect sourceRect (NSRect& globalArea, CGDirectDisplayID& display) const
    {
        NSScreen* screen = window.screen;
        display = (CGDirectDisplayID) [screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];

        const NSRect s = screen.frame;
        NSRect c = NSIntersectionRect ([window contentRectForFrameRect: window.frame], s);
        if (c.size.width < 1 || c.size.height < 1)
            c = NSMakeRect (s.origin.x, s.origin.y, 16, 16);

        globalArea = c;
        return CGRectMake (c.origin.x - s.origin.x, NSMaxY (s) - NSMaxY (c), c.size.width, c.size.height);
    }

    SCStreamConfiguration* makeConfig (CGRect source) const
    {
        SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
        config.sourceRect = source;
        config.width  = (size_t) juce::jmax (16, juce::roundToInt (source.size.width  * options.scale));
        config.height = (size_t) juce::jmax (16, juce::roundToInt (source.size.height * options.scale));
        config.minimumFrameInterval = CMTimeMake (1, 30);
        config.pixelFormat = kCVPixelFormatType_32BGRA;
        config.showsCursor = NO;
        config.queueDepth = 5;
        config.backgroundColor = CGColorGetConstantColor (kCGColorBlack);
        return config;
    }

    bool encode (CVPixelBufferRef buffer, juce::MemoryBlock& jpeg) const
    {
        if (CVPixelBufferLockBaseAddress (buffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess)
            return false;

        CGColorSpaceRef space = CGColorSpaceCreateWithName (kCGColorSpaceSRGB);
        CGContextRef bitmap = CGBitmapContextCreate (CVPixelBufferGetBaseAddress (buffer),
                                                     CVPixelBufferGetWidth (buffer), CVPixelBufferGetHeight (buffer), 8,
                                                     CVPixelBufferGetBytesPerRow (buffer), space,
                                                     (CGBitmapInfo) kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
        CGImageRef image = bitmap != nullptr ? CGBitmapContextCreateImage (bitmap) : nullptr;
        CVPixelBufferUnlockBaseAddress (buffer, kCVPixelBufferLock_ReadOnly);

        bool ok = false;
        if (image != nullptr)
        {
            CFMutableDataRef data = CFDataCreateMutable (nullptr, 0);
            if (CGImageDestinationRef dest = CGImageDestinationCreateWithData (data, CFSTR ("public.jpeg"), 1, nullptr))
            {
                NSDictionary* props = @{ (NSString*) kCGImageDestinationLossyCompressionQuality: @(quality.load()) };
                CGImageDestinationAddImage (dest, image, (CFDictionaryRef) props);
                ok = CGImageDestinationFinalize (dest);
                CFRelease (dest);
            }
            if (ok)
                jpeg.replaceAll (CFDataGetBytePtr (data), (size_t) CFDataGetLength (data));
            CFRelease (data);
            CGImageRelease (image);
        }

        if (bitmap != nullptr) CGContextRelease (bitmap);
        CGColorSpaceRelease (space);
        return ok;
    }

    //==========================================================================
    // Events

    // The front-most of the shown windows at a point (a popup menu over the plugin, or the plugin)
    NSWindow* windowAt (NSPoint global) const
    {
        for (NSWindow* w in [NSApp orderedWindows])
            if (std::find (shownIds.begin(), shownIds.end(), (CGWindowID) w.windowNumber) != shownIds.end()
                 && w.isVisible && NSPointInRect (global, w.frame))
                return w;
        return window;
    }

    void post (NSEventType type, NSPoint global, NSEventModifierFlags flags, int clickCount, float pressure)
    {
        NSWindow* w = target != nil ? target : window;
        const NSPoint local = [w convertPointFromScreen: global];

        NSEvent* event = [NSEvent mouseEventWithType: type
                                            location: local
                                       modifierFlags: flags
                                           timestamp: [NSProcessInfo processInfo].systemUptime
                                        windowNumber: w.windowNumber
                                             context: nil
                                         eventNumber: ++eventNumber
                                          clickCount: clickCount
                                            pressure: pressure];
        if (event != nil)
            [NSApp postEvent: event atStart: NO];
    }

    void sendWheel (NSPoint global, double dx, double dy, NSEventModifierFlags flags)
    {
        NSWindow* w = windowAt (global);
        const NSPoint local = [w convertPointFromScreen: global];

        CGEventRef cg = CGEventCreateScrollWheelEvent (nullptr, kCGScrollEventUnitPixel, 2, (int32_t) dy, (int32_t) dx);
        if (cg == nullptr)
            return;

        // The event has no window, so AppKit reads its location as window
        // coordinates (bottom-left): give it the point inside the window.
        const CGFloat primaryHeight = NSMaxY ([NSScreen screens].firstObject.frame);
        CGEventSetLocation (cg, CGPointMake (local.x, primaryHeight - local.y));
        CGEventSetFlags (cg, (CGEventFlags) flags);

        if (NSEvent* event = [NSEvent eventWithCGEvent: cg])
            [w sendEvent: event];
        CFRelease (cg);
    }

    void sendKeys (const juce::var& e, NSEventModifierFlags flags)
    {
        NSWindow* keyWindow = NSApp.keyWindow;
        NSWindow* w = (keyWindow != nil && std::find (shownIds.begin(), shownIds.end(), (CGWindowID) keyWindow.windowNumber) != shownIds.end())
                        ? keyWindow : window;

        auto sendOne = [&] (NSString* chars, unsigned short code)
        {
            for (auto type : { NSEventTypeKeyDown, NSEventTypeKeyUp })
            {
                NSEvent* event = [NSEvent keyEventWithType: type location: NSZeroPoint modifierFlags: flags
                                                 timestamp: [NSProcessInfo processInfo].systemUptime
                                              windowNumber: w.windowNumber context: nil
                                                characters: chars charactersIgnoringModifiers: chars
                                                 isARepeat: NO keyCode: code];
                if (event != nil)
                    [NSApp postEvent: event atStart: NO];
            }
        };

        if (e["t"].toString() == "key")
        {
            const auto k = e["k"].toString();
            unichar c = 0;
            unsigned short code = 0;
            if      (k == "Enter")      { c = '\r'; code = 36; }
            else if (k == "Tab")        { c = '\t'; code = 48; }
            else if (k == "Backspace")  { c = 0x7f; code = 51; }
            else if (k == "Escape")     { c = 0x1b; code = 53; }
            else if (k == "ArrowLeft")  { c = NSLeftArrowFunctionKey;  code = 123; }
            else if (k == "ArrowRight") { c = NSRightArrowFunctionKey; code = 124; }
            else if (k == "ArrowDown")  { c = NSDownArrowFunctionKey;  code = 125; }
            else if (k == "ArrowUp")    { c = NSUpArrowFunctionKey;    code = 126; }
            else return;
            sendOne ([NSString stringWithCharacters: &c length: 1], code);
            return;
        }

        for (auto ch : e["s"].toString())
        {
            const unichar c = (unichar) ch;
            sendOne ([NSString stringWithCharacters: &c length: 1], keyCodeFor (ch));
        }
    }

    // US keyboard key codes for the characters people type into plugin fields
    static unsigned short keyCodeFor (juce::juce_wchar c)
    {
        static const char row[] = "asdfhgzxcv\0bqweryt123465=97-80]ou[ip\0lj'k;\\,/nm.";
        c = juce::CharacterFunctions::toLowerCase (c);
        for (unsigned short i = 0; i < (unsigned short) (sizeof (row) - 1); ++i)
            if (row[i] != 0 && (juce::juce_wchar) row[i] == c)
                return i;
        if (c == ' ') return 49;
        return 0;
    }

    //==========================================================================
    // Native popup menus (a plugin's preset or option menus) can't take
    // clicks from the remote, so the remote shows them as a touch list.

    void startMenuWatch()
    {
        std::weak_ptr<PluginViewCapture> weak = weak_from_this();
        NSNotificationCenter* centre = [NSNotificationCenter defaultCenter];

        beginObserver = [[centre addObserverForName: NSMenuDidBeginTrackingNotification object: nil queue: nil
                                         usingBlock: ^(NSNotification* n)
        {
            if (auto self = weak.lock())
                static_cast<MacViewCapture*> (self.get())->menuOpened ((NSMenu*) n.object);
        }] retain];

        endObserver = [[centre addObserverForName: NSMenuDidEndTrackingNotification object: nil queue: nil
                                       usingBlock: ^(NSNotification* n)
        {
            if (auto self = weak.lock())
                static_cast<MacViewCapture*> (self.get())->menuClosed ((NSMenu*) n.object);
        }] retain];
    }

    void stopMenuWatch()
    {
        NSNotificationCenter* centre = [NSNotificationCenter defaultCenter];
        for (id* o : { &beginObserver, &endObserver })
            if (*o != nil)
            {
                [centre removeObserver: *o];
                [*o release];
                *o = nil;
            }
        [menu release];
        menu = nil;
    }

    void menuOpened (NSMenu* m)
    {
        if (m == nil || menu != nil || m.supermenu != nil)
            return;

        for (NSMenu* root = m; root != nil; root = root.supermenu)
            if (root == NSApp.mainMenu)
                return; // the menu bar, not a plugin

        // Only menus the remote opened (a tap in the last few seconds)
        if (juce::Time::getMillisecondCounter() - lastInputMs > 3000)
            return;

        menu = [m retain];

        auto* o = new juce::DynamicObject();
        o->setProperty ("t", "menu");
        o->setProperty ("items", itemsOf (m, 0));
        postMessage (juce::var (o));
    }

    void menuClosed (NSMenu* m)
    {
        if (m == nil || m != menu)
            return;
        [menu release];
        menu = nil;

        auto* o = new juce::DynamicObject();
        o->setProperty ("t", "menuEnd");
        postMessage (juce::var (o));
    }

    static juce::var itemsOf (NSMenu* m, int depth)
    {
        juce::Array<juce::var> items;
        for (NSMenuItem* item in m.itemArray)
        {
            if (items.size() >= 400)
                break;
            auto* o = new juce::DynamicObject();
            if (item.isSeparatorItem)
                o->setProperty ("sep", true);
            else
            {
                o->setProperty ("title", toJuce (item.title));
                o->setProperty ("enabled", (bool) item.isEnabled);
                o->setProperty ("checked", item.state == NSControlStateValueOn);
                if (item.hasSubmenu && depth < 5)
                    o->setProperty ("sub", itemsOf (item.submenu, depth + 1));
            }
            if (item.isHidden)
                o->setProperty ("hidden", true);
            items.add (juce::var (o));
        }
        return items;
    }

    void chooseMenuItem (const juce::var& path)
    {
        if (menu == nil)
            return;

        NSMenu* root = [[menu retain] autorelease];
        NSMenu* current = root;
        NSInteger index = -1;

        if (auto* steps = path.getArray())
        {
            for (int i = 0; i < steps->size(); ++i)
            {
                const NSInteger step = (int) (*steps)[i];
                if (current == nil || step < 0 || step >= current.numberOfItems)
                {
                    current = nil;
                    break;
                }
                if (i < steps->size() - 1)
                    current = [current itemAtIndex: step].submenu;
                else
                    index = step;
            }
        }

        // Pick the item, then close the menu
        if (current != nil && index >= 0 && [current itemAtIndex: index].isEnabled)
            [current performActionForItemAtIndex: index];

        [root cancelTrackingWithoutAnimation];
    }

    //==========================================================================
    juce::Component::SafePointer<juce::DocumentWindow> juceWindow;
    NSWindow* window = nil;           // owned by JUCE
    NSWindow* target = nil;           // retained: the window a drag started in
    Options options;
    std::atomic<float> quality;

    std::shared_ptr<FrameStore> store;
    LiveRackCaptureSink* sink = nil;
    dispatch_queue_t queue = nullptr;
    SCStream* stream = nil;
    juce::uint64 taken = 0;

    std::vector<CGWindowID> shownIds;
    CGDirectDisplayID shownDisplay = 0;
    juce::String geometryKey;
    NSRect captured = NSZeroRect;
    bool fetching = false;
    NSInteger savedLevel = -1;
    id activity = nil;

    bool buttonDown = false, rightButton = false;
    int clicks = 1;
    NSInteger eventNumber = 0;
    juce::uint32 lastInputMs = 0;

    id beginObserver = nil, endObserver = nil;
    NSMenu* menu = nil;
};
} // namespace

bool PluginViewCapture::isSupported() { return true; }

std::shared_ptr<PluginViewCapture> PluginViewCapture::create (juce::DocumentWindow& window, Options options, juce::String& error)
{
    juce::ignoreUnused (error);
    auto capture = std::make_shared<MacViewCapture> (window, options);
    capture->begin();
    return capture;
}
#endif
