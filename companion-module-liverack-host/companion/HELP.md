## LiveRack

Controls [LiveRack](https://github.com/dfloring/LiveRack), a live audio plugin host, over OSC.

**In LiveRack:** Devices > Controllers > OSC remote must be on (port 9010 by default).
**In this connection:** enter LiveRack's IP address (127.0.0.1 if Companion runs on the same Mac) and the port.

The connection shows OK once LiveRack answers. Variables and presets follow LiveRack's racks, keys and pages, and update when you rename racks or change colours.

### Actions
- Rack: bypass (toggle / on / off), bypass the selected rack, select
- Plugin: bypass (by rack and position, 1 = top)
- User Key: press (1-8)
- Hot Plugin: open (1-12)
- Scene: recall, next, previous, store
- Page: show (0 = All), next, previous
- Tempo: tap, set BPM
- View: two windows

### Feedbacks
Rack bypassed, rack selected, plugin bypassed, User Key lit, page showing, current scene.

### Variables
`scene`, `bpm`, `selected_rack`, `selected_rack_number`, `page`, and for each rack `rack_N_name`, `rack_N_gr` (gain reduction), `rack_N_latency`, `rack_N_group`; `key_N_label`, `hot_N_name`, `page_N_name`.

### Presets
- **Racks:** bypass buttons in each rack's colour showing its name and gain reduction (amber when bypassed), and select buttons.
- **Keys and plugins:** User Keys (lit like in LiveRack) and Hot Plugins.
- **Scenes, tempo and pages:** previous / store / next scene, tap tempo with BPM, bypass selected rack, and a button per page.
