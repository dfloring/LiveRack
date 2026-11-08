# companion-module-liverack-host

Bitfocus Companion 5 module for LiveRack (module API 2.1). See `companion/HELP.md` for what it does.

## Install (Mac)

1. Double-click `install.command`. It installs Node.js with Homebrew if needed, installs the module's libraries and builds a package (`.tgz`).
2. In Companion: **Modules > Import module package**, choose the `.tgz`.
3. Add a **LiveRack** connection and enter LiveRack's IP and port (9010).

### Developer mode (no package)
After `npm install`, point Companion's **Developer modules path** (Settings) at the folder *containing* this folder. Companion loads it straight from the source and reloads it when files change.

## Files
- `src/main.js`: connection, OSC client, state
- `src/actions.js`, `src/feedbacks.js`, `src/variables.js`, `src/presets.js`
- `src/osc.js`: OSC encoding without extra libraries
