#!/usr/bin/env python3
"""LiveRack's automated tests.

Runs the Linux test build (configured with -DLIVERACK_PREVIEW=ON) headless
under Xvfb in a set of scenarios, and checks what it reports. Each scenario
gets a fresh home folder, so settings and shows never leak between them.

    python3 tests/run.py --app build-linux/LiveRack_artefacts/Release/LiveRack \\
                         --plugins build-plugins [--only client-basic,protocol]

Scenarios drive LiveRack through its test hooks (Source/PreviewHooks.h for
Native, Source/ClientPreview.h for LiveRack Client), which print lines such
as "REMOTE racks 7 same as the engine 1". Each scenario lists the lines it
must see (regular expressions). A crash fails the scenario.

Exit code 0 when every required check passed. Logs go to tests/out/.
"""

import argparse, os, re, shutil, signal, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")


class Proc:
    """One LiveRack (or helper) process with its own log."""

    def __init__(self, name, cmd, env, log_path, cwd=None):
        self.name, self.log_path = name, log_path
        self.log = open(log_path, "w")
        self.p = subprocess.Popen(cmd, env=env, stdout=self.log, stderr=subprocess.STDOUT, cwd=cwd,
                                  start_new_session=True)

    def alive(self):
        return self.p.poll() is None

    def crashed(self):
        rc = self.p.poll()
        return rc is not None and rc < 0 and -rc in (signal.SIGSEGV, signal.SIGABRT, signal.SIGBUS, signal.SIGFPE, signal.SIGILL)

    def stop(self):
        if self.alive():
            try:
                os.killpg(self.p.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            self.p.wait(timeout=10)
        self.log.close()

    def text(self):
        with open(self.log_path, errors="replace") as f:
            return f.read()


class Scenario:
    def __init__(self, name, description, server=None, client=None, helpers=(), duration=12, expect=(), optional=(),
                 actions=(), plugins_in_home=False, home_files=None):
        self.name, self.description = name, description
        self.server = server          # dict: open mode + extra env, or None
        self.client = client          # dict: test name + args, or None
        self.helpers = helpers        # (delay s, ["cmd", ...]) run alongside
        self.duration = duration
        self.expect = expect          # regexes that must appear
        self.optional = optional      # regexes that should appear (warn only: network-dependent)
        self.actions = actions        # (at s, "stop-server" | "start-server")
        self.plugins_in_home = plugins_in_home   # copy GainPlugin.vst3 into ~/.vst3 (scanning tests)
        self.home_files = home_files or {}       # relative path -> text


def scenarios():
    S = Scenario
    return [
        # ---- Native, through its own test hooks
        S("remote-strips", "Rack strips and plugin panel over the connection layer, against this engine",
          server={"open": "remote"}, duration=18,
          expect=[r"REMOTE connected 1", r"REMOTE racks 7 same as the engine 1",
                  r"REMOTE changes reached the engine: bypass 1 safe 1 group 2 colour 6 name 'Renamed remotely' slot bypass 1",
                  r"REMOTE and back in the copy: bypass 1 safe 1 group 2 colour 6 name 'Renamed remotely' slot bypass 1"]),
        S("overview", "Channels / Rack views, double-click to open, back again",
          server={"open": "overview"}, duration=14,
          expect=[r"OVERVIEW single click: full view 0 selected 1:0",
                  r"OVERVIEW double click: full view 1 panel shown 1 editor 1 rack lit 1",
                  r"OVERVIEW back: full view 0 editor kept 0 overview lit 1",
                  r"OVERVIEW open by id: full view 1 selected 2:1"]),
        S("snapshots", "Snapshot differences and MIDI program changes",
          server={"open": "snapshots"}, duration=10,
          expect=[r"SNAP differences for 2: 3", r"SNAP program 3 ch1 -> current 3 \(expect 3\)",
                  r"SNAP program 1 ch2 ignored -> current 3 \(expect 3\)",
                  r"SNAP first=0, program 11 -> current 10 \(expect 10\)"]),
        S("bigblock", "A block larger than the rack buffer does not resize the buffer in the audio callback",
          server={"open": "bigblock"}, duration=8,
          expect=[r"BIGBLOCK buffer kept after 8192-sample block 1, after a small one 1 \(expect 1, 1\)"]),
        S("isolate-all", "One switch isolates every plugin; a crashing plugin passes audio through instead of taking the app down",
          server={"open": "isolateall"}, duration=16,
          expect=[r"ISOALL before: isolated 0 of \d+ \(expect 0\), switch 0",
                  r"ISOALL on: all isolated 1 switch 1 \(expect 1, 1\)",
                  r"ISOALL added later: ok 1 isolated 1 \(expect 1, 1\)",
                  r"ISOALL new show: switch 0 \(expect 0\)",
                  r"ISOALL reopened: switch 1 all isolated 1 \(expect 1, 1\)",
                  r"ISOALL after the crash: app alive 1, plugin restarts [1-9]\d* .*audio kept flowing 1 \(expect 1\)"]),
        S("isolate-watchdog", "A plugin whose helper keeps crashing is bypassed automatically",
          server={"open": "isowatchdog"}, duration=40,
          expect=[r"ISOWATCH isolated 1 bypassed 0 \(expect 1, 0\)",
                  r"ISOWATCH after one crash: bypassed 0 \(expect 0\)",
                  r"ISOWATCH after [4-9] crashes: bypassed 1 status 'Stopped after repeated crashes.*\(expect bypassed 1\)"]),
        S("reconfig", "Reconfiguring a rack while the audio runs does not hold the audio thread up",
          server={"open": "reconfig"}, duration=14,
          expect=[r"RECONFIG 20 changes, \d+ blocks ran; longest block \d+ ms, under 100 ms 1 \(expect 1\)"]),
        S("remote-snapshots", "The Snapshots screen over the connection layer",
          server={"open": "remotesnapshots"}, duration=12,
          expect=[r"RSNAP list same 1", r"RSNAP differences for 2: 3 same as the engine 1", r"RSNAP scope of 3 same 1",
                  r"RSNAP duplicate 3 -> 4 sent 1, renumber 10 -> 1 refused 1",
                  r"RSNAP engine now: 1:Walk-in \(remote\) 2:Worship Set 3:Message 4:Message copy 12:Baptism",
                  r"RSNAP 3 bpm 0 \(expect 0\), kick in 3 1 \(expect 1\)"]),
        S("remote-routing", "The Routing matrix over the connection layer",
          server={"open": "remoterouting"}, duration=10,
          expect=[r"RROUTE drivers same as here 1",
                  r"RROUTE rack 1 now in 20 out 21,22 \(expect 20, 21,22\); rack 2 in -1 out -1 \(expect -1 -1\)",
                  r"RROUTE the copy sees rack 1 in 20 out 21,22; rack 2 in -1"]),
        S("remote-devices", "Audio drivers over the connection layer",
          server={"open": "remotedevices"}, duration=12,
          expect=[r"RDEV status same 1", r"RDEV drivers now 2 \(expect 2\), the copy sees 2",
                  r"RDEV after remove: 1 \(expect 1\), the copy sees 1"]),
        S("remote-setup", "Consoles, OSC, web remote and Server settings over the connection layer",
          server={"open": "remotesetup"}, duration=12,
          expect=[r"RSET consoles 2 same 1", r"RSET osc same 1 web same 1", r"RSET server same 1", r"RSET server check \d+ items same 1",
                  r"RSET here now: consoles 1 \(expect 1\), follow 0 \(expect 0\), slot 'Renamed slot', osc port 9123 \(expect 9123\)",
                  r"RSET the copy sees: consoles 1, osc 'OSC on port 9123"]),
        S("remote-plugins", "Plugin list, scanning, pages and Hot Plugins over the connection layer",
          server={"open": "remoteplugins"}, duration=12, plugins_in_home=True,
          expect=[r"RPLUG known 4 same as here 1",
                  r"RPLUG here now: Gain still listed 0 \(expect 0\), pages 1 last 'Band' with 2 racks, current page 0, first hot 2:1 \(expect 2:1\)",
                  r"RPLUG scan done by \d+ ms: 'Scan finished. 1 new plugin.' Gain back here 1, in the panel 1"]),
        S("cue-follow", "Following a Yamaha console's CUE (a fake CL5 on the network)",
          server={"open": "cuefollow"}, duration=12,
          helpers=[(0.0, [sys.executable, os.path.join(HERE, "fake_cl5.py")])],
          expect=[r"CUE at 2500 ms: selected rack 2 \(Snare\)", r"CUE at 3500 ms: selected rack 5 \(Pastor Mic\)",
                  r"CUE at 7500 ms: selected rack 5 \(Pastor Mic\)"]),   # CUE with follow off is ignored

        # ---- The server protocol and the iPad page, from outside
        S("protocol", "The /live server protocol from a separate client (Node)",
          server={"open": "demo"}, duration=30,
          helpers=[(7.0, ["node", os.path.join(HERE, "protocol.mjs"), "localhost:8090"])],
          expect=[r'LIVE hello \{"t":"hello","protocol":1', r"LIVE state racks 7", r"LIVE rack 1 bypass after cmd true",
                  r"LIVE view DemoCompressor params 3", r"LIVE rackStep from 0 to 1", r"LIVE deleted true",
                  r"LIVE hot has \[6,1\] true rack 6 group 2", r"LIVE multiCore off true",
                  r"LIVE after storing only rack 2 bypass: 0 differences \(expect 0\)", r"LIVE pong true",
                  r"LIVE second client hello true poll racks 7"],
          optional=[r'LIVE beacon \{"liverack":1']),
        S("ipad-page", "The iPad / web remote page in a headless browser",
          server={"open": "demo"}, duration=25,
          helpers=[(7.0, [sys.executable, os.path.join(HERE, "ipad_page.py")])],
          expect=[r"WEB live socket open: True strips: 7", r"WEB bypass via live: True button: BYPASSED",
                  r"WEB view over live: DemoCompressor params 3 plugin window shown: gui show",
                  r"WEB edit: add-plugin buttons 7 drivers in state 1", r"WEB errors: \[\]"]),

        # ---- LiveRack Client against a server process
        S("client-basic", "Client: connect, plugin picture and input, edits, every screen, pop-out, server lost and back",
          server={"open": "none", "env": {"LIVERACK_PREVIEW_GENERIC": "1"}},
          client={"test": "basic", "args": ["--client", "127.0.0.1:8090"]},
          duration=42, actions=[(22.0, "stop-server"), (26.0, "start-server")],
          expect=[r"CLIENT connected 1 server '.*' version [0-9.]+ racks 7 families 4 overlay 0",
                  r"CLIENT picture 1 size \d+, \d+ valid 1",
                  r"CLIENT after: racks 8 rack0 slots 4 rack1 bypassed 1 bpm 133 snapshot7 1 'From the client' pages 1",
                  r"CLIENT added plugin '\w+'", r"CLIENT devices 1", r"CLIENT routing 1", r"CLIENT snapshots 1", r"CLIENT plugins 1",
                  r"CLIENT pop outs 1 picture in Rack gone 1", r"CLIENT pop out window .* picture valid 1",
                  r"CLIENT pop outs after closing 0", r"CLIENT server gone: connected 0 overlay 1",
                  r"CLIENT server back: connected 1 overlay 0", r"CLIENT connect screen: listening 1"],
          optional=[r"CLIENT connect screen: listening 1 servers \[.+:8090"]),
        S("client-controls", "Client: User Key assignment, MIDI learn and control, shortcuts, rack window, apps, remotes list",
          server={"open": "none", "env": {"LIVERACK_PREVIEW_GENERIC": "1", "LIVERACK_PREVIEW_PRINTCLIENTS": "1"}},
          client={"test": "controls", "args": ["--client", "127.0.0.1:8090"]},
          duration=15,
          expect=[r"CLIENT connected 1 midi 1 key shortcut 0 'F1'",
                  r"CLIENT learned 1 'Note C3 ch 1 \(Test pad\)' -> 'Bypass Kick' rack1 bypassed before 0",
                  r"CLIENT rack1 bypassed after the note 1 key0 label 'Select Snare' apps 1 apps button 1",
                  r"CLIENT after F1 selected 2 \(expect 2\)",
                  r"CLIENT param 0 'Threshold' value 0\.7559",
                  r"CLIENT new snapshots recall rack 0: \[0, 1, 1\]",
                  r"CLIENT rack window 1 dock in it 1 picture 1",
                  r"CLIENT back to one window: dock here 1 rack window 0",
                  r"SERVER remotes '.+' key0 type 3 key0 rack 2"]),
        S("client-click", "Client: pick a server on the connect screen (the 0.28.0 crash)",
          server={"open": "none"}, client={"test": "clickserver", "args": ["--client"]}, duration=11,
          expect=[r"CLIENT clicking '", r"CLIENT after click: view 1 connected 1 racks 7"],
          optional=[r"CLIENT clicking '.*LiveRack 0\."]),   # found by its broadcast, not from the recent list
        S("switch-mode", "Native -> Client -> Native in one window",
          server={"open": "switchmode"}, duration=10,
          expect=[r"NATIVE up \(0\)", r"CLIENT up: connect screen 1 mode client 1", r"NATIVE up \(1\) racks 7"]),
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", required=True, help="the Linux test build of LiveRack")
    ap.add_argument("--plugins", required=True, help="build folder of tests/demo-plugins")
    ap.add_argument("--only", default="", help="comma-separated scenario names")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    app, plugins = os.path.abspath(args.app), os.path.abspath(args.plugins)
    chosen = [s for s in scenarios() if not args.only or s.name in args.only.split(",")]
    if not chosen:
        print(f"No scenario called {args.only!r}; --list shows them")
        return 1
    if args.list:
        for s in chosen:
            print(f"{s.name:18} {s.description}")
        return 0

    os.makedirs(OUT, exist_ok=True)
    gain = next((os.path.join(r, d) for r, ds, _ in os.walk(plugins) for d in ds if d == "GainPlugin.vst3"), None)

    # Two screens: the server (and Native tests) on :99, a Client on :98.
    # LiveRack can't start without one, so wait until both answer.
    displays = [subprocess.Popen(["Xvfb", f":{n}", "-screen", "0", "1440x960x24", "-nolisten", "tcp"],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) for n in (99, 98)]
    try:
        if not wait_for_displays(displays, (99, 98)):
            print("The virtual screens (Xvfb :99 and :98) didn't start. Another one running? Remove /tmp/.X99-lock and /tmp/.X98-lock.")
            return 1

        results = []
        for s in chosen:
            results.append(run_scenario(s, app, plugins, gain))
    finally:
        for d in displays:
            d.terminate()
            try:
                d.wait(timeout=5)
            except subprocess.TimeoutExpired:
                d.kill()

    print()
    failed = [r for r in results if not r[1]]
    for name, ok, notes in results:
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"   ({notes})" if notes else ""))
    print(f"\n{len(results) - len(failed)} of {len(results)} scenarios passed")
    return 1 if failed else 0


