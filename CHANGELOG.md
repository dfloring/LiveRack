# Changelog

What changed in each version, in plain words. The section for a version
becomes its release notes. New work goes under **Unreleased** as it's merged;
the release step renames that to the version.

Versions marked *beta* are for testing on the rig; a beta becomes **stable**
once it has proven itself (Actions > Promote to stable). Services run stable.

## Unreleased

- How LiveRack is tested changed (nothing in the app): the `reconfig` test sometimes crashed the test server because the test's fake audio thread walked the driver list while the test added and removed drivers. In the real app each driver's audio runs on its own device thread and never walks that list, so I found no matching crash there (#67).

- The Companion module is now version 0.1.1. The one in 0.31.0 still said 0.1.0, so Companion refused to import it over the old one (#73). The release steps now bump it whenever the module changes.

- Console links: each rack's name now sits over its own column. In 0.31.0 every name was one column too far to the left, so the first hung over empty space and the last column had none (#70).

## 0.31.0 (2026-11-21)

- The Routing screen now has tabs: **Patch** (the routing matrix, as before) and **Console links**. Console links is a grid with the console's channels down the side (inputs, mixes and matrixes, scrolling) and your racks across the top: click a cell to link that rack to that console channel, so selecting the channel on the console selects the rack. No typing. A stereo rack takes two channels (first click is L, second is R); one channel links to one rack. Works the same on LiveRack Client. The old Console channel box in the rack dialog still works. Console channel names and the Latency tab come later (#53).
- Look (for you to judge in a beta): darker backgrounds everywhere, more vivid rack colours, and 16 rack colours to pick from instead of 8 (Orange, Lime, Sky, Indigo, Magenta, Rose, Mint, Silver are new). A show keeps the same colour per rack, and "Automatic" still cycles through the first eight. The iPad page uses the same greys (#54).
- LiveRack Client: if the plugin list from the server arrived while the "add plugin" picker was open (a scan finishing on the server), the picker could be reading a list that was just thrown away, which can crash the Client. The new list now waits until the picker closes (#62).
- The Companion module no longer drops to "No reply from LiveRack" after about 12 seconds: LiveRack now answers the module's ping, and the module stays connected to an older LiveRack that stays quiet. The module help says to use the Native or Server computer's address, not Client (#60).
- How LiveRack is built changed (nothing in the app): every night main is built from scratch and the automated tests run; if it fails, a "Nightly check failed" issue is opened (#3).
- The release page now lists only the downloads (Mac, Windows and the Companion module) instead of about thirty files, and the release job no longer fails on an empty test log (#47).
- On the Snapshots screen, a new **MIDI Learn** button: press it, hit a note on your controller, and that note recalls the picked snapshot. No User Key needed. The learned note shows next to the snapshot in the list. In LiveRack Client the note is learned on that computer (#52).

## 0.30.0 (2026-11-20)

- A plugin running in its own process that keeps crashing or stops responding (three times in a minute) is now switched to bypass, so the rack shows it as bypassed with the reason, instead of looking switched on while the audio passes through (#7).
- New: show menu > Run all plugins in their own process (experimental). One step to isolate every plugin in the show, including ones you add later, so a crashing plugin can't take LiveRack down. Saved in the show and off unless you turn it on. LiveRack Client has the same menu item, ticked when the server has it on (#25, #33).
- Fixed: with that switch on, a plugin that couldn't move to a helper disappeared when the show was reopened or LiveRack restarted after a crash. It now comes back where it was (#32).
- Isolated plugins now receive MIDI. On Mac their panel shows a live picture of the plugin's window that you can use, and your LiveRack keys (tap tempo, User Keys) still work with it focused. On Windows the panel keeps the Show Plugin Window button (#8, #9).
- New: a pre-service checklist (docs/PRE-SERVICE.md) and a "Before the service" block at the top of Server check that ticks what LiveRack can know: audio drivers, console link, remotes and Server check. It says "All clear" or how many things to look at (#2).
- Fixed: changing a rack's device, buffer size or format no longer makes the audio wait while plugins are re-prepared, which could cause clicks and dropouts mid-show (#26).
- Fixed: the audio no longer resizes a rack's buffer while running, which could cause a click if the device sent a larger block than expected (#27).
- Fixed: on LiveRack Client, the Plugins list could stay out of date after a quick scan (#17).
- Screens now take their colours and text sizes from one shared theme, so they look more consistent. A few colours that were slightly off (the amber in the parameter list and gain-reduction meters, a blue in the parameter list) now match the rest, and text sizes were evened out (#19, #20).
- Behind the scenes (nothing in the app): automated tests run on every change, new versions come out as betas first, work is tracked as GitHub issues, and the reconfigure test covers more (#34).

## 0.29.0 (2026-11-14)

- LiveRack Client has its own MIDI controllers and keyboard shortcuts: add a controller in the Client's Devices and MIDI Learn User Keys or plugin parameters (right-click a plugin > MIDI Learn a parameter).
- User Keys can be assigned from the Client; each computer keeps its own key shortcuts (F1 to F8 to start with).
- Client: the server's apps (QRec...) open live with the Apps button; Rack Window; the plugin menu's snapshot items.
- The server's status bar lists who's connected (Clients by name).
- The Client explains why it can't connect (macOS Local Network permission, a firewall, a wrong port).
- Server check (Windows): whether Windows Firewall lets LiveRack in, with a fix.

## 0.28.1 (2026-11-14)

- Fixed: the Client crashed when you picked a server from the list.
- The connecting screen says why it can't connect.

## 0.28.0 (2026-11-14)

- **LiveRack Client:** the screens on another computer, the audio on the server. Logo menu > Use this computer as a Client. Plugin windows as live pictures you can use, several Clients at once, reconnects by itself.

## 0.27.1 (2026-11-14)

- The overview button is labelled **Channels**.

## 0.27.0 (2026-11-14)

- Plugin scanning, pages and Hot Plugins work over the network (on the way to the Client).

## 0.26.0 (2026-11-14)

- Consoles, OSC, the web remote and Server settings work over the network.

## 0.25.0 (2026-11-13)

- Audio driver settings work over the network.

## 0.24.0 (2026-11-13)

- The Routing matrix works over the network.

## 0.23.0 (2026-11-13)

- The Snapshots screen works over the network.
- Yamaha consoles: Follow CUE selects the rack for the cued channel.

## 0.22.0 (2026-11-13)

- Channels (all racks) and Rack (one rack's plugins, full window) views; double-click a plugin to open it.
- Plugin lists scroll on long racks.
- The iPad page reloads itself after an update.

## 0.21.0 (2026-11-12)

- The rack strips and plugin panel read the show through a connection layer (no visible change).

## 0.20.0 (2026-11-11)

- Server apps on the iPad remote (Windows), pinch to zoom, Open Recent, point console scenes (4.10).

## 0.19.0 (2026-11-11)

- Network choice for the remotes, port 8090, per-plugin timing, Server check, start other apps with LiveRack.

## 0.18.0 (2026-11-11)

- Snapshots screen, store options, MIDI program changes, OSC recall by name.

Earlier versions: see the [releases](https://github.com/dfloring/LiveRack/releases).
