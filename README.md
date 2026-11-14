<p align="center"><img src="docs/banner.png" alt="LiveRack: live audio plugin host for Mac" width="100%"></p>

**LiveRack** hosts racks of VST3 and Audio Unit plugins for live mixing, in the spirit of Waves SuperRack and Fourier transform.engine. It runs on a Mac with Apple silicon, talks to your console over Dante or the Waves SoundGrid Driver, follows scene recalls and channel selection from Yamaha CL/QL/DM7 (by CUE) and Behringer X32 consoles, and can be driven from a Stream Deck through Bitfocus Companion.

<p align="center"><img src="docs/screenshot-main.png" alt="LiveRack main window" width="100%"></p>

### Highlights
- **Racks:** mono, stereo or mono-to-stereo racks, each with its own plugin chain, meters, gain reduction and bypass
- **Several audio drivers at once** (Dante Virtual Soundcard, SoundGrid, any CoreAudio device), each on its own audio thread
- **Console-style routing matrix** with automatic output patching
- **Latency groups A–H** keep related inputs sample-aligned
- **Console Link:** follows scene recalls (Yamaha RCP, X32 scenes/snippets/cues) and channel selection (X32 SEL, Yamaha CUE)
- **Snapshots with scope** down to individual plugins, Recall Safe racks, in-place recall with no audio gap
- **Custom pages, Hot Plugins, User Keys,** keyboard shortcuts, MIDI controllers with MIDI Learn, tap tempo
- **iPad / web remote:** run racks and every plugin parameter from Safari on an iPad, including **the plugin's own window, live** (Mac)
- **OSC remote** and a **Companion module** (`companion-module-liverack-host/`)
- **Mac and Windows** builds (Windows with ASIO for Dante Virtual Soundcard and SoundGrid)

> Status: early and in active development. Test it on your rig before relying on it for a show.

---

## Download

Get the latest build from [Releases](../../releases): the Mac app (Apple silicon), the Windows app (x64, ASIO) and the Companion module. Every push to `main` is also built under [Actions](../../actions).