def wait_for_displays(procs, numbers, timeout=10.0):
    import socket
    end = time.time() + timeout
    while time.time() < end:
        if any(p.poll() is not None for p in procs):
            return False
        ready = 0
        for n in numbers:
            try:
                with socket.socket(socket.AF_UNIX) as s:
                    s.connect(f"/tmp/.X11-unix/X{n}")
                    ready += 1
            except OSError:
                pass
        if ready == len(numbers):
            return True
        time.sleep(0.2)
    return False


def run_scenario(s, app, plugins, gain):
    print(f"\n=== {s.name}: {s.description}", flush=True)
    homes = [tempfile.mkdtemp(prefix=f"liverack-{s.name}-")]
    for h in homes:
        os.makedirs(os.path.join(h, ".config", "LiveRack"), exist_ok=True)
        for rel, text in s.home_files.items():
            path = os.path.join(h, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as f:
                f.write(text)
        if s.plugins_in_home and gain:
            shutil.copytree(gain, os.path.join(h, ".vst3", "GainPlugin.vst3"))

    base = dict(os.environ, HOME=homes[0], LIVERACK_PREVIEW_PLUGINS=plugins, LIVERACK_TEST_OUT=OUT,
                # Playwright's browsers are in the real home, not the scenario's
                PLAYWRIGHT_BROWSERS_PATH=os.environ.get("PLAYWRIGHT_BROWSERS_PATH",
                                                        os.path.expanduser("~/.cache/ms-playwright")))
    procs, helpers = [], []
    restarts = 0

    def start_server(restart=False):
        nonlocal restarts
        env = dict(base, DISPLAY=":99", LIVERACK_PREVIEW_OPEN=s.server["open"], **s.server.get("env", {}))
        if restart:
            env["LIVERACK_PREVIEW_PLUGINS"] = ""   # a bare server: what comes back after a restart
            restarts += 1
        return Proc("server", [app], env, os.path.join(OUT, f"{s.name}-server{'-' + str(restarts) if restart else ''}.log"))

    server = start_server() if s.server else None
    if server:
        procs.append(server)

    client = None
    if s.client:
        time.sleep(3.0)   # the server first
        chome = tempfile.mkdtemp(prefix=f"liverack-{s.name}-client-")
        os.makedirs(os.path.join(chome, ".config", "LiveRack"), exist_ok=True)
        homes.append(chome)
        env = dict(base, DISPLAY=":98", HOME=chome, LIVERACK_PREVIEW_CLIENT=s.client["test"], LIVERACK_PREVIEW_PLUGINS="")
        env.pop("LIVERACK_PREVIEW_OPEN", None)
        client = Proc("client", [app] + s.client["args"], env, os.path.join(OUT, f"{s.name}-client.log"))
        procs.append(client)

    start = time.time()
    pending_helpers = sorted(s.helpers, key=lambda h: h[0])
    pending_actions = sorted(s.actions, key=lambda a: a[0])
    crashed = []
    while time.time() - start < s.duration:
        t = time.time() - start
        while pending_helpers and pending_helpers[0][0] <= t:
            _, cmd = pending_helpers.pop(0)
            helpers.append(Proc("helper", cmd, base, os.path.join(OUT, f"{s.name}-helper{len(helpers)}.log")))
        while pending_actions and pending_actions[0][0] <= t:
            _, what = pending_actions.pop(0)
            if what == "stop-server" and server:
                server.stop()
            elif what == "start-server":
                server = start_server(restart=True)
                procs.append(server)
        for p in procs:
            if p.crashed() and p not in crashed:
                crashed.append(p)
                print(f"  CRASH: {p.name} ({p.p.returncode})", flush=True)
        time.sleep(0.2)

    for p in procs + helpers:
        if p.crashed() and p not in crashed:
            crashed.append(p)
        p.stop()

    text = "\n".join(p.text() for p in procs + helpers)
    ok = not crashed
    notes = []
    for rx in s.expect:
        found = re.search(rx, text) is not None
        ok = ok and found
        print(f"  {'ok  ' if found else 'MISS'} {rx}")
    for rx in s.optional:
        found = re.search(rx, text) is not None
        if not found:
            notes.append("optional check missed")
        print(f"  {'ok  ' if found else 'warn'} {rx} (optional)")
    if crashed:
        notes.append("crashed: " + ", ".join(p.name for p in crashed))
    if not ok:
        print("  --- last lines of the logs ---")
        for line in text.splitlines()[-40:]:
            print("  | " + line)

    for h in homes:
        shutil.rmtree(h, ignore_errors=True)
    return (s.name, ok, "; ".join(notes))


if __name__ == "__main__":
    sys.exit(main())
