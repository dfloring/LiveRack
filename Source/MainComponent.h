#pragma once
#include "RackEngine.h"
#include "PluginWindow.h"
#include "PluginViewCapture.h"
#include "ConsoleLink.h"
#include "RackStripsView.h"
#include "PluginDock.h"
#include "PluginLibrary.h"
#include "Actions.h"
#include "ControlBars.h"
#include "OscRemote.h"
#include "MidiControl.h"
#include "Theme.h"
#include "Shortcuts.h"
#include "WebRemote.h"

class RoutingMatrix;
class ScopeEditor;
class DevicesPanel;

class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      public ActionHost,
                      private juce::ChangeListener,
                      private juce::Timer,
                      private juce::KeyListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // ActionHost: User Keys, MIDI, keyboard and OSC all come through here
    void performAction (const Action&, int value) override;
    bool isActionActive (const Action&) const override;
    int  getSelectedRack() const override { return selectedRack; }
    int  getSelectedSlot() const override { return selectedSlot; }

    void dragOperationStarted (const juce::DragAndDropTarget::SourceDetails&) override;
    void dragOperationEnded (const juce::DragAndDropTarget::SourceDetails&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    using juce::Component::keyPressed;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    void sendFeedback();

    // Selection
    void select (int rack, int slot); // slot -1 = keep / first plugin
    void refreshAll();
    void refreshPluginList();

    // Racks
    void addRack();
    void showRackDialog (int rackIndex); // edit name / console channel
    void finishRackDialog (int result, int rackIndex);
    void showRackMenu (int rack);
    void removeRack (int rack);
    void editConsoleChannel (int rack);
    void changeFormat (int rack, const RackConfig&);

    // Plugins
    void showAddPluginMenu (int rack);
    void closePicker();
    void showSlotMenu (int rack, int slot, juce::Rectangle<int> screenArea = {});
    void popOutEditor (int rack, int slot);
    void removeSlot (int rack, int slot);
    void showParameters (int rack, int slot);
    void handleSlotDrop (int fromRack, int fromSlot, int toRack, int toIndex, bool copy);
    void handlePluginDrop (const juce::String& pluginId, int toRack, int toIndex);
    void openSlotId (int slotId);

    // One window (strips + plugin panel) or two (overview + rack view, e.g. on two screens)
    void setTwoWindowMode (bool);
    bool isTwoWindowMode() const { return rackWindow != nullptr; }

    // Floating editor windows
    void closeEditorFor (juce::AudioProcessor*);
    void closeEditorsForRack (int rack);
    void closeAllEditors(); // also releases the docked editor

    // Windows and files
    void showDevices();
    void showDriverSettings (int driver);
    void showRouting (int rack = -1);
    void showScope();
    void showPluginScanner();
    void saveSnapshot();
    void loadSnapshot();
    void loadSnapshotFile (const juce::File&);
    void showError (const juce::String& title, const juce::String& message);

    // Consoles
    ConsoleLink* primaryConsole() const;
    void addConsole();
    void removeConsole (int);
    void wireConsole (int);
    void saveConsoles();
    void showConsoleSettings (int);
    void showStoreSceneDialog();
    void finishStoreScene (int result);
    void storeScene (int scene);
    void recallScene (int scene);
    void stepScene (int direction);
    void selectRackForChannel (int console, int channel);
    juce::String slotLabelFor (int driver, int channel) const;
    juce::File consoleSettingsFile (int i) const;

    // Pages
    void addPage();
    void showPageMenu (int page);
    void showRackOnSomePage (int rack); // switch page if the rack isn't on the current one

    // Mono-to-stereo racks: the version each plugin should be, by position
    PluginLibrary::Want wantForSlot (int rack, int insertIndex, const PluginLibrary::Family&) const;
    void fixStereoVersions (int rack);

    // Keyboard shortcuts
    void showShortcuts();
    void captureShortcut (const juce::String& forWhat, std::function<void (juce::KeyPress)> onAccepted);
    juce::String shortcutOwner (const juce::KeyPress&) const; // what already uses it, or empty
    void clearShortcut (const juce::KeyPress&);

    // iPad / web remote
    juce::String buildWebState (const juce::String& query);
    void handleWebCommand (const juce::var&);
    std::shared_ptr<PluginViewCapture> openRemoteView (const juce::String& query, juce::String& error);
    void closeRemoteView (std::shared_ptr<PluginViewCapture>);

    struct RemoteView
    {
        std::weak_ptr<PluginViewCapture> capture;
        juce::AudioProcessor* plugin = nullptr;
        bool openedWindow = false; // opened for the remote: close it afterwards
    };
    std::vector<RemoteView> remoteViews;

    // OSC remote
    void handleOsc (const juce::String& address, const juce::Array<juce::var>& args);

    RackEngine engine;
    PluginLibrary library { RackEngine::getSettingsDir().getChildFile ("plugin-library.xml") };
    std::vector<std::unique_ptr<ConsoleLink>> consoles;
    int activeConsole = 0;
    int currentScene = -1;
    juce::String consoleNote;
    int selectedRack = -1, selectedSlot = -1;

    OscRemote osc;
    WebRemote webRemote;
    std::unique_ptr<MidiControl> midi;

    // Toolbar
    juce::TextButton devicesButton { "Devices" }, routingButton { "Routing" }, scopeButton { "Scope" },
                     scanButton { "Plugins" }, storeSceneButton { "Store Scene" },
                     addRackButton { "+ Rack" }, windowModeButton { "Two Windows" },
                     stackRowsButton { "Stack Rows" },
                     saveButton { "Save" }, loadButton { "Load" }, tapButton { "TAP" };
    class BpmDisplay;
    std::unique_ptr<BpmDisplay> bpmDisplay;

    RackStripsView strips { engine };
    PluginDock dock { engine };
    HotPluginsBar hotBar { engine, *this };
    PageTabs pageTabs { engine };
    std::vector<Shortcut> extraShortcuts = Shortcuts::loadExtras();
    UserKeysBar keysBar { engine, *this };

    class StatusBar;
    std::unique_ptr<StatusBar> statusBar;
    juce::TooltipWindow tooltips { this, 600 };

    class RackViewWindow;
    std::unique_ptr<RackViewWindow> rackWindow;

    std::vector<std::unique_ptr<PluginWindow>> editorWindows;

    class PickerOverlay;
    std::unique_ptr<PickerOverlay> pickerOverlay;

    class InspectorWindow;
    std::vector<std::unique_ptr<InspectorWindow>> inspectorWindows;

    class ToolWindow;
    std::unique_ptr<ToolWindow> devicesWindow, routingWindow, scopeWindow, shortcutsWindow;
    ShortcutsEditor* shortcutsEditor = nullptr;
    RoutingMatrix* routingMatrix = nullptr;
    ScopeEditor* scopeEditor = nullptr;
    DevicesPanel* devicesPanel = nullptr;

    std::unique_ptr<juce::AlertWindow> rackDialog, sceneDialog;
    std::unique_ptr<juce::FileChooser> fileChooser;
    int ticks = 0;

   #if LIVERACK_PREVIEW_HOOKS
    friend struct PreviewHooks;
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