- **Mac:** unzip, then right-click `LiveRack.app` > **Open** the first time (it isn't notarised).
- **Windows:** unzip and run `LiveRack.exe`. If Windows warns about an unknown publisher: **More info > Run anyway**. Pick your ASIO driver (Dante Virtual Soundcard, SoundGrid) in Devices > driver Settings.

# User guide (v0.25)

Any number of independent racks of VST3/AU plugins on one or more audio drivers at once (Dante Virtual Soundcard, a Dante interface, the Waves SoundGrid Driver). Everything lives in one **show file** (`.liverack`) with its snapshots inside it, and the open show comes back automatically on launch.

## Build (Mac, Apple silicon)

Double-click `build.command` (installs CMake with Homebrew if needed, builds, and writes `build.log`). Or by hand:

```
cmake -B build -G Xcode
cmake --build build --config Release
```

App: `build/LiveRack_artefacts/Release/LiveRack.app`

## The main window

- **Top bar,** in groups: **Show** (save; open, save as, new), **Snapshots** (previous / next, the current snapshot: click it for the list, rename and delete; **STORE**), **Views** (Overview, Rack, Devices, Routing, Plugins, Snapshots: what the main window shows), **Rack controls** (add rack, rack window, stack rows), and **BPM** with **TAP**. Buttons are labelled when the window is wide enough; hover one for more. Click the BPM number to type a tempo. The window title and the far right of the status bar show the LiveRack version.
- **Overview and Rack:** **Overview** shows every rack's strip, full height. Click a plugin or rack to select it; **double-click** a plugin to open it in **Rack**, which gives the plugin panel the whole window. Opening a plugin from a Hot Plugin, a User Key, MIDI or OSC also goes to Rack. **Overview** (or Esc) goes back. With the rack window on (two windows), the main window stays on Overview and plugins open in the rack window.
- **Setup screens** (Devices, Routing, Plugins, Snapshots, Keyboard shortcuts) open in the main window in place of the racks, so everything works on one screen. **‹ Racks**, Esc or the toolbar button again goes back. **Pop Out** moves a screen into its own window (for a second screen), and **Show in Main Window** brings it back; LiveRack remembers which way you like each one.
- **Pages** (tabs above the strips): **All** shows every rack; **+ Page** makes a page (starting with the selected rack). Put racks on pages from a rack's right-click menu > **Pages** (a rack can be on several), or right-click a page tab > Add. New racks join the page you're on. Selecting a rack (from the console, a key, MIDI or OSC) switches to a page that has it. Pages are saved in the show, not in snapshots.
- **Hot Plugins** (under the top bar): up to 12 pinned plugins. Right-click a plugin on a strip > **Pin to Hot Plugins**. Click a tile to open that plugin; drag tiles to reorder; right-click to unpin. Hot Plugins are saved in the show.
- **Strips:** each rack's header shows its name and console channel (**CH**, click to set). When a rack has more plugins than fit, its list scrolls (trackpad or mouse wheel over it), fading out at the edge where there's more. Double-click the name to rename in place. The badges under it are buttons:
  - **MONO / M>S / ST**: switch format: mono, mono in with stereo out, or stereo (the rack drops out for a moment while its plugins switch over)
  - **A–H**: latency group (dashed = none)
  - **SAFE**: Recall Safe
  - colour swatch: pick a colour
  - The line under the badges (e.g. `1 → 1`) shows routing; click it to open **Routing** for that rack.
- **User Keys** (bottom): 8 assignable buttons, lit when what they control is on. Right-click to **Assign** (bypass a rack or plugin, select a rack, open a plugin, Hot Plugin 1–12, recall / next / previous / store scene, show a page, tap tempo, two windows, Rack view, next / previous rack), **Set keyboard shortcut** or **MIDI Learn**.
- **Keyboard shortcuts:** F1–F8 press the User Keys and T taps tempo by default. Change any of them, or add shortcuts for other actions, in Devices > Controllers > **Keyboard shortcuts**. LiveRack warns when keys are already used and refuses ones macOS needs (Cmd+Q, Cmd+W...). Shortcuts work while a LiveRack window is in front.
- **Status bar:** one chip per audio driver (rate, buffer, DSP and the longest block in the last 10 s, dropouts with the time of the last one), then consoles and the OSC remote. Click it to open Devices.

## Devices

- **Audio drivers:** each driver is a panel with a status light, rate, buffer, channels, dropouts, DSP and the racks on it. **Settings** picks the device; **+ Add driver** adds another (up to 4). Each driver runs on its own audio thread, so a hiccup on one doesn't touch the other.
- **DSP** shows the average load (the bar) and the **longest block** in the last 10 s (the tick). The longest block is what matters live: at 100% a block took all of its time, which is a dropout. Each rack strip shows its own share at the bottom right (its longest in the last few seconds; amber over 25%, red over 50%), so you can see which rack is heavy. Inside a rack, each plugin's share shows on its tab in the plugin panel, and when you hover a plugin in a strip (hover the rack's % for all its plugins). The driver card lists the three **heaviest** plugins.
- **Racks run on several CPU cores at once.** Racks don't depend on each other, so each block's racks are shared out between the audio thread and a helper thread per spare core (the DSP line shows how many). Light blocks stay on one core, where it's cheaper. No latency is added. It can be turned off in the Server mode card if a plugin misbehaves with it.
  - A rack runs entirely on one driver: its inputs and outputs are on the same driver. Patching a rack to another driver's channel in Routing moves it there.
  - Two drivers aren't clock-locked to each other inside the Mac. That's fine as long as each rack stays on one driver.
- **Consoles:** one panel per console (+ Add console for more). Each shows its card slots: click a slot to say what's in it (Dante, Waves SoundGrid, stage box...) and which driver channels it arrives on. Routing uses this to label its channel blocks, e.g. "MY Slot 1". Live tiles show the last scene, the selected channel and its rack, and when the console last sent anything. **Settings** opens the connection settings (console type, IP, scene folder, X32 follow and Learn, and the log).
- **Controllers:** starts with no MIDI devices: **+ Add MIDI device** lists what's plugged in. Only added devices are used; an unplugged one stays listed as "not connected" and reconnects on its own. Right-click a device to remove it (with or without its mappings). Also here: MIDI mappings, Keyboard shortcuts and the OSC remote. Lights flash as messages arrive.


