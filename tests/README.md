# LiveRack's automated tests

`tests/run.py` runs the Linux test build of LiveRack headless (Xvfb) through
15 scenarios and checks what it reports: Native screens over the connection
layer, the server protocol from Node, the iPad page in a headless browser,
console CUE follow against a fake CL5, and LiveRack Client against a real
server process (plugin picture and input, edits, every screen, MIDI learn,
User Keys, losing and regaining the server, switching Native <-> Client).

CI runs them on every pull request and every push to main (the "Tests (Linux)"
job); a release isn't made unless they pass.

## Running them yourself (Linux)

```sh
git clone --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE.git juce
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR=$PWD/juce -DLIVERACK_PREVIEW=ON
cmake --build build-linux
cmake -S tests/demo-plugins -B build-plugins -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR=$PWD/juce
cmake --build build-plugins
pip install playwright && python3 -m playwright install chromium   # for the iPad page
python3 tests/run.py --app build-linux/LiveRack_artefacts/Release/LiveRack --plugins build-plugins
```

`--only client-basic,protocol` runs some; `--list` lists them. Logs land in `tests/out/`.

## Adding a test

- **Native:** add a mode to `Source/PreviewHooks.h` (`LIVERACK_PREVIEW_OPEN=<mode>`) that
  drives MainComponent and prints tagged lines (`MYTEST ...`), then a `Scenario`
  in `run.py` listing the lines it must print.
- **Client:** add a test to `Source/ClientPreview.h` (`LIVERACK_PREVIEW_CLIENT=<test>`).
- Print values with what they should be (`... 3 (expect 3)`), so a failure log reads on its own.
- Network-dependent checks (UDP broadcast) go in `optional`: CI runners may not loop broadcasts back.

The test hooks only exist in the Linux test build (`LIVERACK_PREVIEW_HOOKS`);
nothing in them ships in the Mac or Windows app.
