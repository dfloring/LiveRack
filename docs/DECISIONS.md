# Decisions

Lasting choices and why, newest first. Add one when you decide something a
future change might otherwise undo. Format: what, why, what it costs.

## 2026-11-18: Support agents run in Paperclip; GitHub stays the record
The reviewer, triage and nightly check run as Paperclip agents on the owner's
MacBook (`docs/PAPERCLIP.md`); the head developer stays in its own session.
They only comment and label, and only act on the owner's own PRs and issues.
**Why:** the owner can see and budget the agents in one place, and reviews
happen without the head developer starting them. **Cost:** agents only run
while the MacBook is awake; they share the owner's Claude usage; two places
show tasks (Paperclip's is just a run log).

## 2026-11-18: Colours and text sizes come from Theme.h
Screens take colours from `Theme::` and text sizes from `Theme::fontCaption`
to `fontHeading` (11, 12, 13, 14, 15, 17); display sizes (21, 24, 30) and one
bold 10 in the plugin dock stay where they're used. **Why:** one place to change the look, and the same look on
Native, Client and the rest. **Cost:** sizes moved by at most a point (16 became
15, 18 became 17), and some one-off colours (routing matrix cells, plugin picker
swatches, white-with-alpha text) are still local until a later pass.

## 2026-11-17: A small team process
Issues, branches and pull requests, automated tests in CI, a reviewer agent,
betas before stable, a weekend freeze. **Why:** LiveRack runs services; a bad
build on a Sunday is the worst outcome, and the owner tests rather than reads code.
**Cost:** a little ceremony per change.

## 2026-11-14: MIDI controllers belong to the computer they're plugged into
LiveRack Client runs its own MIDI controllers and shortcuts and sends the same
commands its screens do; MIDI isn't forwarded to the server. **Why:** controller
lights can follow the Client's own selection, no new protocol, two people with
two controllers don't fight. **Cost:** mappings live on each computer;
program-change snapshot recall stays a server setting for MIDI on the server.

## 2026-11-14: The Ryzen runs LiveRack from one fixed folder
`Documents\LiveRack\LiveRack.exe`, replaced in place on update. **Why:** Windows
Firewall remembers its answer per program location; a folder per version
blocked the Client after every update. **Cost:** no side-by-side older version
on the rig (the Releases page keeps them).

## 2026-11-14: One app for Native and Client
LiveRack Client is the same app in another mode (logo menu, or `--client`).
**Why:** the two ends always have the same version, one download. **Cost:** the
Client carries the engine code it doesn't use.

## 2026-11-14: Each Client has its own selection
Selecting a rack on one Client doesn't move the others; all follow the
console's channel select. **Why:** two people can work on different racks.

## 2026-11-12: Every screen reads the show through `LiveConnection`
**Why:** Native and Client stay the same code; the Client was mostly free once
the screens had moved. **Cost:** a feature touches three places
(`LiveConnection`, `LocalConnection`, `RemoteConnection`).

## 2026-11-12: The protocol is JSON over WebSocket, and only grows
State pushed by the server, commands and requests from clients, additive
changes only. **Why:** the iPad page, Companion and the Client all speak it;
old versions keep working against a newer server.

## 2026-11-08: Plugin windows on remotes are pictures, not remote GUIs
The server captures the real window and plays input back. **Why:** works with
any plugin. **Cost:** picture quality and latency (an overhaul is on the list).

## Yamaha consoles follow CUE, not SEL
RCP doesn't report SEL. Follow uses CUE on; CUE off does nothing (the owner is
fine with pressing CUE twice).

## The Mac app is self-signed, not notarised
**Why:** no Apple Developer account yet. **Cost:** Open Anyway on first launch, and
macOS may ask again (Local Network, Screen Recording) after updates.

## Versions stay 0.x
Until the owner decides it's 1.0.