### Server mode (running unattended)

For a computer that only runs LiveRack, wherever it lives. In **Devices**, click the **Server mode** card:

- **Start LiveRack when this computer starts.** Also set the computer to log in automatically (Mac: System Settings > Users & Groups; Windows: `netplwiz`, untick "Users must enter a user name and password", then type the account's password carefully: a wrong one is accepted there and simply doesn't log in. If that tick box is missing, turn off Settings > Accounts > Sign-in options > "For improved security, only allow Windows Hello sign-in" first). On the Mac, LiveRack must be in the Applications folder first.
- **Restart LiveRack if it quits unexpectedly.** A small hidden watcher runs alongside (on its own, so ending LiveRack in Task Manager doesn't take the watcher with it) and starts LiveRack again after a crash, with the show it had. Quitting normally doesn't restart it. After three restarts in five minutes it stops trying.
- **Run racks on all CPU cores** (on by default): see Audio drivers above.
- **Start these apps too:** apps LiveRack starts when it starts, if they aren't running already. Add QRec (or SoundGrid Studio) here when the SoundGrid driver needs it running.
- **Server check** (a card under Controllers): checks what makes an unattended computer drop out, sleep, restart or lock mid-service, and offers a fix where LiveRack can make one. Windows: the power plan (High performance), sleep, the screen saver lock, Windows Update paused or not, automatic login, and Avast/AVG (add an exception for LiveRack and your plugins). Mac: sleep and automatic login. Both: start at login, restart after a crash, all CPU cores, each audio driver (missing, recent dropouts, longest block over 80%), and the remote's network. Rechecked every minute.
- An audio driver that was set to a particular device now **waits for that device** instead of moving to the built-in audio. The status bar shows "waiting for ..." and the driver connects by itself when the device appears, e.g. when Dante Virtual Soundcard starts after LiveRack does.

Control it from the iPad / web remote, OSC or Companion as usual.

### Server protocol (for clients)

The web remote's server also speaks a fuller protocol for apps that control LiveRack over the network (the coming LiveRack Client, Companion, your own scripts). Everything is JSON over one WebSocket at `ws://<computer>:<port>/live`:

- On connect the server sends `{"t":"hello", "protocol":1, "version":..., "server":<computer name>}`, then `{"t":"state", "data":...}` whenever anything changes (the same JSON as `/state`; ask for `live=1` to get drivers with their status, consoles, snapshots, Hot Plugins, latency, isolation and server settings), and `{"t":"meters", "data":...}` about 20 times a second (rack in / out levels, gain reduction per plugin with `est` marking estimates, each plugin's and rack's processing time, each driver's DSP and longest block). The state also gives the current page's racks in order (`order`), each rack's `colourIndex`, each plugin's library `key`, and each latency group's delay (`groupLatencyMs`).
- The client sends `{"t":"query", "q":"live=1&view=2:0"}` to choose what the state includes (`view` adds every parameter of one plugin), `{"t":"cmd", ...}` for any command, `{"t":"get", "what":"library"|"shows"|"devices"|"snapshot", "number":n, "id":n}` (answered with a `reply`; `snapshot` gives its differences from now and what it recalls), and `{"t":"ping"}` every few seconds.
- Commands, besides everything the web remote already does: `rackStep` (dir), `sceneStore` (number, name, and optionally `slots` [[rack, slot]…] / `racks` [rack…] to store over only those), `sceneRename` / `sceneDelete` / `sceneDuplicate` (to) / `sceneRenumber` (to), `sceneScope` (number; rack, slot, in; and the four switches), `midiSnapshots` (follow, input, channel, first, send, output, outChannel), `hot` (rack, slot, on), `latencyGroup`, `isolate` (rack, slot, on), `server` (multiCore, startAtLogin, restartAfterCrash), `slotDrop` (rack, slot, toRack, to, copy: a plugin dragged within a rack or to another one), `addPlugin` with `id` (an exact plugin) as well as `key` (a library family), `rackConfig` with `colour` (0–7, -1 = automatic), and the audio drivers: `driverSet` (driver, type, device, rate, buffer), `driverAdd`, `driverRemove`. `get devices` lists each driver's device types, devices, sample rates and buffer sizes.
- **Finding servers:** while the web remote is on, LiveRack announces itself every 2 s with a UDP broadcast on port 47810 (`{"liverack":1, "name", "port", "version"}`), so clients can list the LiveRack computers on the network by name.

The iPad / web remote page now uses this connection too, so changes show up straight away; it falls back to polling where WebSockets aren't available. LiveRack's own rack strips, plugin panel, Snapshots screen, Routing matrix and the audio driver cards in Devices now read the show the same way (in Native, straight from this computer's engine), on the way to LiveRack Client. The state also gives each rack's and plugin's `id`; `sceneScope` takes number -1 for the scope new snapshots start with. Each driver in the state has its `label`, `numIns` / `numOuts` (channels enabled) and `blocks` (the console card slot for each block of 8 channels); `rackConfig` takes -1 in `in` / `out` for unpatched.

## Routing

A console-style crosspoint matrix. Rack inputs (or outputs) run down the side, driver channels across the top, 32 per page. Click a crosspoint to patch, click a lit one to unpatch. Stereo racks patch L and R separately; mono-to-stereo racks have one input and L/R outputs.

**Auto-patch:** patching an input also patches the rack's output to the same number, if that output isn't patched yet and no other rack uses it (otherwise Routing says so and leaves it). Mono-to-stereo racks take the next output too for R. A red ring means two racks share an output (they add together). Switch between **Rack inputs / Rack outputs**, drivers, and channel pages with the tabs. Channel numbers count the channels *enabled* in that driver's Settings.

## Mono-to-stereo racks

Set a rack's format badge to **M>S**: one input, stereo outputs. Plugins run mono until the **stereo point**, then stereo. For example: gate (mono), compressor (mono), reverb (mono in, stereo out), stereo EQ.

- In the picker, a reverb, delay or modulation effect added while the rack is still mono becomes the stereo point; other effects load their mono version. After the stereo point, stereo versions load.
- Plugins with fixed versions (Waves Mono, Mono/Stereo, Stereo) are swapped automatically when you drag them across the stereo point, keeping their settings.
- Right-click any plugin > **Make this the stereo point** to choose it yourself. The stereo point has a blue mark on the strip.
- With no stereo point, the mono signal goes to both outputs.

## Latency groups

Give racks a group letter (A–H) with the badge on the strip. Every rack in a group is delayed to match the slowest one, so all those inputs stay sample-aligned. It re-aligns automatically when plugins are added, removed or change their latency. A bypassed rack in a group keeps the group's delay. Ungrouped racks stay as fast as possible. The strip shows the group and the rack's total latency; the group menu shows each group's latency. Channels that don't go through LiveRack aren't delayed: if they need to line up, set the console's input delay to the group's figure.

## Show file and snapshots

- The **show** holds everything: racks, plugins and their settings, routing, latency groups, pages, Hot Plugins, User Keys, BPM, scope, the rack window, and all the snapshots. **Save** (Cmd+S) writes it; the folder button has **Save As**, **Open** (Cmd+O), **Open Recent** (the last 10 shows) and **New Show**. The window title shows the show's name and "(edited)" when it has unsaved changes; opening another show asks first. An edited show is also kept automatically every minute and on quit, and comes back on launch. Each save first copies the previous version into a "<Show> Backups" folder next to the show file (the newest 20 are kept). Storing over an existing snapshot from the Store dialog, and deleting one, ask first.
- A **snapshot** is a numbered (and optionally named) set of plugin settings stored in the show. **STORE** stores one; console scene numbers map to snapshot numbers.
- Recalling a snapshot **never adds, removes or moves plugins or racks**. It sets the settings and bypass of the plugins already in the show, and (if in scope) rack bypass and BPM. A plugin added after the snapshot was stored is left as it is (the status bar says how many); a plugin that was deleted is skipped.
- **Scope:** each snapshot has its own: what its recall changes (plugin settings, plugin bypass, rack bypass, BPM) and which plugins it touches. Edit it on the Snapshots screen (below), or from a plugin's right-click menu (**Recalled by new snapshots / by snapshot N**). With plugin bypass out of scope, a recall keeps the plugin's current bypass even if its saved settings include a bypass switch.

### The Snapshots screen

**Snapshots** in the top bar (or the snapshot chip's menu) opens it in the main window:

- **The list** (left): every snapshot with its number, name, and what triggers it (the console scene, a MIDI program change or learned control). CURRENT marks the one recalled last; an amber dot means its plugins have changed since it was stored. Click one to work on it, double-click to recall it. Store a new one from the row at the top. **Scope for new snapshots** edits what newly stored snapshots recall.
- **The snapshot picked** (right): rename it, **Recall**, **Store over**, **Duplicate** (to the next free number), **Renumber**, **Delete**. The line under it shows its console scene, when it was stored, how many plugins it recalls, its MIDI trigger and its OSC address.
- **Store over** offers: **Everything it recalls**; **Only the ticked changes** (see below); or **Pick racks or plugins…**: click the plugins (or a rack's name for all of it, including its bypass) and press Store. Storing over always keeps what the snapshot recalls (its scope); only its settings are updated.
- **What it recalls:** the four switches (plugin settings, plugin bypass, rack bypass, BPM), then a row per rack with a chip per plugin: filled = recalled, dashed = left alone. Click to change; a rack's name does the whole rack.
- **Differs from now:** what recalling it would change, plugin by plugin, with the first few settings ("Threshold -20 dB → -24 dB"), plugin bypass and rack bypass. Untick what you don't want and **Store over with the ticked** updates just those. (Snapshots stored before 0.18 only say "settings differ" until they're stored again.)

### Snapshots from MIDI and OSC

- **Program changes** (Snapshots screen, bottom left): turn on **Program changes recall snapshots**, pick the MIDI device (or any) and channel (or any), and which snapshot program 1 recalls (1 by default; 0 for a device that counts from 0, or any offset). A program change learned for something else still does that instead.
- **Send a program change on recall:** pick a MIDI output and channel, and every recall sends the matching program change (lighting, playback or another rack can follow). One that came in by MIDI isn't sent straight back.
- **OSC:** `/liverack/scene/recall` takes a number or a **name** (`/liverack/scene/recall "Worship Set"`, any case), so a Companion button keeps working after renumbering. Feedback adds `/liverack/scene/name` (the current one's), `/liverack/scenes` (how many) and `/liverack/scene/<n>/name` for each.
- **Recall Safe** racks are never touched by recalls.
- Old files: opening a pre-0.13 `.liverack` file opens it as a show. Scene files in a console's scene folder are imported into the show the first time their scene is recalled (if they have the same racks and plugins).

## Rack window

The plugin panel (in **Rack**, or the rack window) shows the selected rack: its colour, name, console channel, latency, in/out meters, **Rack Bypass**, and a tab for each plugin. A resizable plugin window is sized to fit the panel. One that can't resize and doesn't fit isn't squeezed in: use **Pop Out**, or make the window bigger and it comes back. **Rack** (View group in the top bar, or double-click a plugin in a strip) gives the panel the whole main window, below the Hot Plugins; **Overview** or Esc goes back. The **‹ ›** arrows next to the rack's name step to the previous / next rack on the page shown (also available as User Key, MIDI and keyboard-shortcut actions, and over OSC). **Pop Out** opens a plugin in its own window. The rack window button (View group) moves it into its own window, e.g. on a second screen. Whether it's open and where are saved in the show.

## Console Link (follow the console, like Waves ProLink)

- **Yamaha CL/QL:** Remote Control Protocol, TCP port 49280. Use the console's IP from SETUP > NETWORK (FOR MIXER CONTROL).
- **Yamaha DM7:** the same protocol and port, using the For Mixer Control IP. Pick scene list A, or B for the second side in Split mode. Scene 4.00 recalls snapshot 4. A point scene such as 4.10 recalls snapshot 4 too, unless a snapshot is linked to it: pick the snapshot on the Snapshots screen, **Console scene…**, and type 4.10 (one snapshot per scene; Duplicate doesn't copy the link). (Not yet tested on a real DM7: the console link log shows what it sends.)
- **Behringer X32:** OSC on UDP port 10023. Choose whether LiveRack follows **Scenes, Snippets or Cues**, press **Learn**, then recall one on the X32.

Workflow: set up racks, then **STORE** a snapshot with the console's scene number. When the console recalls scene 12, LiveRack recalls snapshot 12. Give racks a console channel (**CH** on the strip) so selecting that channel on the console selects the rack. Yamaha consoles don't report SEL to LiveRack, so on a CL/QL/DM7 pressing **CUE** on an input channel selects its rack instead; switch **Follow CUE** off on the console's card if you use CUE for listening and don't want racks to follow. The console log (Settings) shows everything the console sends.

## MIDI controllers

Any number of MIDI devices at once. **MIDI Learn:**
- for a User Key: right-click it > MIDI Learn, then press a button on your controller
- for a plugin parameter: right-click the plugin > Parameters, select a parameter, **MIDI Learn**, then choose **This plugin only** or **Follow the selected rack** (one set of knobs for every rack: it controls the plugin in the same position in whichever rack is selected)

Buttons (notes, CCs above 0, program changes) press; CCs move parameters. LiveRack sends LED feedback (note or CC 127 / 0) to the MIDI output with the same name as the input. Mappings are listed under Devices > MIDI mappings.

## iPad / web remote

LiveRack runs a small web server (port 8090 on new setups; one set to 8080 before keeps it. Devices > Controllers > **iPad / web remote** shows the address and turns it on or off). On an iPad on the same network, open that address in Safari, e.g. `http://192.168.1.20:8090`.

**Network:** in the same settings, pick which network the remote answers on (each of this computer's network ports is listed with its address; "self-assigned" ones are usually Dante or SoundGrid). LiveRack then answers only devices on that network (and this computer itself), announces itself only there, and shows that network's address. Pick your control network to keep remote traffic off the audio networks. If that port isn't connected, the remote answers on every network so it can still be reached, and says so. The OSC remote has the same choice. **Share > Add to Home Screen** gives it an app-like icon that opens full screen. After LiveRack is updated, an open remote page reloads itself to match.

- Rack strips for the current page: tap the header to select, **BYPASS** to bypass, with level and gain reduction
- Tap a plugin to open **all its parameters** as touch sliders (with filter and plugin bypass); meters like gain reduction show read-only
- Pages, User Keys, previous / next scene and tap tempo
- Any number of iPads, phones or computers at once; it shows "Not connected" if LiveRack goes away

### Building the show from the remote (for a server)

Tap **Edit** at the top of the remote. Then:

- **+ Add rack** at the end of the strips: name, mono / stereo / mono-to-stereo, audio driver, console channel, Recall Safe. It takes the next free channels.
- Tap a **rack's header** to change the same settings plus its input and output channels (named as the driver names them), or to remove it.
- **+ Add plugin** on a rack opens the scanned plugin list with search. The right mono or stereo version loads.
- Tap a **plugin** to move it up or down, open its parameters, or remove it.
- The **show button** (the show's name; a dot means unsaved changes) saves, saves under a new name, starts a new show, or opens one. Shows saved from the remote go in **Documents/LiveRack Shows** on the LiveRack computer; the list also shows the folder of the current show. Opening or starting another show asks about unsaved changes first.

Scanning for plugins, choosing audio devices and installing plugins still happen on the LiveRack computer itself (or over screen sharing).

### Plugin windows on the iPad (Mac and Windows)

Tapping a plugin shows **its own window, live**, and you work it with your fingers. LiveRack opens the window on the Mac if it isn't open, keeps it in front while the iPad uses it, and closes it again when you're done.

- **Tap** = click, **drag** = click-drag (knobs, faders, EQ points), **double-tap** = double-click (resets many controls)
- **Two-finger drag** = scroll wheel, **two-finger tap** = right-click
- **Pinch** zooms the picture (up to 4×) for small controls; while zoomed, two fingers move it around. **Fit** goes back to the whole window
- **Shift / Option / Cmd / Ctrl** stay on until tapped again: Shift-drag for fine adjust, Option-click to reset, as each plugin does it
- **Right-click** makes the next tap a right-click; **Keyboard** types into the plugin's value boxes
- The plugin's preset and option menus open as a touch list
- **Sharp / Fast** picks picture quality; **Sliders** goes back to the parameter list
- **First time:** macOS asks for **Screen Recording** permission for LiveRack (System Settings > Privacy & Security > Screen & System Audio Recording). Turn it on and reopen LiveRack. Because builds aren't signed with an Apple developer ID, each new LiveRack version needs the switch turned off and on again.

- **Windows:** the LiveRack computer must be signed in, unlocked and have a display on (a dummy HDMI plug works on a server). Taps move its real mouse pointer.

### Other apps on the iPad (Windows)

**Apps** at the top of the remote shows the windows of the apps in **Start these apps too** (Devices > Server mode), such as QRec, and you work them the same way as a plugin window. That way the SoundGrid driver's settings can be changed on a server with no screen. If the app isn't running, LiveRack starts it: tap Try again after a few seconds. A minimised window is brought back, and it's kept in front while the iPad shows it. Not yet on Mac.

## Companion

The `companion-module-liverack-host` folder holds a module for Bitfocus Companion 5 (module API 2.1): actions, feedbacks, variables and ready-made presets (rack bypass buttons in each rack's colour with gain reduction, User Keys, Hot Plugins, scenes, tap tempo, pages). Double-click its `install.command` to build it, then import the `.tgz` in Companion (Modules > Import module package). See its README.

## OSC remote (for Companion)

On by default on UDP port **9010** (change it in Devices). Whoever sends a message gets feedback sent back to the address and port it came from; send `/liverack/ping` (or anything) at least once a minute to keep it coming. Numbers are 1-based.

Commands:
- `/liverack/rack/<n>/bypass [0|1]` (no argument = toggle), `/liverack/rack/<n>/select`
- `/liverack/rack/<n>/slot/<m>/bypass [0|1]`, `/liverack/selected/bypass [0|1]`
- `/liverack/key/<n>/press`, `/liverack/hot/<n>/open`
- `/liverack/scene/recall <n>`, `/liverack/scene/next`, `/liverack/scene/prev`, `/liverack/scene/store [n]`
- `/liverack/tap`, `/liverack/bpm <bpm>`, `/liverack/view/twowindows [0|1]`, `/liverack/view/rack [0|1]` (also `/liverack/view/fullview`), `/liverack/rack/next`, `/liverack/rack/prev`
- `/liverack/page/<n>/select` (0 = All), `/liverack/page/next`, `/liverack/page/prev`
- `/liverack/subscribe` (send everything again), `/liverack/ping`

Feedback: `/liverack/racks`, `/selected`, `/scene`, `/bpm`, `/page` (0 = All), `/pages`, `/page/<n>/name`; per rack `/liverack/rack/<n>/name`, `bypass`, `colour` (hex), `format`, `selected`, `group`, `latency` (ms), `gr` (dB); per plugin `/liverack/rack/<n>/slot/<m>/name`, `bypass`; `/liverack/key/<n>/label`, `state`; `/liverack/hot/<n>/name`; `/liverack/driver/<n>/name`, `dsp`, `dropouts`.

## Plugins

- **+ Add plugin** opens the browser: search (Enter adds the top match), big category tiles, favourites and recent on the left. Each plugin shows once and the right mono/stereo version loads automatically. **Drag a plugin straight onto any strip** to put it exactly there.
- Drag plugins within a rack to reorder, onto another rack to move (Hot Plugin pins follow), **Option** to copy with settings.
- **Gain reduction** on each slot comes from, in order: a parameter you picked, the PreSonus VST3 extension, a meter parameter, or an estimate marked "~" for dynamics that report nothing (e.g. Waves).
- **Two Windows** puts the plugin panel in its own window (on a second screen if there is one).


### Running a plugin in its own process (experimental)

Right-click a plugin > **Run in its own process**. The plugin then runs in a hidden helper copy of LiveRack, so if it crashes or stops responding only that plugin is affected: audio passes through it unprocessed for a moment while a fresh helper starts in the background and its last settings are put back. LiveRack stays usable meanwhile. The status bar says when that happened. After three crashes in a minute the plugin is left stopped (audio still passes through).

- It is per plugin, off by default, and saved in the show. Tick it again to bring the plugin back inside LiveRack; settings carry over both ways.
- Audio goes to the helper and back within the same block, so no latency is added. The panel shows how long the hand-off takes.
- The plugin's own window works on the web remote like any other: the helper captures it and plays taps back into it. If the plugin restarts while a device is showing it, the device says so; tap **Try again**.
- Limits for now: the plugin's window is a separate window owned by the helper (not inside the panel), MIDI isn't passed to the plugin, and gain reduction from the PreSonus VST3 extension isn't read (a parameter or the estimate still work).
- To try it: hold **Option** while right-clicking an isolated plugin for **Crash this plugin (test)**.

## Testing with the CL5

- In Dante Controller, route CL5 Dante outputs to the Mac's Dante inputs and the Mac's Dante outputs back to CL5 Dante inputs.
- On the CL5, patch a channel's insert out/in (or a direct out and a return channel) to those Dante ports.
- Total round trip = buffer + device I/O + rack latency + the Dante network latency set in Dante Controller.

## Files

Settings live in `~/Library/Application Support/LiveRack`: `audio-drivers.xml`, `plugins.xml`, `last-session.liverack`, `console-link*.xml`, `user-keys.xml`, `shortcuts.xml`, `midi-control.xml`, `osc-remote.xml`, `web-remote.xml`, `plugin-library.xml`, `layout.xml`.

## Layout

- `Source/RackEngine.*`: audio engine, drivers, latency groups, snapshots (no GUI code: this is the part that moves to a DSP server later)
- `Source/MainComponent.*`: main window; `RackStripsView`, `PluginDock`, `ControlBars` (Hot Plugins, User Keys)
- `Source/RoutingMatrix.*`, `SnapshotsPanel.*`, `DevicesPanel.*`: the setup screens
- `Source/Actions.*`, `MidiControl.*`, `OscRemote.*`, `Shortcuts.*`, `WebRemote.*`: control from keys, MIDI, OSC and the web remote (`Resources/web/remote.html`)
- `Source/PluginViewCapture*`: plugin windows on the web remote (ScreenCaptureKit on the Mac)
- `companion-module-liverack-host/`: the Companion module
- `Source/ConsoleLink*`: console following
- `Source/ServerMode.*`: start at login and the watcher that restarts LiveRack after a crash
- `Source/IsolatedPlugin.*`: a plugin in its own helper process (both sides: the stand-in inside the rack and the helper)

## Next steps

- Plugin isolation, next stages: the plugin's window inside the panel, MIDI, and making it the default once it has proved itself
- Split engine and UI over the network for a separate DSP server

## Licence

Copyright (C) 2026 Daniel Floring.

LiveRack is free software under the **GNU Affero General Public License v3** (see [LICENSE](LICENSE)): you can use, study, change and share it. If you share a changed version, you share its source under the same licence. It comes with no warranty. LiveRack is built with [JUCE](https://juce.com), used under JUCE's AGPLv3 option, and hosts VST3 plugins through the VST 3 SDK (MIT).

The Companion module in `companion-module-liverack-host/` is under the **MIT License** (see its LICENSE).
