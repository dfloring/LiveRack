# LiveRack architecture

A short map for anyone (or any agent) changing the code. JUCE 8, C++17.

```
              Native (one computer)                      Server + Client (two computers)

   MainComponent --LocalConnection--> RackEngine     ClientComponent --RemoteConnection--+
        |                                 |               (screens)       (WebSocket /live)|
   screens: strips, plugin panel,     audio drivers,                                       |
   Devices, Routing, Plugins,         racks, plugins,     iPad page ---------(/live)-------+
   Snapshots, User Keys...            snapshots                                            v
        |                                                              WebRemote (on the server)
   MidiControl, Shortcuts,                                                 |
   ConsoleLink, OscRemote                                    MainComponent (server side) ---> RackEngine
```

## The engine

- `RackEngine`: audio drivers (several at once), racks (channel routing, mono /
  stereo / mono-to-stereo, latency groups), plugin chains, snapshots and their
  scope, pages, Hot Plugins, show files and the session. Owns the audio callback.
- `IsolatedPlugin`: a plugin in a helper process, so a crash there doesn't stop the show.
- `PluginScanning`, `PluginLibrary`: scanning (crash-safe) and the picker's families
  (mono / stereo versions of the same plugin).
- `GainReduction`: gain reduction meters from plugins that report it, or estimated.

## The connection layer

- `LiveConnection` (abstract): everything a screen needs, as reads ("what racks
  are there") and changes ("bypass this"). A change broadcaster.
- `LocalConnection`: straight to this computer's `RackEngine` (Native, and the server).
- `RemoteConnection`: the same over `/live` (a WebSocket, JSON state pushed by the
  server, commands sent back, `get` requests answered by id). Used by LiveRack Client.
- Screens built on it: `RackStripsView`, `PluginDock`, `SnapshotsPanel`,
  `RoutingScreen` (tabs: `RoutingMatrix`, `ConsoleLinksGrid`), `DevicesPanel`, `DriverSettingsPanel`, `ConsoleLinkPanel`,
  `PluginsPanel`, `ControlBars` (Hot Plugins, pages, User Keys), `Actions`, `MidiControl`.

## The two front ends

- `MainComponent`: Native. Owns the engine, the console links, OSC, the web
  remote server, MIDI and the User Keys. Also answers the protocol for remotes
  (`buildWebState`, `handleWebCommand`).
- `ClientComponent`: LiveRack Client. The connect screen (`ServerFinder` hears
  servers' UDP beacon) and the same screens over a `RemoteConnection`; its own
  MIDI controllers and shortcuts. `RemotePluginView` shows a server's plugin
  window as a live picture and sends the mouse and keyboard back (`/view`).
- `Main.cpp` + `AppMode`: one app, Native or Client (logo menu, `--client`).

## Talking to the outside

- `WebRemote`: the HTTP / WebSocket server: the iPad page (`Resources/web/`),
  `/live` (the protocol), `/view` (plugin and app window pictures, from
  `PluginViewCapture*`), `/state` (old polling), the UDP beacon. Protocol notes: README.
- `ConsoleLink`: Yamaha RCP (CL / QL / DM7) and X32 OSC: scene recall, channel follow.
- `OscRemote`: OSC control (Companion: `companion-module-liverack-host/`).
- `ServerMode` (start at login, restart after a crash, start other apps) and
  `ServerCheck` (what could make an unattended computer drop out).

## Tests

- `PreviewHooks.h` (Native) and `ClientPreview.h` (Client): test-only hooks in
  the Linux test build (`LIVERACK_PREVIEW_HOOKS`), driven by environment variables.
- `tests/run.py`: runs scenarios and checks what the hooks print. See `tests/README.md`.
