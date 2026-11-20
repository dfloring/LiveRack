# Changelog

What changed in each version, in plain words. The section for a version
becomes its release notes. New work goes under **Unreleased** as it's merged;
the release step renames that to the version.

Versions marked *beta* are for testing on the rig; a beta becomes **stable**
once it has proven itself (Actions > Promote to stable). Services run stable.

## Unreleased

- Fixed: with Run all plugins in their own process on, a plugin that couldn't move to a helper disappeared when the show was reopened or LiveRack restarted after a crash. It now comes back where it was (#32).
- New: show menu > Run all plugins in their own process (experimental), one step to isolate every plugin in the show, including ones added later, so a crashing plugin no longer takes LiveRack down. Saved in the show; off unless you turn it on (the default is unchanged until isolated plugins' latency, window and MIDI gaps are dealt with) (#25).
- Fixed: the audio no longer resizes a rack's buffer while running, which could allocate memory on the audio thread and cause a click if the device sent a larger block than expected (#27).
- Fixed: changing a rack's device, buffer size or format no longer makes the audio wait while the rack's plugins are re-prepared, which could cause clicks and dropouts mid-show (#26).
- Screens now take their colours and text sizes from one shared theme, so they look more consistent. A few colours that were slightly off (the amber in the parameter list and gain-reduction meters, a blue in the parameter list) now match the rest. Text sizes were evened out: the half-point sizes are gone and the largest regular text is up to one point smaller and the smallest labels half a point bigger (#19, #20).
- Fixed: on LiveRack Client, the Plugins list could stay out of date after a quick scan (#17).
- How LiveRack is built changed (nothing in the app): automated tests run on
  every change, new versions come out as betas first, and work is tracked as
  GitHub issues.

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
